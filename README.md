# DoubleDroid — DoubleTalk PC speech synthesizer for Android

An Android **text-to-speech engine** that is a real RC Systems **DoubleTalk PC**:
it runs the card's original 80C188EB firmware in the
[doubletalk-pc](https://github.com/daiverd/doubletalk-pc) standalone emulator (vendored MAME CPU core),
compiled for Android with the NDK. Select it as your system TTS engine and
TalkBack — or any app that speaks — comes out of a 1993 ISA text-to-speech
card, complete with Perfect Paul and friends.

Nothing here reimplements the speech algorithms: the firmware does all the
synthesis, exactly as on hardware. The firmware ROM is proprietary and is
**not included** — you import your own dump in the app (see below).

## How it works

| Layer | What |
|---|---|
| `app/src/main/cpp/doubletalk/` | The emulator core, vendored from doubletalk-pc: MAME 8086/80186 + 80C188EB CPU core, compatibility shim, board wrapper, `dtalk` C API |
| `app/src/main/cpp/dtalk_jni.cpp` | JNI bridge to the `dtalk` C API |
| `DoubleTalkEngine.kt` | Owns the single emulator instance; serializes all native calls; builds the card's `0x01`-command utterance prefixes (same WYSIWYG logic as the NVDA driver) |
| `DoubleTalkTtsService.kt` | `TextToSpeechService`: maps Android rate/pitch/voice onto the card's `nS`/`nP`/`nO` commands and streams the firmware's 10,504 Hz PCM (16-bit, through the modeled output stage) to the TTS callback |
| `SettingsActivity.kt` | ROM import + verification, default voice, output filter, rate boost, and the manual's voice-quality knobs (tone/articulation/expression/formant/reverb), plus a local test-speech button |

The 8 firmware voices (Perfect Paul, Vader, Big Bob, Precise Pete, Ricochet,
Biff, Skip, Robo Robert) are exposed as Android `Voice`s, selectable
per-request by apps or as the engine default in settings.

Android's speech-rate setting maps to the card's `nS 0–9` (normal = `5S`, the
card default); pitch scales relative to each voice's own preset pitch so
"normal pitch" always sounds like that voice. The optional **rate boost**
rescales the firmware's rate table in RAM for faster-than-`9S` speech with
pitch preserved, exactly as in the NVDA add-on.

## Building

Requirements: Android SDK (platform 35, build-tools), **NDK 27**, CMake 3.22,
JDK 17+. Point `local.properties` at your SDK, then:

```sh
./gradlew assembleDebug     # app/build/outputs/apk/debug/app-debug.apk
```

## Installing and using

1. Install the APK.
2. Open **DoubleDroid** and tap **Import ROM…** to supply your 512 KB
   `doubletalkpc.bin` firmware dump. The app verifies it
   (CRC32 `66685631`, SHA-1 `bf7e78d6381c76d291ee069971873347a314ffff`)
   and boot-tests it in the emulator before accepting.
3. Try the **Speak** button on the same screen.
4. System settings → Accessibility (or Sound) → **Text-to-speech output** →
   select **DoubleTalk PC (emulated)** as the preferred engine.

Voice, output filter (the card's authentic 3 kHz reconstruction filter or a
brighter 4.8 kHz), rate boost, and voice-quality knobs live in the same
settings screen (also reachable via the gear next to the engine in system TTS
settings). "Auto" on a quality knob follows the selected voice's own firmware
preset.

## The firmware ROM

`doubletalkpc.bin` (512 KB) is proprietary RC Systems firmware and carries no
redistribution grant. Supply your own dump (e.g. read from a DoubleTalk PC
card you own). The app stores it in its private data directory only; the
repository `.gitignore` blocks `*.bin` so it cannot be committed.

## Licensing

All code in this repository is **BSD-3-Clause** — the Android engine and the
vendored doubletalk-pc / MAME sources alike. See [LICENSE](LICENSE) and
`app/src/main/cpp/doubletalk/NOTICE` for per-component attribution. The
firmware ROM is proprietary and never distributable with this software.

## Credits

- **David Sexton** — doubletalk-pc standalone emulator, dtalk C API, NVDA
  driver this engine's parameter mapping is ported from.
- **Christopher Toth** — 80C188EB CPU support in the MAME core and the
  firmware-to-audio reverse engineering.
- **MAME** and its contributors — the 8086/80186 CPU core.
