// license:BSD-3-Clause
// copyright-holders:David Sexton
//
// Android TTS engine driving the DoubleTalk PC emulator: the framework hands
// us text + rate/pitch, we translate that into the card's 0x01-command
// prefix (exactly as the NVDA driver does), feed the firmware, and stream
// its 10.5kHz PCM (through the modeled output stage) back as 16-bit audio.
package org.doubledroid.tts

import android.media.AudioFormat
import android.speech.tts.SynthesisCallback
import android.speech.tts.SynthesisRequest
import android.speech.tts.TextToSpeech
import android.speech.tts.TextToSpeechService
import android.speech.tts.Voice
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.Locale

private const val SAMPLES_PER_CHUNK = 2048

class DoubleTalkTtsService : TextToSpeechService() {

    @Volatile
    private var stopRequested = false

    // The firmware speaks American English only.
    private fun langAvailability(lang: String?, country: String?): Int = when {
        lang == "eng" || lang == "en" -> when (country) {
            "USA", "US" -> TextToSpeech.LANG_COUNTRY_AVAILABLE
            null, "" -> TextToSpeech.LANG_AVAILABLE
            else -> TextToSpeech.LANG_AVAILABLE
        }
        else -> TextToSpeech.LANG_NOT_SUPPORTED
    }

    override fun onIsLanguageAvailable(lang: String?, country: String?, variant: String?): Int =
        langAvailability(lang, country)

    override fun onGetLanguage(): Array<String> = arrayOf("eng", "USA", "")

    override fun onLoadLanguage(lang: String?, country: String?, variant: String?): Int =
        langAvailability(lang, country)

    override fun onGetVoices(): MutableList<Voice> =
        (listOf(DoubleTalkEngine.DEFAULT_VOICE_ID) + DoubleTalkEngine.VOICE_IDS)
            .map { id ->
                Voice(id, Locale.US, Voice.QUALITY_NORMAL, Voice.LATENCY_NORMAL,
                    false, emptySet())
            }.toMutableList()

    override fun onIsValidVoiceName(name: String?): Int =
        if (name == DoubleTalkEngine.DEFAULT_VOICE_ID ||
            DoubleTalkEngine.voiceIndexForId(name) >= 0) TextToSpeech.SUCCESS
        else TextToSpeech.ERROR

    override fun onLoadVoice(name: String?): Int = onIsValidVoiceName(name)

    override fun onGetDefaultVoiceNameFor(lang: String?, country: String?, variant: String?): String =
        DoubleTalkEngine.DEFAULT_VOICE_ID

    override fun onStop() {
        stopRequested = true
        DoubleTalkEngine.stop()
    }

    override fun onSynthesizeText(request: SynthesisRequest, callback: SynthesisCallback) {
        if (!DoubleTalkEngine.open(this)) {
            // No ROM imported yet (or it failed to boot) — the settings
            // activity is where the user fixes this.
            callback.error(TextToSpeech.ERROR_SYNTHESIS)
            return
        }
        stopRequested = false

        // Voice: an explicit per-request concrete voice wins; the default
        // meta-voice (or anything unrecognized) resolves to the saved
        // preference HERE, at synthesis time, so changing the default in
        // settings applies immediately even to long-lived clients that
        // pinned the default voice name at connect (see DEFAULT_VOICE_ID).
        var cardVoice = DoubleTalkEngine.voiceIndexForId(request.voiceName)
        if (cardVoice < 0) {
            cardVoice = DoubleTalkEngine.prefs(this)
                .getInt(DoubleTalkEngine.PREF_VOICE, 0).coerceIn(0, 7)
        }

        val text = request.charSequenceText?.toString() ?: ""
        val utterance = DoubleTalkEngine.utteranceBytes(
            this, cardVoice, request.speechRate, request.pitch, text)

        val rate = DoubleTalkEngine.sampleRate
        if (callback.start(rate, AudioFormat.ENCODING_PCM_16BIT, 1) != TextToSpeech.SUCCESS) {
            return
        }

        // Flush anything a previous (interrupted) utterance left behind,
        // then feed this one. Settings persist across dtalk_stop.
        DoubleTalkEngine.stop()
        DoubleTalkEngine.queue(utterance)

        val samples = ShortArray(SAMPLES_PER_CHUNK)
        val byteBuf = ByteBuffer.allocate(SAMPLES_PER_CHUNK * 2).order(ByteOrder.LITTLE_ENDIAN)
        val maxChunk = callback.maxBufferSize.coerceAtLeast(2)

        while (!stopRequested) {
            val n = DoubleTalkEngine.synthChunk(samples)
            if (n == 0) break
            byteBuf.clear()
            byteBuf.asShortBuffer().put(samples, 0, n)
            val bytes = byteBuf.array()
            val total = n * 2
            var off = 0
            while (off < total && !stopRequested) {
                val len = minOf(maxChunk, total - off)
                if (callback.audioAvailable(bytes, off, len) != TextToSpeech.SUCCESS) {
                    stopRequested = true
                    break
                }
                off += len
            }
        }

        if (stopRequested) {
            DoubleTalkEngine.stop()
        }
        callback.done()
    }
}
