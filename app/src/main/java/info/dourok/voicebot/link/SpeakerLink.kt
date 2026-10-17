package info.dourok.voicebot.link

import android.content.Context
import android.util.Log
import info.dourok.voicebot.alarm.AlarmBook
import info.dourok.voicebot.alarm.AlarmRinger
import info.dourok.voicebot.alarm.AlarmScheduler
import info.dourok.voicebot.data.Settings
import info.dourok.voicebot.domain.voice.AppLog
import info.dourok.voicebot.domain.voice.ConversationLog
import info.dourok.voicebot.domain.voice.VoiceCommands
import info.dourok.voicebot.domain.voice.VoiceDebugState
import info.dourok.voicebot.domain.voice.VoiceGate
import info.dourok.voicebot.mcp.DeviceActions
import info.dourok.voicebot.media.LocalMusicPlayer
import info.dourok.voicebot.net.SpeakerAddress
import info.dourok.voicebot.weather.LocationManager
import org.json.JSONArray
import org.json.JSONObject
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.util.TimeZone

/**
 * The speaker's small UDP service on port [PORT], for things on the home network that are not a
 * web browser: other DB-Robot speakers looking for each other (GroupSync), and a display -- an
 * ESP32-S3 with a screen -- that shows what this screenless speaker is doing.
 *
 * Text messages are JSON, one per datagram:
 *  - `{"t":"find"}` (usually broadcast) → this speaker answers `{"t":"here",id,name,ip,ver,...}`.
 *  - `{"t":"sub","name":"…"}` → the sender gets this speaker's state (`{"t":"st",…}`) whenever it
 *    changes and every few seconds anyway, plus the music spectrum while a song plays. A display
 *    re-sends `sub` every few seconds; one that stops is forgotten after [LEASE_MS].
 *  - `{"t":"bye"}` → forget the sender now.
 *  - `{"t":"cmd","c":"wake"|"sleep"|"pause"|"resume"|"next"|"stop"|"vol+"|"vol-"|"alarm_stop"}`
 *    → the display's touch buttons.
 * The spectrum goes as binary: 'S','P', then [BANDS] bytes 0..255, low frequencies first.
 *
 * Nothing here is secret or harmful to read; the commands are what the control panel, open to
 * the same network, can already do.
 */
object SpeakerLink {
    const val PORT = 8089
    private const val TAG = "SpeakerLink"
    private const val LEASE_MS = 20_000L
    private const val STATE_EVERY_MS = 200L
    private const val KEEPALIVE_MS = 3_000L
    private const val SPECTRUM_EVERY_MS = 66L
    const val BANDS = 32
    private const val TEXT_MAX = 280

    private class Sub(val addr: InetSocketAddress, var name: String, var seen: Long)

    private val subs = LinkedHashMap<String, Sub>()
    @Volatile private var socket: DatagramSocket? = null
    @Volatile private var deviceId = ""
    @Volatile private var version = ""

    /** The displays currently subscribed, for the panel: [{name, ip}]. */
    fun displays(): JSONArray {
        val a = JSONArray()
        synchronized(subs) {
            prune()
            subs.values.forEach { a.put(JSONObject().put("name", it.name).put("ip", it.addr.address.hostAddress)) }
        }
        return a
    }

    fun start(context: Context, id: String, appVersion: String) {
        if (socket != null) return
        deviceId = id
        version = appVersion
        try {
            val s = DatagramSocket(null)
            s.reuseAddress = true
            s.broadcast = true
            s.bind(InetSocketAddress(PORT))
            socket = s
        } catch (e: Exception) {
            Log.e(TAG, "cannot bind :$PORT: ${e.message}")
            return
        }
        Thread({ receiveLoop() }, "link-rx").apply { isDaemon = true }.start()
        Thread({ pushLoop() }, "link-tx").apply { isDaemon = true }.start()
        Thread({ spectrumLoop() }, "link-spec").apply { isDaemon = true }.start()
    }

    /** This speaker as the others see it. */
    fun hereJson(): JSONObject {
        val np = LocalMusicPlayer.nowPlayingBrief()
        return JSONObject()
            .put("t", "here")
            .put("id", deviceId)
            .put("name", Settings.speakerName)
            .put("ip", SpeakerAddress.current())
            .put("ver", version)
            .put("model", "Phicomm R1")
            .put("vol", DeviceActions.getVolume?.invoke() ?: -1)
            .put("playing", if (np != null && np[0] == "playing") np[1] else "")
            .put("leader", GroupSync.isLeader())
    }

    // ── receive ────────────────────────────────────────────────────────────────────

