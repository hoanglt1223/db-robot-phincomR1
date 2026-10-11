package info.dourok.voicebot.control;

import android.util.Base64;

import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;

/**
 * What is behind the panel's chat. It is kept on the speaker rather than in one browser, so the
 * phone it was chosen on and every other device in the house show the same thing.
 *
 * There are four kinds, named by a "style" string:
 * <ul>
 * <li>{@code none} -- nothing;</li>
 * <li>{@code holo:<id>} -- one of the animated backdrops the panel draws itself (control.html);
 *     only the name is stored here;</li>
 * <li>{@code file} -- a picture the owner uploaded, stored beside the app's other files;</li>
 * <li>{@code url:<address>} -- a picture on the Internet. Only the address is stored: each
 *     browser fetches the picture itself, the speaker never does.</li>
 * </ul>
 *
 * A still picture arrives already shrunk by the panel, as base64 ({@link #save}); an animated one
 * (GIF, WebP) cannot be redrawn without losing its motion, so it arrives as it is, as a file the
 * web server has already written to disk ({@link #saveFile}). Either way this only checks that it
 * is a picture and of a sane size, and stores it.
 */
public final class ChatBackground {

    /** Decoded size limit for a still. The panel sends far less; this stops anything else filling the disk. */
    static final int MAX_BYTES = 3 * 1024 * 1024;
    /** Size limit for a picture sent as it is (an animated GIF runs to megabytes). */
    public static final int MAX_RAW_BYTES = 8 * 1024 * 1024;
    private static final int MAX_URL = 600;
    private static final String NAME = "chat_bg.img";
    private static final String STYLE_NAME = "chat_bg.style";

    private ChatBackground() {}

    public static File file(File dir) {
        return new File(dir, NAME);
    }

    /** Changes whenever the picture does; 0 when there is none. The panel uses it as a cache key. */
    public static long stamp(File dir) {
        File f = file(dir);
        return f.isFile() ? f.lastModified() : 0;
    }

    /** "image/jpeg", "image/png", "image/webp" or "image/gif", from the stored file's first bytes. */
    public static String mime(File f) {
        byte[] head = new byte[12];
        FileInputStream in = null;
        try {
            in = new FileInputStream(f);
            int n = in.read(head);
            return n < 12 ? "" : mimeOf(head);
        } catch (Exception e) {
            return "";
        } finally {
            if (in != null) try { in.close(); } catch (Exception ignored) { }
        }
    }

    /**
     * @param body the picture as base64, with or without a {@code data:image/…;base64,} prefix
     * @return JSON for the panel: {@code {"ok":true,"stamp":…}} or {@code {"ok":false,"error":…}}
     */
    public static String save(File dir, String body) {
        if (body == null || body.isEmpty()) return error("Không nhận được ảnh.");
        String b64 = body;
        int comma = body.indexOf(',');
        if (body.startsWith("data:") && comma > 0) b64 = body.substring(comma + 1);
        if (b64.length() > MAX_BYTES / 3 * 4 + 8) return error("Ảnh quá lớn.");
        byte[] bytes;
        try {
            bytes = Base64.decode(b64, Base64.DEFAULT);
        } catch (IllegalArgumentException e) {
            return error("Dữ liệu ảnh không hợp lệ.");
        }
        if (bytes.length < 12 || mimeOf(bytes).isEmpty()) return error("Tệp này không phải ảnh JPEG, PNG, WebP hay GIF.");
        if (bytes.length > MAX_BYTES) return error("Ảnh quá lớn.");

        // Written beside the real file and renamed over it, so a browser fetching the picture
        // at this moment gets the old one or the new one, never half of each.
        File tmp = new File(dir, NAME + ".tmp");
        FileOutputStream out = null;
        try {
            out = new FileOutputStream(tmp);
            out.write(bytes);
            out.close();
            out = null;
            File dst = file(dir);
            if (!tmp.renameTo(dst)) {
                dst.delete();
                if (!tmp.renameTo(dst)) return error("Loa không lưu được ảnh.");
            }
            return stored(dir);
        } catch (Exception e) {
            tmp.delete();
            return error("Loa không lưu được ảnh: " + e.getMessage());
        } finally {
            if (out != null) try { out.close(); } catch (Exception ignored) { }
        }
    }

