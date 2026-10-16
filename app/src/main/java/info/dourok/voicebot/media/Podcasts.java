package info.dourok.voicebot.media;

import info.dourok.voicebot.net.Https;

import org.json.JSONArray;
import org.json.JSONObject;
import org.xml.sax.Attributes;
import org.xml.sax.SAXException;
import org.xml.sax.helpers.DefaultHandler;

import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URLEncoder;
import java.security.MessageDigest;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

import javax.xml.parsers.SAXParser;
import javax.xml.parsers.SAXParserFactory;

/**
 * Free podcasts the Media tab opens on the speaker, the way it opens a radio station.
 *
 * A show is named here by its Apple Podcasts id, not by its feed address. Feeds move between hosts
 * (Anchor became Spotify for Creators, SoundCloud feeds get re-issued), and Apple's directory is
 * where the current address is kept: asking it at play time means a moved feed keeps working
 * without a new release. The address known when this list was written is kept as the fallback for
 * when Apple's directory cannot be reached. The same directory answers the panel's podcast search.
 *
 * The episodes are ordinary audio files on https servers, mostly behind a chain of redirects and
 * often signed by authorities this Android has never heard of, so the speaker's own player is
 * never given their address: ControlServer reads them through {@link Https} and hands them on over
 * local http, passing the player's byte ranges through so that seeking works (and so that an .m4a
 * whose index sits at the end of the file can start at all). Only addresses that came out of a
 * feed read here can be fetched that way, by the token {@link #register} gave them -- the relay is
 * not an open proxy.
 *
 * Plain Java on the JDK, so it can be run off the device.
 */
public final class Podcasts {
    private static final int FEED_MAX_BYTES = 24 * 1024 * 1024;
    private static final long FEED_TTL_MS = 15 * 60 * 1000L;
    private static final long LOOKUP_TTL_MS = 12 * 60 * 60 * 1000L;
    public static final int MAX_EPISODES = 60;
    public static final String ID_PREFIX = "pod:";

    private Podcasts() {}

    /** One show in the built-in list. */
    public static final class Show {
        public final String key, name, shortName, fallbackFeed;
        public final long appleId;

        Show(String key, long appleId, String name, String shortName, String fallbackFeed) {
            this.key = key;
            this.appleId = appleId;
            this.name = name;
            this.shortName = shortName;
            this.fallbackFeed = fallbackFeed;
        }
    }

    // Chosen from Apple's Vietnam chart (October 2026) for a spread of news, talk, stories and
    // learning; every one is free to listen to in full. News comes only from press licensed in
    // Vietnam (RFI was dropped for that reason in 1.5.13).
    private static final List<Show> SHOWS = Collections.unmodifiableList(Arrays.asList(
            // News first, from press licensed in Vietnam (VOV, VnExpress, VietNamNet).
            new Show("vovthoisu", 1448565355L, "VOV - Chương trình Thời sự", "Thời sự VOV",
                    "https://anchor.fm/s/86ec8d4/podcast/rss"),
            new Show("vnexpress", 1751444063L, "VnExpress Hôm nay", "VnExpress",
                    ""),   // address known only to Apple's directory
            new Show("vietnamnet", 1695535499L, "Podcast VietNamNet", "VietNamNet",
                    "https://vietnamnet.vn/podcast-google.rss"),
            new Show("trikycamxuc", 1355572154L, "Tri Kỷ Cảm Xúc", "Tri Kỷ Cảm Xúc",
                    "https://feeds.soundcloud.com/users/soundcloud:users:174938737/sounds.rss"),
            new Show("haveasip", 1540283905L, "Have A Sip", "Have A Sip",
                    "https://anchor.fm/s/103273a04/podcast/rss"),
            new Show("giangoi", 1547160191L, "Giang ơi Radio", "Giang ơi Radio",
                    "https://feeds.soundcloud.com/users/soundcloud:users:341012174/sounds.rss"),
            new Show("doctruyen", 1702660933L, "Đọc truyện đêm khuya - Đài Hà Nội", "Truyện đêm khuya",
                    "https://anchor.fm/s/e75b3a50/podcast/rss"),
            new Show("thitruong", 1649143152L, "5 Phút Chuyện Thị Trường", "Chuyện thị trường",
                    "https://anchor.fm/s/b263c470/podcast/rss"),
            new Show("spiderum", 1498937274L, "Spiderum Official", "Spiderum",
                    "https://anchor.fm/s/13c13550/podcast/rss"),
            new Show("hieutv", 1554576578L, "HIEU.TV", "HIEU.TV",
                    "https://anchor.fm/s/4cfb55bc/podcast/rss"),
            new Show("betterversion", 1588566083L, "Better Version", "Better Version",
                    "https://anchor.fm/s/6d0b6694/podcast/rss"),
            new Show("minhniem", 1505157864L, "Minh Niệm", "Minh Niệm",
                    "https://anchor.fm/s/19d07410/podcast/rss"),
            new Show("visaothenhi", 1611114706L, "Vì sao thế nhỉ!", "Vì sao thế nhỉ",
                    "https://anchor.fm/s/8453cddc/podcast/rss"),
            new Show("cotich", 1532630528L, "Kho truyện cổ tích - CMDD Audio", "Truyện cổ tích",
                    "https://anchor.fm/s/b0e954c/podcast/rss"),
            new Show("6minenglish", 262026947L, "6 Minute English - BBC", "6 Minute English",
                    "https://podcasts.files.bbci.co.uk/p02pc9tn.rss")));

