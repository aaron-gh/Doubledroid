// license:BSD-3-Clause
// copyright-holders:David Sexton
//
// TTS engine protocol: sample text for the system's "Listen to an example".
package org.doubledroid.tts

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.speech.tts.TextToSpeech

class GetSampleTextActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val data = Intent().putExtra(
            TextToSpeech.Engine.EXTRA_SAMPLE_TEXT,
            getString(R.string.sample_text))
        setResult(TextToSpeech.Engine.CHECK_VOICE_DATA_PASS, data)
        finish()
    }
}