    /**
     * Store a picture the web server has already received into a file of its own (a PUT body).
     *
     * @param path that file, which the server deletes when the request ends; "" when the upload
     *             was refused before it was read (too large, or nothing sent)
     */
    public static String saveFile(File dir, String path) {
        String tooBig = "Ảnh quá lớn (tối đa " + MAX_RAW_BYTES / (1024 * 1024) + " MB).";
        if (path == null || path.isEmpty()) return error("Không nhận được ảnh, hoặc ảnh quá lớn (tối đa "
                + MAX_RAW_BYTES / (1024 * 1024) + " MB).");
        File src = new File(path);
        long size = src.length();
        if (size < 12) return error("Không nhận được ảnh.");
        if (size > MAX_RAW_BYTES) return error(tooBig);

        File tmp = new File(dir, NAME + ".tmp");
        FileInputStream in = null;
        FileOutputStream out = null;
        try {
            in = new FileInputStream(src);
            out = new FileOutputStream(tmp);
            byte[] buf = new byte[16 * 1024];
            boolean first = true;
            int n;
            while ((n = in.read(buf)) > 0) {
                if (first) {
                    if (n < 12 || mimeOf(buf).isEmpty()) {
                        out.close();
                        out = null;
                        tmp.delete();
                        return error("Tệp này không phải ảnh JPEG, PNG, WebP hay GIF.");
                    }
                    first = false;
                }
                out.write(buf, 0, n);
            }
            out.close();
            out = null;
            File dst = file(dir);
            if (!tmp.renameTo(dst)) {
                dst.delete();
                if (!tmp.renameTo(dst)) return error("Loa không lưu được ảnh.");
            }
            return stored(dir);
        } catch (Exception e) {
            tmp.delete();
            return error("Loa không lưu được ảnh: " + e.getMessage());
        } finally {
            if (in != null) try { in.close(); } catch (Exception ignored) { }
            if (out != null) try { out.close(); } catch (Exception ignored) { }
        }
    }

    /** A picture has just been stored: it is what the chat shows from now on. */
    private static String stored(File dir) throws Exception {
        writeStyle(dir, "file");
        return new JSONObject().put("ok", true).put("stamp", stamp(dir)).put("style", "file").toString();
    }

    /** Delete the uploaded picture. A backdrop of another kind in use stays in use. */
    public static String clear(File dir) {
        File f = file(dir);
        if (f.exists() && !f.delete()) return error("Loa không xoá được ảnh.");
        if ("file".equals(readStyle(dir))) writeStyle(dir, "none");
        try {
            return new JSONObject().put("ok", true).put("stamp", 0).put("style", style(dir)).toString();
        } catch (Exception e) {
            return "{\"ok\":true,\"stamp\":0}";
        }
    }

    // ── Which kind is in use ─────────────────────────────────────────────────────────────────

    /**
     * The style in use, always one the panel can show: a speaker updated from a version that had
     * only the uploaded picture has no style stored and shows that picture; a stored "file" whose
     * picture is gone shows nothing.
     */
    public static String style(File dir) {
        String s = clean(readStyle(dir));
        boolean picture = file(dir).isFile();
        if (s.isEmpty()) return picture ? "file" : "none";
        if ("file".equals(s) && !picture) return "none";
        return s;
    }

