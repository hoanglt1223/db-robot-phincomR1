package info.dourok.voicebot.media;

import info.dourok.voicebot.net.Https;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.IOException;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Finds videos on YouTube for the control panel's YouTube player.
 *
 * The videos themselves play in the panel, inside YouTube's own embedded player, on the phone or
 * computer that has the panel open: nothing is downloaded or streamed through the speaker, and no
 * music server is involved. Only the search runs here, because a web page cannot ask YouTube
 * directly (the browser refuses the cross-site request), and YouTube's documented search API
 * needs a key of its own.
 *
 * So this asks the same search endpoint youtube.com's own pages use (as NewPipe and the like do),
 * and falls back to reading the results page if that is refused. Either way the answer is the
 * same structure, walked here for its {@code videoRenderer} entries. YouTube changes that
 * structure now and then; when it does, the panel says the search failed and still offers to
 * paste a link or open youtube.com.
 *
 * Plain Java on the JDK, so it can be run off the device.
 */
public final class YouTubeSearch {
    private static final String API = "https://www.youtube.com/youtubei/v1/search?prettyPrint=false";
    private static final String PAGE = "https://www.youtube.com/results?search_query=";
    private static final String CLIENT_VERSION = "2.20260805.01.00";
    /** "Videos only", as youtube.com's filter sends it. */
    private static final String VIDEOS_ONLY = "EgIQAfABAQ%3D%3D";
    private static final String BROWSER_UA =
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36";
    private static final int MAX_BYTES = 6 * 1024 * 1024;
    public static final int MAX_RESULTS = 30;

    private YouTubeSearch() {}

    /** One search result. */
    public static final class Video {
        public final String id, title, channel, duration;
        public final boolean live;

        Video(String id, String title, String channel, String duration, boolean live) {
            this.id = id;
            this.title = title;
            this.channel = channel;
            this.duration = duration;
            this.live = live;
        }

        JSONObject toJson() throws org.json.JSONException {
            return new JSONObject()
                    .put("id", id)
                    .put("title", title)
                    .put("channel", channel)
                    .put("duration", duration)
                    .put("live", live)
                    .put("thumbnail", "https://i.ytimg.com/vi/" + id + "/mqdefault.jpg");
        }
    }

    /** The panel's answer: {"ok":true,"results":[...]} or {"ok":false,"error":"..."}. */
    public static String searchJson(String query) {
        try {
            String q = query == null ? "" : query.trim();
            if (q.isEmpty()) {
                return "{\"ok\":false,\"error\":\"Nhập nội dung cần tìm.\"}";
            }
            if (q.length() > 200) q = q.substring(0, 200);
            List<Video> found = search(q);
            JSONArray arr = new JSONArray();
            for (Video v : found) arr.put(v.toJson());
            return new JSONObject().put("ok", true).put("results", arr).toString();
        } catch (Exception e) {
            String msg = e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage();
            return "{\"ok\":false,\"error\":" + JSONObject.quote("Không tìm được trên YouTube (" + msg + ").") + "}";
        }
    }

    static List<Video> search(String q) throws Exception {
        Exception first;
        try {
            List<Video> r = parse(new JSONObject(viaApi(q)), MAX_RESULTS);
            if (!r.isEmpty()) return r;
            first = new IOException("không có kết quả");
        } catch (Exception e) {
            first = e;
        }
        // The results page carries the same data, embedded as "var ytInitialData = {...};".
        try {
            JSONObject data = initialData(viaPage(q));
            if (data != null) {
                List<Video> r = parse(data, MAX_RESULTS);
                if (!r.isEmpty() || first.getMessage() != null && first.getMessage().equals("không có kết quả")) {
                    return r;
                }
            }
        } catch (Exception ignored) {
            // report the first failure, which is the more telling one
        }
        throw first;
    }

    private static Map<String, String> headers() {
        Map<String, String> h = new HashMap<String, String>();
        h.put("User-Agent", BROWSER_UA);
        h.put("Accept-Language", "vi-VN,vi;q=0.9,en;q=0.6");
        // Consent already given: without it, visitors from some countries get the consent page.
        h.put("Cookie", "SOCS=CAE=");
        return h;
    }

