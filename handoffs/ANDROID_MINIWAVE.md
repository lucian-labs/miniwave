# Android Miniwave — Native Synth Engine + Native UI

## Status: SCAFFOLDED — needs Android Studio + NDK to compile

Full port of the miniwave engine to Android with **100% native UI** (Jetpack Compose, no WebView). Phone becomes a standalone synth — USB MIDI in, AAudio out, swipeable Compose UI, direct JNI state access.

## Architecture

```
android/
  app/src/main/
    jni/
      miniwave_android.c       — platform_* impls, AAudio callback, JNI bridge (40+ exports)
      CMakeLists.txt            — NDK: links aaudio, log, android, m
    java/com/waveloop/miniwave/
      MiniwaveEngine.kt        — JNI bridge: direct struct reads + JSON for detail
      MiniwaveService.kt       — foreground service (keeps audio alive)
      MidiHandler.kt           — Android MIDI API → JNI dispatch
      MainActivity.kt          — Compose activity + HorizontalPager swipe nav
      ui/
        Theme.kt               — miniwave color palette (Mw.accent, Mw.good, etc.)
        Components.kt          — PeakMeter, SlotCard, MwSlider
        RackScreen.kt          — 16-channel grid with live peak meters
        ChannelScreen.kt       — focused channel: volume, mute/solo, instrument picker
        VizScreen.kt           — fullscreen Canvas waveform + spectrum viz
    assets/web/                — symlink to common/web/ (for HTTP remote control)
    res/                       — icon, themes, notification
  build.gradle.kts             — AGP 8.2.2, Kotlin 1.9.22, Compose BOM 2024.01
  build-android.sh             — build + optional adb install
```

## Data Flow — No WebView, No HTTP Round-Trip

```
┌─ AAudio RT thread ─────────────────────────────────────┐
│  audio_callback() → render_mix() → float stereo out    │
└────────────────────────────────────────────────────────┘
          ↑ reads g_rack state                ↑ reads scope_buf
┌─ JNI hot path (~30fps) ───────────────────────────────┐
│  nativePollPeaks()    → 36 floats (16 slots + master) │
│  nativeGetScope()     → 512 floats (L/R interleaved)  │
│  nativeGetSlotInfo()  → 64 ints (active/type/mute/solo)│
│  nativeGetFocusedCh() → int                            │
└────────────────────────────────────────────────────────┘
          ↓ drives Compose recomposition
┌─ Compose UI ──────────────────────────────────────────┐
│  HorizontalPager: RACK ←swipe→ CHANNEL ←swipe→ VIZ   │
│  LaunchedEffect polls JNI at 30fps, updates state     │
└────────────────────────────────────────────────────────┘
          ↓ user actions
┌─ JNI direct writes (no JSON) ─────────────────────────┐
│  nativeSetSlotVolume(), nativeSetFocusedCh(),          │
│  nativeSetSlot(), nativeNoteOn(), nativePanic(), etc.  │
└────────────────────────────────────────────────────────┘
```

## JNI Surface (MiniwaveEngine.kt)

**Hot path** — pre-allocated arrays, zero allocation per frame:
- `nativePollPeaks(FloatArray)` — 36 floats
- `nativeGetScope(FloatArray)` — 512 floats
- `nativeGetSlotInfo(IntArray)` — 64 ints
- `nativeGetFocusedCh()`, `nativeGetBpm()`, `nativeGetMasterVolume()`

**Cold path** — JSON for detail:
- `nativeGetChannelJson(ch)` — instrument vtable json_status
- `nativeGetRackJson()` — full rack dump

**Direct writes** — no JSON overhead:
- `nativeSetSlotVolume(ch, val)`, `nativeSetSlotMute(ch, bool)`, `nativeSetSlotSolo(ch, bool)`
- `nativeSetSlot(ch, typeName)`, `nativeClearSlot(ch)`
- `nativeSetFocusedCh(ch)`, `nativeSetMasterVolume(val)`, `nativeSetBpm(val)`
- `nativeNoteOn(ch, note, vel)`, `nativeNoteOff(ch, note)`, `nativePanic()`

## UI — Swipeable Pages

| Page | Content | Data source |
|------|---------|-------------|
| RACK | 16-slot grid, live peak meters, tap to focus+navigate | nativePollPeaks, nativeGetSlotInfo |
| CHANNEL | Focused ch detail: volume slider, mute/solo, instrument picker | nativeGetSlotVolume, nativeGetSlotTypeName |
| VIZ | Fullscreen Canvas: scope waveform + spectrum bars + channel dots | nativeGetScope, nativePollPeaks |

Swipe left/right via `HorizontalPager`. Tapping a slot in RACK focuses it and swipes to CHANNEL.

## Audio Path

AAudio pull callback → `render_mix()` → float32 stereo output.
- Performance mode: LOW_LATENCY, exclusive sharing
- No int16 conversion (unlike Linux ALSA path)
- Expected ~10ms latency on Galaxy S10 / Pixel 2 XL

## HTTP Server (Remote Control)

The C HTTP server still runs on port 8080 for **remote control from other devices** on the LAN. The local UI does NOT use it — everything goes through JNI.

## Build

```bash
cd android
./build-android.sh              # build debug APK
./build-android.sh --install    # build + adb install + launch
```

Requires: Android Studio with NDK 26.x, or standalone SDK/NDK. Generate gradle wrapper first if needed.

## What's Next

- **Key/chord/voicing modes** — native UI for scale programming (user has UX ideas)
- **Per-instrument param screens** — parse `nativeGetChannelJson` into Compose controls
- **Touch keyboard** — on-screen note input via `nativeNoteOn/Off`
- **Config screen** — sample rate, period size, audio device info
- **Bluetooth MIDI** support
- **Hardware volume buttons** → master volume