    /** Body: {@code {"style":"none" | "file" | "holo:<id>" | "url:<http address>"}}. */
    public static String setStyle(File dir, String body) {
        String wanted;
        try {
            wanted = new JSONObject(body == null ? "" : body).optString("style", "");
        } catch (Exception e) {
            return error("Yêu cầu không hợp lệ.");
        }
        String s = clean(wanted);
        if (s.isEmpty()) {
            return error(wanted.startsWith("url:")
                    ? "Link ảnh không hợp lệ: cần bắt đầu bằng http:// hoặc https://, không có khoảng trắng."
                    : "Kiểu nền không hợp lệ.");
        }
        if ("file".equals(s) && !file(dir).isFile()) return error("Loa chưa có ảnh nào được tải lên.");
        if (!writeStyle(dir, s)) return error("Loa không lưu được lựa chọn này.");
        try {
            return new JSONObject().put("ok", true).put("style", s).put("stamp", stamp(dir)).toString();
        } catch (Exception e) {
            return "{\"ok\":true}";
        }
    }

    /**
     * {@code wanted} if it is a style, otherwise "". The address of a "url:" style ends up inside
     * a CSS {@code url("…")} in every browser that opens the panel, so nothing that could end
     * that string or start another is let through.
     */
    static String clean(String wanted) {
        if (wanted == null) return "";
        String s = wanted.trim();
        if ("none".equals(s) || "file".equals(s)) return s;
        if (s.startsWith("holo:")) {
            String id = s.substring(5);
            if (id.isEmpty() || id.length() > 16) return "";
            for (int i = 0; i < id.length(); i++) {
                char c = id.charAt(i);
                if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9')) return "";
            }
            return s;
        }
        if (s.startsWith("url:")) {
            String u = s.substring(4);
            String low = u.toLowerCase(java.util.Locale.ROOT);
            if (!low.startsWith("http://") && !low.startsWith("https://")) return "";
            if (u.length() > MAX_URL || u.length() < 12) return "";
            for (int i = 0; i < u.length(); i++) {
                char c = u.charAt(i);
                if (c <= ' ' || c == 0x7f || c == '"' || c == '\'' || c == '\\' || c == '<' || c == '>'
                        || c == '(' || c == ')' || c == '`' || c == '{' || c == '}') return "";
            }
            return s;
        }
        return "";
    }

    private static String readStyle(File dir) {
        File f = new File(dir, STYLE_NAME);
        if (!f.isFile() || f.length() > MAX_URL + 16) return "";
        FileInputStream in = null;
        try {
            in = new FileInputStream(f);
            byte[] buf = new byte[(int) f.length()];
            int got = 0, n;
            while (got < buf.length && (n = in.read(buf, got, buf.length - got)) > 0) got += n;
            return new String(buf, 0, got, "UTF-8");
        } catch (Exception e) {
            return "";
        } finally {
            if (in != null) try { in.close(); } catch (Exception ignored) { }
        }
    }

    private static boolean writeStyle(File dir, String style) {
        FileOutputStream out = null;
        try {
            out = new FileOutputStream(new File(dir, STYLE_NAME));
            out.write(style.getBytes("UTF-8"));
            return true;
        } catch (Exception e) {
            return false;
        } finally {
            if (out != null) try { out.close(); } catch (Exception ignored) { }
        }
    }

    /** The type the first bytes say this is, or "" when it is none of the four accepted. */
    static String mimeOf(byte[] b) {
        if ((b[0] & 0xff) == 0xFF && (b[1] & 0xff) == 0xD8 && (b[2] & 0xff) == 0xFF) return "image/jpeg";
        if ((b[0] & 0xff) == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') return "image/png";
        if (b[0] == 'R' && b[1] == 'I' && b[2] == 'F' && b[3] == 'F'
                && b[8] == 'W' && b[9] == 'E' && b[10] == 'B' && b[11] == 'P') return "image/webp";
        if (b[0] == 'G' && b[1] == 'I' && b[2] == 'F' && b[3] == '8' && (b[4] == '7' || b[4] == '9') && b[5] == 'a') return "image/gif";
        return "";
    }

    private static String error(String message) {
        try {
            return new JSONObject().put("ok", false).put("error", message).toString();
        } catch (Exception e) {
            return "{\"ok\":false}";
        }
    }
}
