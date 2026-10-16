package info.dourok.voicebot.link

import info.dourok.voicebot.data.Settings
import info.dourok.voicebot.domain.voice.AppLog
import info.dourok.voicebot.media.LocalMusicPlayer
import info.dourok.voicebot.net.SpeakerAddress
import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * Several speakers playing the same thing: this speaker leads, the speakers chosen in the panel
 * follow. Whatever starts playing here -- a song from the Media tab or by voice, a radio station,
 * a podcast episode, the next song of the queue -- is sent to each follower, every speaker
 * prepares it and holds it, and when all are ready (or [READY_WAIT_MS] has passed) the leader
 * tells all of them to start [GO_DELAY_MS] from now. Pause, resume, stop and seek follow too.
 *
 * Starting on a shared "go" rather than "as soon as you can" is what keeps them together: one
 * speaker's stream buffering in half a second and another's in three would otherwise put them
 * seconds apart. What remains is the network's few milliseconds and each Android player's own
 * start-up latency -- close enough for different rooms, not for a stereo pair (see README).
 *
 * The followers are plain DB-Robot speakers reached over their control server (port 8088):
 * `POST /api/group/play` (prepare + hold, answers once prepared), `/api/group/go`,
 * `/api/group/cmd`. Each request carries the header `X-DBR-Group`, which a web page on some other
 * site cannot send without the speaker agreeing first, so a page in the owner's browser cannot
 * make the speakers play something.
 */
object GroupSync : LocalMusicPlayer.GroupLeader {
    private const val READY_WAIT_MS = 9_000L
    private const val GO_DELAY_MS = 400L
    const val HEADER = "X-DBR-Group"

    data class Member(val id: String, val name: String, val ip: String)

    fun members(): List<Member> {
        val out = ArrayList<Member>()
        try {
            val a = JSONArray(Settings.groupMembers)
            for (i in 0 until a.length()) {
                val o = a.optJSONObject(i) ?: continue
                val ip = o.optString("ip")
                if (SpeakerAddress.isAddress(ip)) out.add(Member(o.optString("id"), o.optString("name"), ip))
            }
        } catch (_: Exception) { }
        return out
    }

    fun memberCount() = members().size

    fun isLeader() = Settings.groupSync && members().isNotEmpty()

    /** Lead or stop leading, after the settings changed (and at start-up). */
    fun refresh() {
        LocalMusicPlayer.groupLeader = if (isLeader()) this else null
    }

    fun save(sync: Boolean, list: List<Member>) {
        val a = JSONArray()
        list.forEach { a.put(JSONObject().put("id", it.id).put("name", it.name).put("ip", it.ip)) }
        Settings.groupMembers = a.toString()
        Settings.groupSync = sync
        refresh()
    }

    // ── leading ────────────────────────────────────────────────────────────────────

    override fun onTrack(track: LocalMusicPlayer.Track, token: String) {
        val list = members()
        Thread({ lead(track, token, list) }, "group-lead").apply { isDaemon = true }.start()
    }

    private fun lead(track: LocalMusicPlayer.Track, token: String, list: List<Member>) {
        val me = SpeakerAddress.current()
        // A podcast episode is relayed by this speaker under a token only it knows: the others
        // fetch it from here. Radio stays local -- every speaker relays its own station.
        val url = if (track.url.startsWith("http://127.0.0.1:") && track.url.contains("/podcast/") && me.isNotEmpty())
            track.url.replace("http://127.0.0.1:", "http://$me:") else track.url
        val body = JSONObject()
            .put("token", token)
            .put("from", JSONObject().put("name", Settings.speakerName).put("ip", me))
            .put("track", JSONObject().put("id", track.id).put("title", track.title).put("artist", track.artist)
                .put("thumbnail", track.thumbnail).put("duration", track.duration).put("url", url))
            .toString()
        val done = CountDownLatch(list.size)
        val failed = ArrayList<String>()
        list.forEach { m ->
            Thread({
                val ok = post(m.ip, "/api/group/play", body, READY_WAIT_MS.toInt() + 1500) != null
                if (!ok) synchronized(failed) { failed.add(m.name.ifBlank { m.ip }) }
                done.countDown()
            }, "group-prep").apply { isDaemon = true }.start()
        }
        val end = System.currentTimeMillis() + READY_WAIT_MS
        while (System.currentTimeMillis() < end && !LocalMusicPlayer.isReady(token)) Thread.sleep(50)
        done.await((end - System.currentTimeMillis()).coerceAtLeast(0), TimeUnit.MILLISECONDS)
        val go = JSONObject().put("token", token).put("delay", GO_DELAY_MS).toString()
        list.forEach { m -> Thread({ post(m.ip, "/api/group/go", go, 2000) }, "group-go").apply { isDaemon = true }.start() }
        LocalMusicPlayer.goHeld(token, GO_DELAY_MS)
        if (failed.isNotEmpty()) AppLog.w("Nhóm loa: không nối được ${failed.joinToString(", ")}")
    }

