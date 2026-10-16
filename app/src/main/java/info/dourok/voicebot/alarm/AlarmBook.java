package info.dourok.voicebot.alarm;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Calendar;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.TimeZone;

/**
 * The alarms the owner sets in the control panel (tab System → Báo thức), and the sounds they ring
 * with. Pure rules -- no Android -- so they can be checked off the speaker: which alarm rings next
 * and when, whether an alarm that just fired is really due, and where uploaded sounds are kept.
 *
 * An alarm has a time, a set of weekdays (none = rings once, then switches itself off), a sound
 * ("default" = the tone in assets/alarm_default.wav, otherwise the name of an uploaded file) and an
 * optional label. Days are a bit mask with Monday as bit 0 and Sunday as bit 6, the way the week is
 * written in Vietnamese (Thứ Hai … Chủ nhật).
 */
public final class AlarmBook {
    public static final int MAX_ALARMS = 12;
    public static final int MAX_SOUNDS = 10;
    public static final long MAX_SOUND_BYTES = 15L * 1024 * 1024;
    public static final String DEFAULT_SOUND = "default";
    public static final String SOUND_DIR = "alarm_sounds";
    /** An alarm delivered further than this from its minute is not rung (clock jumps, see isDue). */
    public static final long DUE_WINDOW_MS = 3 * 60 * 1000L;
    private static final String[] AUDIO_EXT = {".mp3", ".m4a", ".aac", ".ogg", ".wav", ".flac"};

    private AlarmBook() {}

    /** One alarm. Immutable; edits make a new one. */
    public static final class Alarm {
        public final String id;
        public final boolean enabled;
        public final int hour, minute, days;
        public final String sound, label;

        public Alarm(String id, boolean enabled, int hour, int minute, int days, String sound, String label) {
            this.id = id;
            this.enabled = enabled;
            this.hour = hour;
            this.minute = minute;
            this.days = days & 0x7f;
            this.sound = sound == null || sound.isEmpty() ? DEFAULT_SOUND : sound;
            this.label = label == null ? "" : label;
        }

        public boolean once() {
            return days == 0;
        }

        public Alarm withEnabled(boolean on) {
            return new Alarm(id, on, hour, minute, days, sound, label);
        }

        public String time() {
            return String.format(Locale.US, "%02d:%02d", hour, minute);
        }

        JSONObject toJson() throws org.json.JSONException {
            JSONArray d = new JSONArray();
            for (int i = 0; i < 7; i++) if ((days & (1 << i)) != 0) d.put(i);
            return new JSONObject().put("id", id).put("enabled", enabled).put("time", time())
                    .put("days", d).put("sound", sound).put("label", label);
        }
    }

    // ── JSON ────────────────────────────────────────────────────────────────────────

    /** The stored list; an unreadable store is an empty list, never a crash at boot. */
    public static List<Alarm> parse(String json) {
        List<Alarm> out = new ArrayList<Alarm>();
        if (json == null || json.trim().isEmpty()) return out;
        try {
            JSONArray a = new JSONArray(json);
            for (int i = 0; i < a.length() && out.size() < MAX_ALARMS; i++) {
                Alarm alarm = fromJson(a.optJSONObject(i), "a" + i);
                if (alarm != null) out.add(alarm);
            }
        } catch (Exception e) {
            // keep what was read
        }
        return out;
    }

