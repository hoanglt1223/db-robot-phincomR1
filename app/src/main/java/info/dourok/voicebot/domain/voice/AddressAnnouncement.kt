package info.dourok.voicebot.domain.voice

import kotlinx.coroutines.flow.MutableSharedFlow

/**
 * The speaker reading its own network address out loud without being asked, so that the owner can
 * find the control panel again after the address has changed (see
 * [info.dourok.voicebot.net.SpeakerAddress]).
 *
 * ControlServer decides WHEN -- it can see the network and the registration with the server --
 * and [VoiceAssistant] does the talking, because only it holds the channel to the server. Neither
 * holds a reference to the other, hence this object, like [TextCommands].
 */
object AddressAnnouncement {
    /** An address to read out. */
    val flow = MutableSharedFlow<String>(extraBufferCapacity = 1)

    /**
     * Requests in a row that the server did not answer. ControlServer waits longer after each and
     * gives up after a few: an address nobody can be told about is not worth a chime every minute.
     */
    @Volatile var failures: Int = 0
}