    override fun onCommand(command: String, arg: Int) {
        val list = members()
        val body = JSONObject().put("c", command).put("arg", arg).toString()
        list.forEach { m -> Thread({ post(m.ip, "/api/group/cmd", body, 2500) }, "group-cmd").apply { isDaemon = true }.start() }
    }

    /** Set a member's volume from this speaker's panel. */
    fun memberVolume(ip: String, percent: Int): Boolean {
        if (members().none { it.ip == ip }) return false
        return get(ip, "/api/set?key=volume&value=${percent.coerceIn(0, 100)}") != null
    }

    // ── following (called by ControlServer) ────────────────────────────────────────

    /** `/api/group/play`: prepare the leader's track and answer once it is ready (or not). */
    fun follow(body: String): String {
        val j = try { JSONObject(body) } catch (e: Exception) { return """{"ok":false,"error":"bad body"}""" }
        val t = j.optJSONObject("track") ?: return """{"ok":false,"error":"no track"}"""
        val token = j.optString("token")
        val url = t.optString("url")
        if (token.isEmpty() || !(url.startsWith("http://") || url.startsWith("https://"))) {
            return """{"ok":false,"error":"bad track"}"""
        }
        val from = j.optJSONObject("from")?.optString("name").orEmpty()
        AppLog.i("Nhóm loa: phát cùng ${from.ifBlank { "loa chính" }} — ${t.optString("title")}")
        LocalMusicPlayer.playForGroup(
            LocalMusicPlayer.Track(
                id = t.optString("id"), title = t.optString("title"), artist = t.optString("artist"),
                thumbnail = t.optString("thumbnail"), duration = t.optString("duration"), url = url,
            ),
            token,
        )
        val end = System.currentTimeMillis() + READY_WAIT_MS
        while (System.currentTimeMillis() < end && !LocalMusicPlayer.isReady(token)) Thread.sleep(50)
        return JSONObject().put("ok", true).put("ready", LocalMusicPlayer.isReady(token)).toString()
    }

    fun go(body: String): String {
        val j = try { JSONObject(body) } catch (e: Exception) { return """{"ok":false}""" }
        LocalMusicPlayer.goHeld(j.optString("token"), j.optLong("delay", GO_DELAY_MS).coerceIn(0, 3000))
        return """{"ok":true}"""
    }

    fun command(body: String): String {
        val j = try { JSONObject(body) } catch (e: Exception) { return """{"ok":false}""" }
        LocalMusicPlayer.groupCommand(j.optString("c"), j.optInt("arg", 0))
        return """{"ok":true}"""
    }

    // ── HTTP ───────────────────────────────────────────────────────────────────────

    private fun post(ip: String, path: String, body: String, readMs: Int): String? =
        request(ip, path, "POST", body, readMs)

    private fun get(ip: String, path: String): String? = request(ip, path, "GET", null, 3000)

    private fun request(ip: String, path: String, method: String, body: String?, readMs: Int): String? {
        var c: HttpURLConnection? = null
        return try {
            c = URL("http://$ip:8088$path").openConnection() as HttpURLConnection
            c.connectTimeout = 2000
            c.readTimeout = readMs
            c.requestMethod = method
            c.setRequestProperty(HEADER, "1")
            if (body != null) {
                val bytes = body.toByteArray(Charsets.UTF_8)
                c.doOutput = true
                c.setRequestProperty("Content-Type", "application/json; charset=utf-8")
                c.setFixedLengthStreamingMode(bytes.size)
                c.outputStream.use { it.write(bytes) }
            }
            if (c.responseCode in 200..299) c.inputStream.bufferedReader().use { it.readText() } else null
        } catch (e: Exception) {
            null
        } finally {
            c?.disconnect()
        }
    }
}
