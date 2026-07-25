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

    /** Queue raw bytes (text + 0x01-prefixed commands; CR starts speech). */
    external fun queue(handle: Long, data: ByteArray)

    /** Immediate stop: drops queued input, flushes buffered speech/audio. */
    external fun stop(handle: Long)
    external fun active(handle: Long): Boolean

    /**
     * Run the emulation forward, filling [out] with signed 16-bit mono PCM
     * through the modeled output stage. Returns samples produced; 0 = idle.
     */
    external fun synth16(handle: Long, out: ShortArray): Int
}