    public static List<Show> all() {
        return SHOWS;
    }

    public static Show byKey(String key) {
        for (Show s : SHOWS) if (s.key.equals(key)) return s;
        return null;
    }

    // ── Apple's directory ───────────────────────────────────────────────────────────

    private static final class Lookup {
        final String feed, name, author, image;
        final long at;

        Lookup(String feed, String name, String author, String image, long at) {
            this.feed = feed;
            this.name = name;
            this.author = author;
            this.image = image;
            this.at = at;
        }
    }

    private static final Map<Long, Lookup> LOOKUPS = new HashMap<Long, Lookup>();

    /** What Apple's directory says about a show; null when it cannot be asked or does not know. */
    static Lookup lookup(long appleId, String userAgent) {
        synchronized (LOOKUPS) {
            Lookup hit = LOOKUPS.get(appleId);
            if (hit != null && System.currentTimeMillis() - hit.at < LOOKUP_TTL_MS) return hit;
        }
        try {
            String url = "https://itunes.apple.com/lookup?entity=podcast&id=" + appleId;
            JSONObject j = new JSONObject(Https.readText(Https.get(url, ua(userAgent)), 2 * 1024 * 1024));
            JSONArray results = j.optJSONArray("results");
            JSONObject r = results == null ? null : results.optJSONObject(0);
            String feed = r == null ? "" : r.optString("feedUrl", "");
            if (!feed.startsWith("http")) return null;
            Lookup l = new Lookup(feed, r.optString("collectionName", ""), r.optString("artistName", ""),
                    r.optString("artworkUrl600", r.optString("artworkUrl100", "")), System.currentTimeMillis());
            synchronized (LOOKUPS) {
                LOOKUPS.put(appleId, l);
            }
            return l;
        } catch (Exception e) {
            return null;
        }
    }

    /** The panel's podcast search: {"ok":true,"results":[{apple_id,name,author,image}]}. */
    public static String searchJson(String term, String userAgent) {
        try {
            String q = term == null ? "" : term.trim();
            if (q.isEmpty()) return "{\"ok\":false,\"error\":\"Nhập tên podcast cần tìm.\"}";
            if (q.length() > 120) q = q.substring(0, 120);
            String url = "https://itunes.apple.com/search?media=podcast&entity=podcast&country=VN&limit=30&term="
                    + URLEncoder.encode(q, "UTF-8");
            JSONObject j = new JSONObject(Https.readText(Https.get(url, ua(userAgent)), 4 * 1024 * 1024));
            JSONArray results = j.optJSONArray("results");
            JSONArray out = new JSONArray();
            for (int i = 0; results != null && i < results.length(); i++) {
                JSONObject r = results.optJSONObject(i);
                if (r == null || r.optLong("collectionId", 0) <= 0 || !r.optString("feedUrl", "").startsWith("http")) continue;
                out.put(new JSONObject()
                        .put("apple_id", r.optLong("collectionId"))
                        .put("name", r.optString("collectionName", ""))
                        .put("author", r.optString("artistName", ""))
                        .put("image", r.optString("artworkUrl100", ""))
                        .put("count", r.optInt("trackCount", 0)));
            }
            return new JSONObject().put("ok", true).put("results", out).toString();
        } catch (Exception e) {
            return "{\"ok\":false,\"error\":" + JSONObject.quote("Không tìm được podcast (" + message(e) + ").") + "}";
        }
    }