    private fun receiveLoop() {
        val s = socket ?: return
        val buf = ByteArray(1500)
        while (true) {
            try {
                val pk = DatagramPacket(buf, buf.size)
                s.receive(pk)
                val text = String(pk.data, pk.offset, pk.length, Charsets.UTF_8)
                val msg = try { JSONObject(text) } catch (e: Exception) { continue }
                val from = InetSocketAddress(pk.address, pk.port)
                when (msg.optString("t")) {
                    "find" -> send(from, hereJson().toString())
                    "sub" -> {
                        val key = from.toString()
                        val fresh: Boolean
                        synchronized(subs) {
                            val have = subs[key]
                            fresh = have == null
                            if (have == null) subs[key] = Sub(from, msg.optString("name", "Màn hình").take(32), now())
                            else { have.seen = now(); have.name = msg.optString("name", have.name).take(32) }
                        }
                        if (fresh) {
                            lastState = ""   // everyone gets the full state right away
                            info.dourok.voicebot.domain.voice.AppLog.i("Màn hình phụ đã nối: ${pk.address.hostAddress}")
                        }
                    }
                    "bye" -> synchronized(subs) { subs.remove(from.toString()) }
                    "cmd" -> command(msg.optString("c"))
                }
            } catch (e: Exception) {
                if (socket == null) return
                try { Thread.sleep(200) } catch (_: InterruptedException) { return }
            }
        }
    }

    private fun command(c: String) {
        when (c) {
            "wake" -> VoiceCommands.flow.tryEmit(VoiceCommands.Command.WAKE)
            "sleep" -> VoiceCommands.flow.tryEmit(VoiceCommands.Command.SLEEP)
            // "Tắt nghe" / "Bật nghe" on the display: the same switch as the one on the control panel
            "mute" -> { VoiceGate.paused = true; AppLog.i("Tắt nghe (từ màn hình phụ)") }
            "unmute" -> { VoiceGate.paused = false; AppLog.i("Nghe lại từ đánh thức (từ màn hình phụ)") }
            "pause" -> LocalMusicPlayer.pause()
            "resume" -> LocalMusicPlayer.resume()
            "next" -> LocalMusicPlayer.next()
            "stop" -> LocalMusicPlayer.stop()
            "alarm_stop" -> AlarmRinger.stop("tắt từ màn hình phụ")
            "vol+", "vol-" -> {
                val get = DeviceActions.getVolume ?: return
                val set = DeviceActions.setVolume ?: return
                val v = get() + if (c == "vol+") 7 else -7
                set(v.coerceIn(0, 100))
            }
        }
    }

    // ── state ──────────────────────────────────────────────────────────────────────

    @Volatile private var lastState = ""
    private var lastSentAt = 0L

    private fun pushLoop() {
        while (true) {
            try {
                Thread.sleep(STATE_EVERY_MS)
                val targets = synchronized(subs) { prune(); subs.values.map { it.addr } }
                if (targets.isEmpty()) { lastState = ""; continue }
                val body = state()
                val changed = body != lastState
                if (changed || now() - lastSentAt > KEEPALIVE_MS) {
                    lastState = body
                    lastSentAt = now()
                    // the clock is added on send so it alone never counts as a change
                    val withClock = body.dropLast(1) + ",\"tm\":${System.currentTimeMillis() / 1000}" +
                        ",\"tzo\":${TimeZone.getDefault().getOffset(System.currentTimeMillis()) / 60000}}"
                    targets.forEach { send(it, withClock) }
                }
            } catch (e: InterruptedException) {
                return
            } catch (e: Exception) {
                Log.e(TAG, "push: ${e.message}")
            }
        }
    }

    /** Everything a display shows, kept well under one datagram. */
    fun state(): String {
        val o = JSONObject()
            .put("t", "st")
            .put("name", Settings.speakerName)
            .put("ip", SpeakerAddress.current())
            .put("ver", version)
            .put("vs", VoiceDebugState.voiceState)
            .put("awake", VoiceDebugState.awake)
            .put("emo", VoiceDebugState.emotion)
            .put("mute", VoiceGate.paused)
            .put("vol", DeviceActions.getVolume?.invoke() ?: -1)
        // the last thing said each way: the user's words, and the assistant's reply since then
        val log = ConversationLog.recent()
        val lastUser = log.indexOfLast { it.sender == "user" }
        o.put("user", if (lastUser >= 0) tail(log[lastUser].text) else "")
        val reply = log.drop(lastUser + 1).filter { it.sender != "user" }.joinToString(" ") { it.text }
        o.put("bot", tail(reply))
        LocalMusicPlayer.nowPlayingBrief()?.let { np ->
            val id = np[3] as String
            val src = when {
                id.startsWith("radio:") -> "Radio"
                id.startsWith("pod:") -> "Podcast"
                id.contains("id=yt_") -> "YouTube"
                else -> "Nhạc"
            }
            o.put("m", JSONObject().put("s", np[0]).put("ti", tail(np[1] as String, 120))
                .put("ar", tail(np[2] as String, 80)).put("src", src).put("pos", np[4]).put("dur", np[5]))
        }
        LocationManager.cachedNow()?.let { o.put("wx", it) }
        val al = JSONObject()
        AlarmScheduler.next()?.let { (_, at) -> al.put("next", AlarmBook.whenText(at, TimeZone.getDefault())) }
        AlarmRinger.ringing?.let { r -> if (!r.test) al.put("ring", r.time + if (r.label.isNotEmpty()) " · ${r.label}" else "") }
        if (al.length() > 0) o.put("al", al)
        if (GroupSync.isLeader()) o.put("group", GroupSync.memberCount())
        return o.toString()
    }