    /**
     * What the panel sends, checked: a time that reads as HH:mm, weekday numbers 0..6, a sound
     * that is the default or a plain file name, a label of reasonable length.
     *
     * @return the list, or null with {@code error[0]} saying what was wrong
     */
    public static List<Alarm> validate(JSONArray in, String[] error) {
        List<Alarm> out = new ArrayList<Alarm>();
        if (in == null) {
            error[0] = "Thiếu danh sách báo thức.";
            return null;
        }
        if (in.length() > MAX_ALARMS) {
            error[0] = "Tối đa " + MAX_ALARMS + " báo thức.";
            return null;
        }
        List<String> seen = new ArrayList<String>();
        for (int i = 0; i < in.length(); i++) {
            JSONObject o = in.optJSONObject(i);
            Alarm a = fromJson(o, "a" + System.currentTimeMillis() + i);
            if (a == null) {
                error[0] = "Giờ báo thức thứ " + (i + 1) + " không hợp lệ.";
                return null;
            }
            if (!DEFAULT_SOUND.equals(a.sound) && !safeName(a.sound)) {
                error[0] = "Tên tệp âm thanh không hợp lệ.";
                return null;
            }
            String id = a.id;
            while (seen.contains(id)) id = id + "x";
            seen.add(id);
            out.add(id.equals(a.id) ? a : new Alarm(id, a.enabled, a.hour, a.minute, a.days, a.sound, a.label));
        }
        return out;
    }

    static Alarm fromJson(JSONObject o, String fallbackId) {
        if (o == null) return null;
        int[] hm = parseTime(o.optString("time", ""));
        if (hm == null) return null;
        int days = 0;
        JSONArray d = o.optJSONArray("days");
        for (int i = 0; d != null && i < d.length(); i++) {
            int day = d.optInt(i, -1);
            if (day >= 0 && day <= 6) days |= 1 << day;
        }
        String id = o.optString("id", "").replaceAll("[^A-Za-z0-9_-]", "");
        if (id.isEmpty() || id.length() > 40) id = fallbackId;
        String label = o.optString("label", "").trim();
        if (label.length() > 40) label = label.substring(0, 40);
        return new Alarm(id, o.optBoolean("enabled", true), hm[0], hm[1], days, o.optString("sound", DEFAULT_SOUND), label);
    }

    public static String toJson(List<Alarm> alarms) {
        JSONArray a = new JSONArray();
        try {
            for (Alarm x : alarms) a.put(x.toJson());
        } catch (Exception ignored) {
            // cannot happen with these field types
        }
        return a.toString();
    }

    public static JSONArray toJsonArray(List<Alarm> alarms) {
        try {
            return new JSONArray(toJson(alarms));
        } catch (Exception e) {
            return new JSONArray();
        }
    }

    /** "06:30" → {6, 30}; anything else → null. */
    public static int[] parseTime(String s) {
        if (s == null) return null;
        String[] p = s.trim().split(":");
        if (p.length != 2) return null;
        try {
            int h = Integer.parseInt(p[0].trim()), m = Integer.parseInt(p[1].trim());
            return h >= 0 && h <= 23 && m >= 0 && m <= 59 ? new int[]{h, m} : null;
        } catch (NumberFormatException e) {
            return null;
        }
    }

    // ── When ────────────────────────────────────────────────────────────────────────

    /** Monday = 0 … Sunday = 6, from a Calendar's day of week. */
    static int weekday(Calendar c) {
        return (c.get(Calendar.DAY_OF_WEEK) + 5) % 7;
    }

    /** The next instant (ms) this alarm rings strictly after {@code now}, or -1 if it never will. */
    public static long nextFor(Alarm a, long now, TimeZone zone) {
        if (!a.enabled) return -1;
        Calendar c = Calendar.getInstance(zone);
        c.setTimeInMillis(now);
        c.set(Calendar.HOUR_OF_DAY, a.hour);
        c.set(Calendar.MINUTE, a.minute);
        c.set(Calendar.SECOND, 0);
        c.set(Calendar.MILLISECOND, 0);
        for (int i = 0; i < 8; i++) {
            if (c.getTimeInMillis() > now && (a.once() || (a.days & (1 << weekday(c))) != 0)) {
                return c.getTimeInMillis();
            }
            c.add(Calendar.DAY_OF_YEAR, 1);
            // re-pin the wall-clock time: a daylight-saving day would otherwise shift it
            c.set(Calendar.HOUR_OF_DAY, a.hour);
            c.set(Calendar.MINUTE, a.minute);
        }
        return -1;
    }

