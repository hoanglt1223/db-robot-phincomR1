package info.dourok.voicebot.alarm

import android.content.Context
import android.media.AudioManager
import android.media.MediaPlayer
import android.os.Handler
import android.os.Looper
import info.dourok.voicebot.data.Settings
import info.dourok.voicebot.domain.voice.AppLog
import info.dourok.voicebot.domain.voice.VoiceDebugState
import info.dourok.voicebot.media.LocalMusicPlayer
import info.dourok.voicebot.mcp.DeviceActions

/**
 * Rings an alarm on the speaker: the chosen sound on a loop, louder over the first seconds, at the
 * alarm volume set in the panel (the owner's own volume is put back afterwards), until someone
 * presses the button on top of the speaker or "Tắt báo thức" in the panel -- or ten minutes pass.
 *
 * A song that was playing is paused for the alarm and carries on after it, the way a voice session
 * holds the music. The wake word is ignored while it rings (VoiceAssistant): a false wake on the
 * alarm's own sound must neither start a conversation nor silence the alarm.
 *
 * Main thread only inside; the public calls post to it.
 */
object AlarmRinger {
    data class Ring(val time: String, val label: String, val since: Long, val test: Boolean)

    @Volatile var ringing: Ring? = null
        private set

    val isRinging: Boolean get() = ringing != null

    private val main = Handler(Looper.getMainLooper())
    private var player: MediaPlayer? = null
    private var restoreVolume = -1
    private var fadeStep = 0
    private val autoStop = Runnable { stopNow("hết thời gian") }
    private val fade = object : Runnable {
        override fun run() {
            val p = player ?: return
            fadeStep++
            val v = (0.25f + 0.75f * fadeStep / FADE_STEPS).coerceAtMost(1f)
            try { p.setVolume(v, v) } catch (e: Exception) { return }
            if (fadeStep < FADE_STEPS) main.postDelayed(this, FADE_EVERY_MS)
        }
    }

    /**
     * @param sound "default" or the name of an uploaded file (AlarmBook)
     * @param testSeconds 0 for a real alarm; otherwise a short preview from the panel
     */
    fun start(context: Context, sound: String, time: String, label: String, testSeconds: Int = 0) {
        val app = context.applicationContext
        main.post { startNow(app, sound, time, label, testSeconds) }
    }

    fun stop(reason: String) {
        main.post { stopNow(reason) }
    }

    private fun startNow(context: Context, sound: String, time: String, label: String, testSeconds: Int) {
        if (player != null) stopNow("báo thức mới")
        val file = AlarmBook.soundFile(context.filesDir, sound)
        if (file == null && sound != AlarmBook.DEFAULT_SOUND) {
            AppLog.w("Không thấy tệp \"$sound\" -> dùng tiếng mặc định")
        }
        LocalMusicPlayer.holdForVoice()
        // the alarm's own volume, the owner's put back when it stops
        val get = DeviceActions.getVolume
        val set = DeviceActions.setVolume
        restoreVolume = -1
        if (get != null && set != null) {
            try {
                restoreVolume = get()
                set(Settings.alarmVolume.coerceIn(10, 100))
            } catch (e: Exception) {
                restoreVolume = -1
            }
        }
        val p = MediaPlayer()
        try {
            p.setAudioStreamType(AudioManager.STREAM_MUSIC)
            if (file != null) {
                p.setDataSource(file.path)
            } else {
                context.assets.openFd(DEFAULT_ASSET).use { fd ->
                    p.setDataSource(fd.fileDescriptor, fd.startOffset, fd.length)
                }
            }
            p.isLooping = true
            p.prepare()
            fadeStep = 0
            p.setVolume(0.25f, 0.25f)
            p.start()
        } catch (e: Exception) {
            AppLog.e("Không phát được âm báo thức: ${e.message}")
            try { p.release() } catch (_: Exception) { }
            if (file != null) {
                // a broken upload must not leave the owner without an alarm
                restoreVolumeNow()
                LocalMusicPlayer.releaseAfterVoice()
                startNow(context, AlarmBook.DEFAULT_SOUND, time, label, testSeconds)
            } else {
                restoreVolumeNow()
                LocalMusicPlayer.releaseAfterVoice()
            }
            return
        }
        player = p
        ringing = Ring(time, label, System.currentTimeMillis(), testSeconds > 0)
        main.postDelayed(fade, FADE_EVERY_MS)
        main.postDelayed(autoStop, if (testSeconds > 0) testSeconds * 1000L else RING_MAX_MS)
        AppLog.i(if (testSeconds > 0) "Thử âm báo thức" else "Báo thức $time${if (label.isNotEmpty()) " ($label)" else ""} — bấm nút trên loa để tắt")
    }

    private fun stopNow(reason: String) {
        main.removeCallbacks(autoStop)
        main.removeCallbacks(fade)
        val p = player ?: return
        player = null
        val was = ringing
        ringing = null
        try { p.stop() } catch (_: Exception) { }
        try { p.release() } catch (_: Exception) { }
        restoreVolumeNow()
        // a conversation going on lets its own end bring the music back
        if (!VoiceDebugState.awake) LocalMusicPlayer.releaseAfterVoice()
        if (was != null && !was.test) AppLog.i("Đã tắt báo thức ($reason)")
    }

    private fun restoreVolumeNow() {
        val v = restoreVolume
        restoreVolume = -1
        if (v >= 0) try { DeviceActions.setVolume?.invoke(v) } catch (_: Exception) { }
    }

    const val DEFAULT_ASSET = "alarm_default.wav"
    private const val RING_MAX_MS = 10 * 60 * 1000L
    private const val FADE_STEPS = 10
    private const val FADE_EVERY_MS = 2_000L
}
