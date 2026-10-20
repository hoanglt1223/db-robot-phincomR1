package info.dourok.voicebot.domain.voice

/**
 * The assistant's emotion, for the display's emoji (the 21 names of the xiaozhi firmware's
 * emoji_collection: neutral, happy, laughing, funny, sad, angry, crying, loving, embarrassed,
 * surprised, shocked, thinking, winking, cool, relaxed, delicious, kissy, confident, sleepy, silly,
 * confused).
 *
 * The xiaozhi server is meant to send `{"type":"llm","emotion":…}` with each reply, and the firmware
 * shows exactly that. A server that does not (or sends only the emoji in `text`) left the display on
 * "neutral" for good, so when no emotion came with a reply it is guessed from the reply itself:
 * an emoji in the text first, then a few Vietnamese words. Pure Kotlin, no Android.
 */
object EmotionGuess {
    val NAMES = setOf(
        "neutral", "happy", "laughing", "funny", "sad", "angry", "crying", "loving", "embarrassed",
        "surprised", "shocked", "thinking", "winking", "cool", "relaxed", "delicious", "kissy",
        "confident", "sleepy", "silly", "confused",
    )

    /** The firmware's own emoji for each name first, then the usual look-alikes. */
    private val EMOJI = linkedMapOf(
        "😶" to "neutral", "🙂" to "happy", "😆" to "laughing", "😂" to "funny", "😔" to "sad",
        "😠" to "angry", "😭" to "crying", "😍" to "loving", "😳" to "embarrassed", "😯" to "surprised",
        "😱" to "shocked", "🤔" to "thinking", "😉" to "winking", "😎" to "cool", "😌" to "relaxed",
        "🤤" to "delicious", "😘" to "kissy", "😏" to "confident", "😴" to "sleepy", "😜" to "silly",
        "🙄" to "confused",
        "😀" to "happy", "😃" to "happy", "😄" to "laughing", "😁" to "laughing", "😊" to "happy",
        "☺" to "happy", "🤗" to "happy", "🥳" to "laughing", "🤣" to "funny", "😹" to "funny",
        "😢" to "sad", "😞" to "sad", "😟" to "sad", "🥺" to "sad", "😥" to "sad", "☹" to "sad",
        "😡" to "angry", "🤬" to "angry", "😤" to "angry", "😿" to "crying",
        "🥰" to "loving", "😻" to "loving", "❤" to "loving", "💕" to "loving", "💖" to "loving",
        "😲" to "surprised", "😮" to "surprised", "🤯" to "shocked", "😨" to "shocked", "😰" to "shocked",
        "🧐" to "thinking", "🤨" to "thinking", "😋" to "delicious", "😚" to "kissy", "😙" to "kissy",
        "😗" to "kissy", "🥱" to "sleepy", "😪" to "sleepy", "💤" to "sleepy", "😛" to "silly",
        "😝" to "silly", "🤪" to "silly", "😕" to "confused", "😐" to "neutral", "😑" to "neutral",
        "🤓" to "cool", "😇" to "relaxed",
    )

    /** Words that give the mood away, in the order they are tried (lowercase). */
    private val WORDS = listOf(
        "laughing" to listOf("haha", "hihi", "hehe", "buồn cười", "vui quá", "vui ghê"),
        "sad" to listOf("xin lỗi", "rất tiếc", "tiếc quá", "thật tiếc", "buồn", "chia buồn"),
        "crying" to listOf("hu hu", "huhu"),
        "loving" to listOf("yêu bạn", "thương bạn", "yêu quá", "dễ thương", "đáng yêu"),
        "surprised" to listOf("wow", "ồ", "thật sao", "không ngờ", "ôi"),
        "angry" to listOf("tức giận", "bực mình", "khó chịu"),
        "sleepy" to listOf("ngủ ngon", "buồn ngủ"),
        "thinking" to listOf("để mình xem", "để tôi xem", "hmm", "ừm", "suy nghĩ", "để mình nghĩ"),
        "delicious" to listOf("ngon quá", "thơm ngon", "món ngon"),
        "confident" to listOf("chắc chắn", "dĩ nhiên", "tất nhiên"),
        "happy" to listOf("tuyệt", "chúc mừng", "rất vui", "vui", "chào bạn", "cảm ơn", "hay quá"),
    )

    /** The emotion of the first known emoji in [text], or null. */
    fun fromEmoji(text: String?): String? {
        if (text.isNullOrEmpty()) return null
        var best: String? = null
        var at = Int.MAX_VALUE
        for ((e, name) in EMOJI) {
            val i = text.indexOf(e)
            if (i in 0 until at) { at = i; best = name }
        }
        return best
    }

    /** An emoji in the reply, else a telling word, else null (keep what is shown). */
    fun fromText(text: String?): String? {
        if (text.isNullOrBlank()) return null
        fromEmoji(text)?.let { return it }
        val t = text.lowercase()
        for ((name, words) in WORDS) if (words.any { w -> hasWord(t, w) }) return name
        return null
    }

    /** [w] in [t] on word boundaries ("ôi" must not match inside "tôi"). */
    private fun hasWord(t: String, w: String): Boolean {
        var from = 0
        while (true) {
            val i = t.indexOf(w, from)
            if (i < 0) return false
            val before = if (i == 0) ' ' else t[i - 1]
            val after = if (i + w.length >= t.length) ' ' else t[i + w.length]
            if (!before.isLetterOrDigit() && !after.isLetterOrDigit()) return true
            from = i + 1
        }
    }

    /** A server name, cleaned: lower case, one of the 21, or null. */
    fun clean(name: String?): String? = name?.trim()?.lowercase()?.takeIf { it in NAMES }
}