    /** The alarm that rings first after {@code now}, with its time; null when none is on. */
    public static Object[] next(List<Alarm> alarms, long now, TimeZone zone) {
        Alarm best = null;
        long at = Long.MAX_VALUE;
        for (Alarm a : alarms) {
            long t = nextFor(a, now, zone);
            if (t > 0 && t < at) {
                at = t;
                best = a;
            }
        }
        return best == null ? null : new Object[]{best, at};
    }

    /** Every enabled alarm whose minute is {@code at} (several alarms can share a minute). */
    public static List<Alarm> dueAt(List<Alarm> alarms, long at, TimeZone zone) {
        List<Alarm> out = new ArrayList<Alarm>();
        for (Alarm a : alarms) {
            if (nextFor(a, at - 1, zone) == at) out.add(a);
        }
        return out;
    }

    /**
     * Is an alarm scheduled for {@code scheduled} really due at {@code now}? AlarmManager delivers
     * a stale alarm immediately: the R1 boots on the ROM's 2018 clock, and when NTP corrects it an
     * alarm derived before that would ring at once. Only ring within a few minutes of the minute.
     */
    public static boolean isDue(long scheduled, long now) {
        return Math.abs(now - scheduled) <= DUE_WINDOW_MS;
    }

    /** Once-only alarms that have just rung switch themselves off. */
    public static List<Alarm> afterRinging(List<Alarm> alarms, List<Alarm> rang) {
        List<Alarm> out = new ArrayList<Alarm>();
        for (Alarm a : alarms) {
            boolean done = false;
            for (Alarm r : rang) if (r.id.equals(a.id) && a.once()) done = true;
            out.add(done ? a.withEnabled(false) : a);
        }
        return out;
    }

    private static final String[] DAY_SHORT = {"T2", "T3", "T4", "T5", "T6", "T7", "CN"};
    private static final String[] DAY_LONG = {"thứ Hai", "thứ Ba", "thứ Tư", "thứ Năm", "thứ Sáu", "thứ Bảy", "Chủ nhật"};

