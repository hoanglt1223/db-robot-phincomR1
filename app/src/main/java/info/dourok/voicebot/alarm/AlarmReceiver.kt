package info.dourok.voicebot.alarm

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import info.dourok.voicebot.data.Settings
import info.dourok.voicebot.domain.voice.AppLog
import java.util.TimeZone

/** The next alarm's minute has come (AlarmScheduler): ring it, then set the one after. */
class AlarmReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val app = context.applicationContext
        Settings.init(app)   // a broadcast can arrive before the app is up
        val at = intent.getLongExtra(EXTRA_AT, 0L)
        try {
            val now = System.currentTimeMillis()
            val list = AlarmScheduler.alarms()
            if (at > 0 && AlarmBook.isDue(at, now)) {
                val due = AlarmBook.dueAt(list, at, TimeZone.getDefault())
                if (due.isNotEmpty()) {
                    val first = due[0]
                    val label = due.map { it.label }.filter { it.isNotEmpty() }.joinToString(", ")
                    AlarmRinger.start(app, first.sound, first.time(), label)
                    Settings.alarmsJson = AlarmBook.toJson(AlarmBook.afterRinging(list, due))
                }
            } else if (at > 0) {
                // delivered far from its minute: the clock jumped (boot, NTP) -- don't ring
                AppLog.i("Bỏ qua báo thức lệch giờ (đồng hồ máy vừa nhảy)")
            }
        } catch (e: Exception) {
            AppLog.e("Báo thức lỗi: ${e.message}")
        } finally {
            AlarmScheduler.reschedule(app)
        }
    }

    companion object {
        const val EXTRA_AT = "at"
    }
}