    private fun tail(s: String, max: Int = TEXT_MAX): String {
        val t = s.trim()
        if (t.length <= max) return t
        val cut = t.substring(t.length - max)
        val sp = cut.indexOf(' ')
        return "…" + if (sp in 1..30) cut.substring(sp + 1) else cut
    }

    // ── spectrum ───────────────────────────────────────────────────────────────────

    private fun spectrumLoop() {
        val bands = IntArray(BANDS)
        val pk = ByteArray(2 + BANDS)
        pk[0] = 'S'.code.toByte()
        pk[1] = 'P'.code.toByte()
        var silentSent = false
        while (true) {
            try {
                Thread.sleep(SPECTRUM_EVERY_MS)
                val targets = synchronized(subs) { subs.values.map { it.addr } }
                if (targets.isEmpty()) continue
                val live = LocalMusicPlayer.spectrum(bands)
                if (!live) {
                    if (silentSent) continue
                    bands.fill(0)
                    silentSent = true
                } else {
                    silentSent = false
                }
                for (i in 0 until BANDS) pk[2 + i] = bands[i].coerceIn(0, 255).toByte()
                targets.forEach { sendBytes(it, pk) }
            } catch (e: InterruptedException) {
                return
            } catch (e: Exception) {
                // a frame lost is a frame lost
            }
        }
    }

    // ── discovery, for GroupSync ───────────────────────────────────────────────────

    /** Ask the network which DB-Robot speakers are there (not counting this one). */
    fun discover(waitMs: Long = 1500): List<JSONObject> {
        val found = LinkedHashMap<String, JSONObject>()
        try {
            DatagramSocket().use { s ->
                s.broadcast = true
                s.soTimeout = 250
                val ask = """{"t":"find"}""".toByteArray()
                val targets = mutableListOf(InetAddress.getByName("255.255.255.255"))
                // a subnet broadcast as well: some routers drop the all-ones one
                SpeakerAddress.current().takeIf { it.isNotEmpty() }?.let {
                    targets.add(InetAddress.getByName(it.substringBeforeLast('.') + ".255"))
                }
                val end = now() + waitMs
                var nextAsk = 0L
                val buf = ByteArray(1500)
                while (now() < end) {
                    if (now() >= nextAsk) {
                        targets.forEach { t -> try { s.send(DatagramPacket(ask, ask.size, t, PORT)) } catch (_: Exception) { } }
                        nextAsk = now() + 500
                    }
                    try {
                        val pk = DatagramPacket(buf, buf.size)
                        s.receive(pk)
                        val j = JSONObject(String(pk.data, pk.offset, pk.length, Charsets.UTF_8))
                        if (j.optString("t") != "here") continue
                        val id = j.optString("id")
                        if (id.isEmpty() || id == deviceId) continue
                        if (j.optString("ip").isEmpty()) j.put("ip", pk.address.hostAddress)
                        found[id] = j
                    } catch (_: java.net.SocketTimeoutException) {
                    } catch (_: Exception) {
                    }
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "discover: ${e.message}")
        }
        return found.values.toList()
    }

    // ── plumbing ───────────────────────────────────────────────────────────────────

    private fun prune() {
        val cut = now() - LEASE_MS
        val it = subs.values.iterator()
        while (it.hasNext()) if (it.next().seen < cut) it.remove()
    }

    private fun send(to: InetSocketAddress, text: String) = sendBytes(to, text.toByteArray(Charsets.UTF_8))

    private fun sendBytes(to: InetSocketAddress, bytes: ByteArray) {
        val s = socket ?: return
        try {
            s.send(DatagramPacket(bytes, bytes.size, to))
        } catch (e: Exception) {
            // the display went away; its lease runs out
        }
    }

    private fun now() = android.os.SystemClock.elapsedRealtime()
}