    private static String viaApi(String q) throws Exception {
        JSONObject client = new JSONObject()
                .put("hl", "vi").put("gl", "VN")
                .put("clientName", "WEB").put("clientVersion", CLIENT_VERSION)
                .put("originalUrl", "https://www.youtube.com").put("platform", "DESKTOP")
                .put("utcOffsetMinutes", 420);
        JSONObject body = new JSONObject()
                .put("context", new JSONObject().put("client", client))
                .put("query", q)
                .put("params", VIDEOS_ONLY);
        Map<String, String> h = headers();
        h.put("Content-Type", "application/json");
        h.put("Origin", "https://www.youtube.com");
        h.put("Referer", "https://www.youtube.com");
        h.put("X-YouTube-Client-Name", "1");
        h.put("X-YouTube-Client-Version", CLIENT_VERSION);
        return Https.readText(Https.post(API, h, body.toString().getBytes(StandardCharsets.UTF_8)), MAX_BYTES);
    }

    private static String viaPage(String q) throws Exception {
        String url = PAGE + URLEncoder.encode(q, "UTF-8") + "&sp=" + VIDEOS_ONLY + "&hl=vi&gl=VN";
        return Https.readText(Https.get(url, headers()), MAX_BYTES);
    }

    /** The JSON a youtube.com page embeds as {@code ytInitialData}, or null. */
    static JSONObject initialData(String html) {
        String[] markers = {"var ytInitialData = ", "window[\"ytInitialData\"] = ", "ytInitialData = "};
        for (String m : markers) {
            int at = html.indexOf(m);
            if (at < 0) continue;
            int start = html.indexOf('{', at + m.length() - 1);
            if (start < 0) continue;
            int end = matchingBrace(html, start);
            if (end < 0) continue;
            try {
                return new JSONObject(html.substring(start, end + 1));
            } catch (Exception e) {
                // try the next spelling
            }
        }
        return null;
    }

    /** Index of the brace closing the object that opens at {@code start}, strings skipped. */
    private static int matchingBrace(String s, int start) {
        int depth = 0;
        boolean inString = false;
        for (int i = start; i < s.length(); i++) {
            char c = s.charAt(i);
            if (inString) {
                if (c == '\\') i++;
                else if (c == '"') inString = false;
            } else if (c == '"') {
                inString = true;
            } else if (c == '{') {
                depth++;
            } else if (c == '}') {
                if (--depth == 0) return i;
            }
        }
        return -1;
    }

    /** Every {@code videoRenderer} in a search answer, in order, at most {@code max}. */
    static List<Video> parse(JSONObject root, int max) {
        List<Video> out = new ArrayList<Video>();
        Set<String> seen = new HashSet<String>();
        walk(root, out, seen, max, 0);
        return out;
    }

    private static void walk(Object node, List<Video> out, Set<String> seen, int max, int depth) {
        if (out.size() >= max || depth > 40) return;
        if (node instanceof JSONObject) {
            JSONObject o = (JSONObject) node;
            JSONObject v = o.optJSONObject("videoRenderer");
            if (v != null) {
                Video video = video(v);
                if (video != null && seen.add(video.id)) out.add(video);
            }
            JSONArray names = o.names();
            if (names == null) return;
            for (int i = 0; i < names.length() && out.size() < max; i++) {
                String k = names.optString(i);
                if ("videoRenderer".equals(k)) continue;
                walk(o.opt(k), out, seen, max, depth + 1);
            }
        } else if (node instanceof JSONArray) {
            JSONArray a = (JSONArray) node;
            for (int i = 0; i < a.length() && out.size() < max; i++) {
                walk(a.opt(i), out, seen, max, depth + 1);
            }
        }
    }

    private static Video video(JSONObject v) {
        String id = v.optString("videoId", "");
        if (!validId(id)) return null;
        String title = text(v.optJSONObject("title"));
        if (title.isEmpty()) return null;
        String channel = text(v.optJSONObject("ownerText"));
        if (channel.isEmpty()) channel = text(v.optJSONObject("longBylineText"));
        String duration = text(v.optJSONObject("lengthText"));
        boolean live = duration.isEmpty();
        return new Video(id, title, channel, duration, live);
    }

    /** A YouTube text object: {"simpleText":"..."} or {"runs":[{"text":"..."},...]}. */
    private static String text(JSONObject t) {
        if (t == null) return "";
        String simple = t.optString("simpleText", "");
        if (!simple.isEmpty()) return simple;
        JSONArray runs = t.optJSONArray("runs");
        if (runs == null) return "";
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < runs.length(); i++) {
            JSONObject r = runs.optJSONObject(i);
            if (r != null) b.append(r.optString("text", ""));
        }
        return b.toString();
    }

    /** Eleven characters of A-Z a-z 0-9 - _: what the panel builds the player address from. */
    public static boolean validId(String id) {
        if (id == null || id.length() != 11) return false;
        for (int i = 0; i < id.length(); i++) {
            char c = id.charAt(i);
            boolean ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
            if (!ok) return false;
        }
        return true;
    }
}