    // ── Episodes ────────────────────────────────────────────────────────────────────

    /** One episode, as the panel lists it and the player plays it. */
    public static final class Episode {
        public final String title, date, duration, image, url;

        Episode(String title, String date, String duration, String image, String url) {
            this.title = title;
            this.date = date;
            this.duration = duration;
            this.image = image;
            this.url = url;
        }
    }

    /** What a feed holds: the show's own name and picture, and its newest episodes first. */
    public static final class Channel {
        public final String title, author, image;
        public final List<Episode> episodes;

        Channel(String title, String author, String image, List<Episode> episodes) {
            this.title = title;
            this.author = author;
            this.image = image;
            this.episodes = episodes;
        }
    }

    private static final class Cached {
        final Channel channel;
        final long at;

        Cached(Channel channel, long at) {
            this.channel = channel;
            this.at = at;
        }
    }

    private static final Map<String, Cached> FEEDS = new HashMap<String, Cached>();

    /**
     * The episodes of a built-in show ({@code key}) or of a show found by search ({@code appleId}),
     * as the panel wants them: {"ok":true,"show":{...},"episodes":[{id,title,artist,thumbnail,
     * duration,date}]}. Each id is "pod:&lt;token&gt;", which /api/media/play and the relay accept.
     */
    public static String episodesJson(String key, long appleId, String userAgent) {
        try {
            Show show = key == null || key.isEmpty() ? null : byKey(key);
            long id = show != null ? show.appleId : appleId;
            if (id <= 0) return "{\"ok\":false,\"error\":\"Không rõ podcast nào.\"}";
            Lookup l = lookup(id, userAgent);
            String feed = l != null ? l.feed : show != null ? show.fallbackFeed : "";
            if (feed.isEmpty()) return "{\"ok\":false,\"error\":\"Không lấy được địa chỉ podcast từ danh bạ Apple Podcasts.\"}";
            Channel c;
            try {
                c = channel(feed, userAgent);
            } catch (Exception first) {
                // Apple's address failed: the one this list was written with may still work.
                if (show == null || show.fallbackFeed.equals(feed)) throw first;
                c = channel(show.fallbackFeed, userAgent);
            }
            String showName = show != null ? show.name : !c.title.isEmpty() ? c.title : l != null ? l.name : "";
            String image = !c.image.isEmpty() ? c.image : l != null ? l.image : "";
            JSONArray eps = new JSONArray();
            for (Episode e : c.episodes) {
                eps.put(new JSONObject()
                        .put("video_id", ID_PREFIX + register(e.url))
                        .put("title", e.title)
                        .put("artist", showName)
                        .put("thumbnail", e.image.isEmpty() ? image : e.image)
                        .put("duration", e.duration)
                        .put("date", e.date));
            }
            JSONObject s = new JSONObject()
                    .put("key", show != null ? show.key : "")
                    .put("apple_id", id)
                    .put("name", showName)
                    .put("author", !c.author.isEmpty() ? c.author : l != null ? l.author : "")
                    .put("image", image);
            return new JSONObject().put("ok", true).put("show", s).put("episodes", eps).toString();
        } catch (Exception e) {
            return "{\"ok\":false,\"error\":" + JSONObject.quote("Không đọc được danh sách tập (" + message(e) + ").") + "}";
        }
    }

    static Channel channel(String feed, String userAgent) throws Exception {
        synchronized (FEEDS) {
            Cached hit = FEEDS.get(feed);
            if (hit != null && System.currentTimeMillis() - hit.at < FEED_TTL_MS) return hit.channel;
        }
        HttpURLConnection c = Https.get(feed, ua(userAgent));
        Channel ch;
        try {
            int code = c.getResponseCode();
            if (code < 200 || code > 299) throw new java.io.IOException("HTTP " + code);
            InputStream in = new Capped(c.getInputStream(), FEED_MAX_BYTES);
            try {
                ch = parse(in, MAX_EPISODES);
            } finally {
                in.close();
            }
        } finally {
            c.disconnect();
        }
        if (ch.episodes.isEmpty()) throw new java.io.IOException("podcast này không có tập âm thanh nào");
        synchronized (FEEDS) {
            FEEDS.put(feed, new Cached(ch, System.currentTimeMillis()));
        }
        return ch;
    }