    /** "Hằng ngày", "T2–T6", "T7, CN", "Một lần" … for the panel. */
    public static String daysText(int days) {
        days &= 0x7f;
        if (days == 0) return "Một lần";
        if (days == 0x7f) return "Hằng ngày";
        if (days == 0x1f) return "T2–T6";
        if (days == 0x60) return "T7, CN";
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < 7; i++) {
            if ((days & (1 << i)) != 0) {
                if (b.length() > 0) b.append(", ");
                b.append(DAY_SHORT[i]);
            }
        }
        return b.toString();
    }

    /** "06:30 thứ Hai, 19/10" */
    public static String whenText(long at, TimeZone zone) {
        Calendar c = Calendar.getInstance(zone);
        c.setTimeInMillis(at);
        return String.format(Locale.US, "%02d:%02d %s, %d/%d", c.get(Calendar.HOUR_OF_DAY), c.get(Calendar.MINUTE),
                DAY_LONG[weekday(c)], c.get(Calendar.DAY_OF_MONTH), c.get(Calendar.MONTH) + 1);
    }

    // ── Sounds ──────────────────────────────────────────────────────────────────────

    /** A file name safe to keep in the sound folder: no path, a known audio extension. */
    public static boolean safeName(String name) {
        if (name == null || name.isEmpty() || name.length() > 80) return false;
        if (name.contains("/") || name.contains("\\") || name.startsWith(".") || name.contains("..")) return false;
        String low = name.toLowerCase(Locale.US);
        for (String ext : AUDIO_EXT) if (low.endsWith(ext)) return true;
        return false;
    }

    /** An uploaded file's name made safe: accents and odd characters out, extension kept. */
    public static String cleanName(String raw) {
        if (raw == null) return "";
        String n = raw.replace('\\', '/');
        n = n.substring(n.lastIndexOf('/') + 1);
        n = java.text.Normalizer.normalize(n, java.text.Normalizer.Form.NFD).replaceAll("\\p{M}", "")
                .replace('đ', 'd').replace('Đ', 'D');
        n = n.replaceAll("[^A-Za-z0-9._ -]", "").replaceAll("\\s+", "_").replaceAll("^[._]+", "");
        if (n.length() > 80) {
            int dot = n.lastIndexOf('.');
            String ext = dot > 0 ? n.substring(dot) : "";
            n = n.substring(0, 80 - ext.length()) + ext;
        }
        return safeName(n) ? n : "";
    }

    public static File soundDir(File filesDir) {
        return new File(filesDir, SOUND_DIR);
    }

    /** The uploaded sounds, by name. */
    public static List<String> sounds(File filesDir) {
        File[] files = soundDir(filesDir).listFiles();
        List<String> out = new ArrayList<String>();
        if (files == null) return out;
        for (File f : files) if (f.isFile() && safeName(f.getName())) out.add(f.getName());
        Collections.sort(out, String.CASE_INSENSITIVE_ORDER);
        return out;
    }

    /** The file an alarm's sound names, or null for the default tone or a file that is gone. */
    public static File soundFile(File filesDir, String sound) {
        if (sound == null || DEFAULT_SOUND.equals(sound) || !safeName(sound)) return null;
        File f = new File(soundDir(filesDir), sound);
        return f.isFile() ? f : null;
    }

    /** Keep an upload (a temp file NanoHTTPD wrote) under {@code name}. Returns "" or an error. */
    public static String saveSound(File filesDir, String tmpPath, String name) {
        String clean = cleanName(name);
        if (clean.isEmpty()) return "Chỉ nhận tệp MP3, M4A, AAC, OGG, WAV hoặc FLAC.";
        if (tmpPath == null || tmpPath.isEmpty()) return "Không nhận được tệp, hoặc tệp quá lớn (tối đa " + MAX_SOUND_BYTES / (1024 * 1024) + " MB).";
        File src = new File(tmpPath);
        if (src.length() < 64) return "Tệp âm thanh rỗng.";
        if (src.length() > MAX_SOUND_BYTES) return "Tệp quá lớn (tối đa " + MAX_SOUND_BYTES / (1024 * 1024) + " MB).";
        File dir = soundDir(filesDir);
        if (!dir.isDirectory() && !dir.mkdirs()) return "Loa không tạo được thư mục âm thanh.";
        List<String> have = sounds(filesDir);
        if (!have.contains(clean) && have.size() >= MAX_SOUNDS) return "Tối đa " + MAX_SOUNDS + " tệp âm thanh — xoá bớt trước.";
        File tmp = new File(dir, clean + ".part");
        FileInputStream in = null;
        FileOutputStream out = null;
        try {
            in = new FileInputStream(src);
            out = new FileOutputStream(tmp);
            byte[] buf = new byte[32 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            out.close();
            out = null;
            File dst = new File(dir, clean);
            if (dst.exists() && !dst.delete()) return "Loa không thay được tệp cũ.";
            if (!tmp.renameTo(dst)) return "Loa không lưu được tệp.";
            return "";
        } catch (Exception e) {
            return "Loa không lưu được tệp: " + e.getMessage();
        } finally {
            try { if (in != null) in.close(); } catch (Exception ignored) { }
            try { if (out != null) out.close(); } catch (Exception ignored) { }
            if (tmp.exists()) tmp.delete();
        }
    }

    public static boolean deleteSound(File filesDir, String name) {
        File f = soundFile(filesDir, name);
        return f != null && f.delete();
    }

    /** The default days for a new alarm in the panel: Monday to Friday. */
    public static final List<Integer> WEEKDAYS = Collections.unmodifiableList(Arrays.asList(0, 1, 2, 3, 4));
}
