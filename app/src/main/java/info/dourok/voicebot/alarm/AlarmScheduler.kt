package info.dourok.voicebot.alarm

import android.app.AlarmManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Build
import info.dourok.voicebot.data.Settings
import info.dourok.voicebot.domain.voice.AppLog
import info.dourok.voicebot.news.NewsSchedule
import java.util.TimeZone

/**
 * Keeps ONE AlarmManager alarm set: the next of the owner's alarms to ring (AlarmBook decides which).
 * Re-derived whenever the list changes, after each ring, at boot, and when the clock is set -- the
 * same lifecycle as the news bulletin's alarm (NewsAlarmScheduler), for the same reasons: alarms
 * do not survive a reboot, and the R1 boots on a 2018 clock that NTP corrects seconds later.
 */
object AlarmScheduler {
    private const val REQ = 4310

    fun alarms(): List<AlarmBook.Alarm> = AlarmBook.parse(Settings.alarmsJson)

    fun save(context: Context, list: List<AlarmBook.Alarm>) {
        Settings.alarmsJson = AlarmBook.toJson(list)
        reschedule(context)
    }

    /** The next ring as (alarm, epoch ms), or null when no alarm is on. */
    fun next(): Pair<AlarmBook.Alarm, Long>? {
        val n = AlarmBook.next(alarms(), System.currentTimeMillis(), TimeZone.getDefault()) ?: return null
        return (n[0] as AlarmBook.Alarm) to (n[1] as Long)
    }

    fun reschedule(context: Context) {
        val am = context.getSystemService(Context.ALARM_SERVICE) as AlarmManager
        am.cancel(pendingIntent(context, 0L))
        if (!NewsSchedule.clockIsTrustworthy(System.currentTimeMillis())) {
            AppLog.i("Đồng hồ máy chưa đúng -> hoãn hẹn báo thức tới khi chỉnh xong")
            return
        }
        val n = next() ?: return
        val at = n.second
        val pi = pendingIntent(context, at)
        try {
            when {
                Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && !am.canScheduleExactAlarms() ->
                    am.set(AlarmManager.RTC_WAKEUP, at, pi)
                Build.VERSION.SDK_INT >= Build.VERSION_CODES.M ->
                    am.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at, pi)
                else -> am.setExact(AlarmManager.RTC_WAKEUP, at, pi)
            }
        } catch (e: Throwable) {
            // runs on the panel's request thread: an exception here must not take the app down
            try { am.set(AlarmManager.RTC_WAKEUP, at, pi) } catch (_: Throwable) { }
        }
        AppLog.i("Báo thức kế tiếp: ${AlarmBook.whenText(at, TimeZone.getDefault())}")
    }

    private fun pendingIntent(context: Context, at: Long): PendingIntent {
        val intent = Intent(context, AlarmReceiver::class.java).putExtra(AlarmReceiver.EXTRA_AT, at)
        return PendingIntent.getBroadcast(
            context, REQ, intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
    }
}