    /** Thrown to stop reading a feed once enough episodes are in: big feeds run to megabytes. */
    private static final class Enough extends SAXException {
        Enough() {
            super("enough");
        }
    }

    /** Read an RSS feed: channel title/author/picture, then up to {@code max} audio episodes. */
    static Channel parse(InputStream in, final int max) throws Exception {
        SAXParserFactory f = SAXParserFactory.newInstance();
        f.setNamespaceAware(false);
        f.setValidating(false);
        for (String feature : new String[]{
                "http://xml.org/sax/features/external-general-entities",
                "http://xml.org/sax/features/external-parameter-entities",
                "http://apache.org/xml/features/nonvalidating/load-external-dtd"}) {
            try {
                f.setFeature(feature, false);
            } catch (Exception ignored) {
                // not every parser knows every feature; Android's never fetches entities anyway
            }
        }
        SAXParser p = f.newSAXParser();
        final String[] chan = {"", "", "", ""};   // title, author, itunes:image, <image><url>
        final List<Episode> eps = new ArrayList<Episode>();
        DefaultHandler h = new DefaultHandler() {
            final StringBuilder text = new StringBuilder();
            boolean inItem, inImage;
            String title, date, duration, image, url;

            @Override
            public void startElement(String uri, String local, String qName, Attributes a) {
                String n = name(local, qName);
                text.setLength(0);
                if (n.equals("item") || n.equals("entry")) {
                    inItem = true;
                    title = date = duration = image = url = "";
                } else if (n.equals("image") && !inItem) {
                    inImage = true;
                } else if (n.equals("enclosure") && inItem && url.isEmpty()) {
                    String u = attr(a, "url");
                    String type = attr(a, "type").toLowerCase(Locale.US);
                    if (u.startsWith("http") && isAudio(type, u)) url = u.trim();
                } else if (n.equals("itunes:image")) {
                    String href = attr(a, "href");
                    if (inItem) image = href;
                    else if (chan[2].isEmpty()) chan[2] = href;
                }
            }

            @Override
            public void characters(char[] ch, int start, int length) {
                if (text.length() < 4000) text.append(ch, start, length);
            }

            @Override
            public void endElement(String uri, String local, String qName) throws SAXException {
                String n = name(local, qName);
                String t = text.toString().trim();
                if (inItem) {
                    if (n.equals("title") && title.isEmpty()) title = t;
                    else if (n.equals("pubDate")) date = shortDate(t);
                    else if (n.equals("itunes:duration")) duration = clock(t);
                    else if (n.equals("item") || n.equals("entry")) {
                        inItem = false;
                        if (!url.isEmpty() && !title.isEmpty()) {
                            eps.add(new Episode(title, date, duration, image, url));
                            if (eps.size() >= max) throw new Enough();
                        }
                    }
                } else if (inImage) {
                    if (n.equals("url") && chan[3].isEmpty()) chan[3] = t;
                    else if (n.equals("image")) inImage = false;
                } else if (n.equals("title") && chan[0].isEmpty()) {
                    chan[0] = t;
                } else if (n.equals("itunes:author") && chan[1].isEmpty()) {
                    chan[1] = t;
                }
                text.setLength(0);
            }
        };
        try {
            p.parse(in, h);
        } catch (Enough done) {
            // read as much as wanted
        }
        // itunes:image is the large square picture podcast apps use; <image> is often a small one.
        return new Channel(chan[0], chan[1], chan[2].isEmpty() ? chan[3] : chan[2], eps);
    }

    private static String name(String local, String qName) {
        return qName != null && !qName.isEmpty() ? qName : local == null ? "" : local;
    }

    private static String attr(Attributes a, String name) {
        String v = a.getValue(name);
        return v == null ? "" : v;
    }

