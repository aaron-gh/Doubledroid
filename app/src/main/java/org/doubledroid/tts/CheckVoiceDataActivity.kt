// license:BSD-3-Clause
// copyright-holders:David Sexton
//
// TTS engine protocol: the system fires ACTION_CHECK_TTS_DATA to ask
// whether the engine's voice data is usable. Ours is usable exactly when
// the user has imported a working firmware ROM.
package org.doubledroid.tts

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.speech.tts.TextToSpeech

class CheckVoiceDataActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val haveRom = DoubleTalkEngine.romFile(this).isFile
        val result = if (haveRom) TextToSpeech.Engine.CHECK_VOICE_DATA_PASS
        else TextToSpeech.Engine.CHECK_VOICE_DATA_FAIL
        val data = Intent().apply {
            putStringArrayListExtra(
                TextToSpeech.Engine.EXTRA_AVAILABLE_VOICES,
                if (haveRom) arrayListOf("eng-USA") else arrayListOf())
            putStringArrayListExtra(
                TextToSpeech.Engine.EXTRA_UNAVAILABLE_VOICES,
                if (haveRom) arrayListOf() else arrayListOf("eng-USA"))
        }
        setResult(result, data)
        finish()
    }
}
