/* license:BSD-3-Clause
 * copyright-holders:David Sexton
 *
 * dtalk_jni.cpp - JNI bridge between org.doubledroid.tts.DoubleTalkNative
 * (Kotlin) and the dtalk C API. Thin marshalling only; all serialization of
 * calls into a (non-thread-safe) dtalk handle happens on the Kotlin side.
 */

#include <jni.h>
#include <cstdint>

#include "dtalk.h"

extern "C" {

JNIEXPORT jlong JNICALL
Java_org_doubledroid_tts_DoubleTalkNative_create(JNIEnv *env, jobject, jbyteArray rom)
{
	jsize len = env->GetArrayLength(rom);
	jbyte *bytes = env->GetByteArrayElements(rom, nullptr);
	dtalk *dt = dtalk_create(bytes, (size_t)len);
	env->ReleaseByteArrayElements(rom, bytes, JNI_ABORT);
	return (jlong)(intptr_t)dt;
}

JNIEXPORT void JNICALL
Java_org_doubledroid_tts_DoubleTalkNative_destroy(JNIEnv *, jobject, jlong h)
{
	dtalk_destroy((dtalk *)(intptr_t)h);
}

JNIEXPORT void JNICALL
Java_org_doubledroid_tts_DoubleTalkNative_reset(JNIEnv *, jobject, jlong h)
{
	dtalk_reset((dtalk *)(intptr_t)h);
}

JNIEXPORT jint JNICALL
Java_org_doubledroid_tts_DoubleTalkNative_sampleRate(JNIEnv *, jobject, jlong h)
{
	return (jint)dtalk_sample_rate((const dtalk *)(intptr_t)h);
}

JNIEXPORT jint JNICALL
Java_org_doubledroid_tts_DoubleTalkNative_rateBoostMax(JNIEnv *, jobject)
{
	return (jint)dtalk_rate_boost_max();
}

JNIEXPORT void JNICALL
Java_org_doubledroid_tts_DoubleTalkNative_setRateBoost(JNIEnv *, jobject, jlong h, jint level)
{
	dtalk_set_rate_boost((dtalk *)(intptr_t)h, (int)level);
}

JNIEXPORT void JNICALL
Java_org_doubledroid_tts_DoubleTalkNative_setLowpassHz(JNIEnv *, jobject, jlong h, jint hz)
{
	dtalk_set_lowpass_hz((dtalk *)(intptr_t)h, (uint32_t)hz);
}

JNIEXPORT void JNICALL
Java_org_doubledroid_tts_DoubleTalkNative_queue(JNIEnv *env, jobject, jlong h, jbyteArray data)
{
	jsize len = env->GetArrayLength(data);
	jbyte *bytes = env->GetByteArrayElements(data, nullptr);
	dtalk_queue((dtalk *)(intptr_t)h, bytes, (size_t)len);
	env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_org_doubledroid_tts_DoubleTalkNative_stop(JNIEnv *, jobject, jlong h)
{
	dtalk_stop((dtalk *)(intptr_t)h);
}

JNIEXPORT jboolean JNICALL
Java_org_doubledroid_tts_DoubleTalkNative_active(JNIEnv *, jobject, jlong h)
{
	return dtalk_active((dtalk *)(intptr_t)h) ? JNI_TRUE : JNI_FALSE;
}

/* Fills out[] with up to out.length signed 16-bit samples through the modeled
 * output stage; returns the number of samples produced (0 = idle). */
JNIEXPORT jint JNICALL
Java_org_doubledroid_tts_DoubleTalkNative_synth16(JNIEnv *env, jobject, jlong h, jshortArray out)
{
	jsize max = env->GetArrayLength(out);
	jshort *buf = env->GetShortArrayElements(out, nullptr);
	size_t n = dtalk_synth16((dtalk *)(intptr_t)h, (int16_t *)buf, (size_t)max);
	env->ReleaseShortArrayElements(out, buf, 0);
	return (jint)n;
}

} // extern "C"