    /** Audio the speaker can play; a video enclosure is skipped rather than half-played. */
    static boolean isAudio(String type, String url) {
        if (type.startsWith("audio/")) return true;
        if (!type.isEmpty() && !type.equals("application/octet-stream")) return false;
        String path = url.toLowerCase(Locale.US);
        int q = path.indexOf('?');
        if (q >= 0) path = path.substring(0, q);
        return path.endsWith(".mp3") || path.endsWith(".m4a") || path.endsWith(".aac") || path.endsWith(".ogg");
    }

    /** itunes:duration as h:mm:ss or m:ss, from either "5400" or "01:30:00". */
    static String clock(String raw) {
        String s = raw.trim();
        if (s.isEmpty()) return "";
        long total;
        try {
            if (s.contains(":")) {
                total = 0;
                for (String part : s.split(":")) total = total * 60 + Long.parseLong(part.trim());
            } else {
                total = (long) Double.parseDouble(s);
            }
        } catch (Exception e) {
            return "";
        }
        if (total <= 0) return "";
        long h = total / 3600, m = (total % 3600) / 60, sec = total % 60;
        return h > 0 ? String.format(Locale.US, "%d:%02d:%02d", h, m, sec) : String.format(Locale.US, "%d:%02d", m, sec);
    }

    /** An RSS date as dd/MM/yyyy; "" when it cannot be read. */
    static String shortDate(String raw) {
        String s = raw.trim();
        for (String pattern : new String[]{"EEE, dd MMM yyyy HH:mm:ss Z", "EEE, d MMM yyyy HH:mm:ss Z",
                "EEE, dd MMM yyyy HH:mm:ss zzz", "EEE, dd MMM yyyy HH:mm Z", "dd MMM yyyy HH:mm:ss Z"}) {
            try {
                SimpleDateFormat in = new SimpleDateFormat(pattern, Locale.US);
                in.setLenient(true);
                Date d = in.parse(s);
                if (d != null) return new SimpleDateFormat("dd/MM/yyyy", Locale.US).format(d);
            } catch (Exception ignored) {
                // try the next spelling
            }
        }
        return "";
    }

    // ── Relay tokens ────────────────────────────────────────────────────────────────

    private static final int MAX_TOKENS = 4000;
    private static final Map<String, String> TOKENS = new LinkedHashMap<String, String>(256, 0.75f, true) {
        @Override
        protected boolean removeEldestEntry(Map.Entry<String, String> eldest) {
            return size() > MAX_TOKENS;
        }
    };

    /** A short, stable name for an episode address read from a feed. */
    static String register(String url) {
        String token = digest(url);
        synchronized (TOKENS) {
            TOKENS.put(token, url);
        }
        return token;
    }

    /** The address a token stands for; null if no feed read here contained it. */
    public static String urlOf(String token) {
        if (token == null) return null;
        synchronized (TOKENS) {
            return TOKENS.get(token);
        }
    }

    private static String digest(String s) {
        try {
            byte[] d = MessageDigest.getInstance("SHA-1").digest(s.getBytes("UTF-8"));
            StringBuilder b = new StringBuilder();
            for (int i = 0; i < 10; i++) b.append(String.format(Locale.US, "%02x", d[i] & 0xff));
            return b.toString();
        } catch (Exception e) {
            return Integer.toHexString(s.hashCode());
        }
    }

    private static Map<String, String> ua(String userAgent) {
        Map<String, String> h = new HashMap<String, String>();
        h.put("User-Agent", userAgent == null || userAgent.isEmpty() ? "db-robot-r1" : userAgent);
        return h;
    }

    private static String message(Exception e) {
        return e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage();
    }

    /** Stops a runaway feed from eating the speaker's memory. */
    private static final class Capped extends java.io.FilterInputStream {
        private long left;

        Capped(InputStream in, long max) {
            super(in);
            left = max;
        }

        @Override
        public int read() throws java.io.IOException {
            if (left <= 0) throw new java.io.IOException("podcast quá lớn");
            int b = super.read();
            if (b >= 0) left--;
            return b;
        }

        @Override
        public int read(byte[] buf, int off, int len) throws java.io.IOException {
            if (left <= 0) throw new java.io.IOException("podcast quá lớn");
            int n = super.read(buf, off, (int) Math.min(len, left));
            if (n > 0) left -= n;
            return n;
        }
    }
}
