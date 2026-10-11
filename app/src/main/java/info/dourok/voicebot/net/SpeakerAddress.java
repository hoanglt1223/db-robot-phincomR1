package info.dourok.voicebot.net;

import org.json.JSONObject;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Enumeration;
import java.util.List;

/**
 * The speaker's own address on the home network, and how to say it out loud.
 *
 * The control panel is reached at http://&lt;address&gt;:8088, and a speaker has no screen to show
 * that on. So the address can be asked for by voice (a tool the assistant calls, see
 * mcp/DeviceTools.kt), and the speaker reads it out by itself when it has changed (see
 * VoiceAssistant.announceAddress): a router handing out a different address must not leave the
 * owner locked out of the panel.
 *
 * Everything said aloud here is produced by the server's text-to-speech, which cannot be trusted
 * to read "192.168.1.22" the way a person would dictate it. The address is therefore spelled out
 * digit by digit in words before it leaves the speaker.
 */
public final class SpeakerAddress {

    public static final int PANEL_PORT = 8088;
    /** The address the speaker has while it broadcasts its setup network (see WifiSetup). */
    public static final String SETUP_IP = "192.168.43.1";

    private static final String[] DIGITS = {
            "không", "một", "hai", "ba", "bốn", "năm", "sáu", "bảy", "tám", "chín"};

    private SpeakerAddress() {}

    /** One address a network interface has; what {@link #pick} chooses between. */
    static final class Candidate {
        final String name, ip;

        Candidate(String name, String ip) {
            this.name = name;
            this.ip = ip;
        }
    }

    /** The speaker's IPv4 address now, or "" when it is on no network. */
    public static String current() {
        List<Candidate> found = new ArrayList<Candidate>();
        try {
            Enumeration<NetworkInterface> all = NetworkInterface.getNetworkInterfaces();
            if (all == null) return "";
            for (NetworkInterface n : Collections.list(all)) {
                try {
                    if (!n.isUp() || n.isLoopback()) continue;
                    for (InetAddress a : Collections.list(n.getInetAddresses())) {
                        if (a instanceof Inet4Address && !a.isLoopbackAddress()) {
                            found.add(new Candidate(n.getName(), a.getHostAddress()));
                        }
                    }
                } catch (Exception ignored) {
                    // one interface that will not answer says nothing about the others
                }
            }
        } catch (Exception e) {
            return "";
        }
        return pick(found);
    }

    /** Wi-Fi before a cable before anything else; never a self-assigned 169.254 address. */
    static String pick(List<Candidate> found) {
        String best = "";
        int bestRank = Integer.MAX_VALUE;
        for (Candidate c : found) {
            if (c.ip == null || !isAddress(c.ip) || c.ip.startsWith("169.254.") || c.ip.startsWith("127.")) continue;
            String name = c.name == null ? "" : c.name;
            int rank = name.startsWith("wlan") ? 0 : name.startsWith("eth") ? 1 : 2;
            if (rank < bestRank) {
                bestRank = rank;
                best = c.ip;
            }
        }
        return best;
    }

    /** Four numbers 0..255 with dots between them, and nothing else. */
    public static boolean isAddress(String ip) {
        if (ip == null) return false;
        String[] parts = ip.split("\\.", -1);
        if (parts.length != 4) return false;
        for (String p : parts) {
            if (p.isEmpty() || p.length() > 3) return false;
            for (int i = 0; i < p.length(); i++) {
                if (p.charAt(i) < '0' || p.charAt(i) > '9') return false;
            }
            if (Integer.parseInt(p) > 255) return false;
        }
        return true;
    }

    public static String panelUrl(String ip) {
        return "http://" + ip + ":" + PANEL_PORT;
    }

    /**
     * Is {@code ip} worth reading out unasked? Not when there is none, not in setup mode (no
     * server can be reached from there to do the reading), and not when it is the address that
     * was read out last time.
     */
    public static boolean worthAnnouncing(String ip, String lastAnnounced) {
        return isAddress(ip) && !SETUP_IP.equals(ip) && !ip.equals(lastAnnounced);
    }

    /** Late evening and night: an address that changes at 3 a.m. is read out in the morning. */
    public static boolean quietHour(int hourOfDay) {
        return hourOfDay >= 22 || hourOfDay < 7;
    }

    /**
     * The address as it is dictated: every digit by its name, "chấm" for a dot, and a comma after
     * each group so the voice pauses there. "192.168.1.22" becomes
     * "một chín hai, chấm, một sáu tám, chấm, một, chấm, hai hai".
     */
    public static String spoken(String ip) {
        if (!isAddress(ip)) return "";
        StringBuilder out = new StringBuilder();
        String[] parts = ip.split("\\.");
        for (int p = 0; p < parts.length; p++) {
            if (p > 0) out.append(", chấm, ");
            out.append(digits(parts[p]));
        }
        return out.toString();
    }

    private static String digits(String number) {
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < number.length(); i++) {
            if (i > 0) out.append(' ');
            out.append(DIGITS[number.charAt(i) - '0']);
        }
        return out.toString();
    }

    /** What the owner should hear, as one piece of text ready for a voice to read. */
    public static String sentence(String ip) {
        String said = spoken(ip);
        if (said.isEmpty()) {
            return "Loa chưa vào mạng Wi-Fi nào nên chưa có địa chỉ IP.";
        }
        return "Địa chỉ IP của loa là: " + said + ". "
                + "Để mở trang điều khiển, hãy vào trình duyệt trên điện thoại hoặc máy tính cùng mạng Wi-Fi, "
                + "gõ địa chỉ đó, thêm dấu hai chấm và số " + digits(String.valueOf(PANEL_PORT)) + ".";
    }

    /**
     * The request sent to the assistant when the speaker reads its address out unasked. It goes
     * the way a typed chat message does, so it is worded as an instruction to repeat, with the
     * text to say already written out.
     */
    public static String announceRequest(String ip) {
        return "Đây là thông báo tự động của loa, không phải câu hỏi của người dùng. "
                + "Hãy nói với người dùng đúng nội dung sau, giữ nguyên từng chữ số, không thêm bớt, không gọi công cụ nào: "
                + "\"Loa đã kết nối mạng. " + sentence(ip) + "\"";
    }

    /** The reply of the assistant's tool, see mcp/DeviceTools.kt. */
    public static String toolReply(String ip) {
        try {
            JSONObject o = new JSONObject();
            boolean has = isAddress(ip);
            o.put("success", has);
            if (has) {
                o.put("ip", ip);
                o.put("control_panel", panelUrl(ip));
                o.put("setup_mode", SETUP_IP.equals(ip));
            }
            o.put("speak", sentence(ip));
            return o.toString();
        } catch (Exception e) {
            return "{\"success\":false}";
        }
    }
}
