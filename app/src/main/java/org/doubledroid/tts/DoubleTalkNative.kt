// license:BSD-3-Clause
// copyright-holders:David Sexton
//
// Raw JNI binding to libdtalk.so (the standalone DoubleTalk PC emulator
// core vendored from doubletalk-pc). NOT thread-safe: every call into a
// handle must be serialized by the caller (DoubleTalkEngine does this).
package org.doubledroid.tts

object DoubleTalkNative {
    init {
        System.loadLibrary("dtalk")
    }

    /** Create an instance from the 512KB firmware ROM; 0 on failure. */
    external fun create(rom: ByteArray): Long
    external fun destroy(handle: Long)
    external fun reset(handle: Long)
    external fun sampleRate(handle: Long): Int
    external fun rateBoostMax(): Int
    external fun setRateBoost(handle: Long, level: Int)
    external fun setLowpassHz(handle: Long, hz: Int)

    /** Caps any silence run in the generated PCM to [ms] milliseconds; 0
     * disables trimming (the authentic firmware pause). */
    external fun setPauseCapMs(handle: Long, ms: Int)

    /**
     * Configure the final upsample stage: [hz] must be >= the card's native
     * rate (smaller values clamp up to it); 0 disables resampling. See
     * dtalk_set_output_rate for why this exists (the card's raw 10504Hz rate
     * upsamples badly through the OS's own audio pipeline).
     */
    external fun setOutputRate(handle: Long, hz: Int)

    /** Queue raw bytes (text + 0x01-prefixed commands; CR starts speech). */
    external fun queue(handle: Long, data: ByteArray)

    /** Immediate stop: drops queued input, flushes buffered speech/audio. */
    external fun stop(handle: Long)
    external fun active(handle: Long): Boolean

    /**
     * Run the emulation forward, filling [out] with signed 16-bit mono PCM
     * through the modeled output stage (and the setOutputRate resample stage,
     * if configured). Returns samples produced; 0 = idle.
     */
    external fun synth16(handle: Long, out: ShortArray): Int
}
