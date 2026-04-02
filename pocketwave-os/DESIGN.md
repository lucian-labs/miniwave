# PocketWave OS — Design Notes

## Vision

A lightweight Linux OS image for cheap ARM handhelds that turns them into
portable sound devices. Users browse a web repository of community-made
instruments — synths, samplers, noisemakers, drum machines, generative
things, weird vibe-coded audio toys — and load them onto their device.

Think: app store for handheld sound toys, running on $40-60 hardware.

## Target hardware (phase 1)

**Allwinner H700 devices** — established Linux CFW community, known boot chain.

| Device | Wi-Fi | Price | Notes |
|--------|-------|-------|-------|
| RG35XX Plus | Yes | ~$60 | Primary dev target |
| RG35XX H | Yes | ~$60 | Horizontal form factor |
| RG35XX SP | Yes | ~$65 | Clamshell |
| RG35XX OG | No | ~$55 | Viable but no repo access without dongle |

All share the same SoC, same kernel, same ALSA codec path. One OS image,
multiple device configs for display/input layout.

## Evaluated and rejected (for now)

| Device | Why not |
|--------|---------|
| MagicX Mini Zero 28 V2 | Android, unknown bootloader, A133P Linux support unproven |
| Labists Smart Mirror HM1 | Just a Pi in a box, not portable |

## OS image structure

```
/pocketwave/
  boot/           # kernel, dtb, u-boot
  rootfs/          # minimal Linux (busybox or Alpine-based)
  instruments/     # downloaded instrument packages
  config/          # device config, Wi-Fi, audio routing
```

Flash to SD card, boot directly into pocketwave. No emulator frontend,
no Android, no desktop environment.

### Base system

- Minimal Linux userland (Alpine or buildroot)
- ALSA audio (no PulseAudio/PipeWire bloat)
- Wi-Fi (wpa_supplicant)
- USB OTG (MIDI controllers)
- Framebuffer UI (no X11 if we can avoid it)
- miniwave as the audio engine

## Instrument package format

An instrument is a directory or archive that contains everything needed
to make sound on the device.

```
my-instrument/
  manifest.json    # name, author, description, tags, controls mapping
  instrument.wasm  # or .so, or .pd, or .lua — TBD
  samples/         # optional sample data
  ui.json          # optional UI layout for the device screen
  preview.mp3      # optional audio preview for the repo browser
```

### Runtime options (pick one or support multiple)

| Runtime | Pros | Cons |
|---------|------|------|
| **Native .so** (C, compiled for ARM) | Fast, low latency, full control | Must cross-compile, unsafe |
| **WASM** (sandboxed) | Safe, portable, one binary works everywhere | Needs a WASM audio runtime, perf overhead |
| **Lua scripts** | Easy to write, safe-ish | Slower for heavy DSP |
| **Pure Data patches** (.pd) | Visual patching community already exists | Needs libpd, heavier |
| **miniwave DSL** | Already exists (KeySeq expressions) | Limited to what the expression engine supports |

Recommendation: **WASM first.** yama-bruh already proves this works — it's
a full FM synth (YM2413, 99 presets) compiled from Rust to wasm32. The
instrument authoring story is: write in Rust/C/Zig, compile to WASM,
upload to repo. Safe, portable, one binary runs on any pocketwave device.
Add native .so escape hatch later for latency-critical stuff.

## Instrument API (sketch)

```c
// every instrument implements this interface
typedef struct {
    const char *name;
    void (*init)(float sample_rate);
    void (*note_on)(int note, int velocity);
    void (*note_off)(int note);
    void (*render)(float *left, float *right, int frames);
    void (*set_param)(int id, float value);
    void (*destroy)(void);
} pocketwave_instrument_t;
```

This is basically what miniwave's instrument interface already is.
Instrument .so files export a `pocketwave_register()` function that
returns this struct.

## Repository

A simple web service. Doesn't need to be fancy.

```
GET  /api/instruments              # list/search
GET  /api/instruments/:id          # metadata + download URL
GET  /api/instruments/:id/preview  # audio preview
POST /api/instruments              # upload (authenticated)
```

On-device, a simple TUI browser:
- Browse/search instruments over Wi-Fi
- Preview audio
- Download to /pocketwave/instruments/
- Load into a miniwave slot

## Device UI

Framebuffer-based (like pocketwave-ui on PocketCHIP), adapted per device:

- **Instrument browser** — browse repo, download, manage local instruments
- **Rack view** — which instruments are loaded in which slots
- **Play mode** — buttons mapped to notes/params, screen shows instrument UI
- **Config** — Wi-Fi setup, audio routing, MIDI mapping

Button mapping varies per device form factor but the UI is the same.

## Related projects

### miniwave (this repo)
- The audio engine — 6 synth types, ALSA/CoreAudio, OSC + HTTP control
- `pocketchip/pocketwave-ui` — terminal UI over OSC, proof of concept
- `pocketchip/os/` — systemd services, udev rules, install script
- Instrument interface in `common/instruments.h`

### yama-bruh (lucian-labs/yama-bruh)
- WASM FM synth — YM2413 engine, 99 presets, 10 moods, 39 scales
- Rust → wasm32-unknown-unknown compilation pipeline
- Already runs in browser via Web Audio API
- Proves the WASM instrument model works
- The FM engine and presets could be a built-in pocketwave instrument

### waveloop (lucian-labs/waveloop)
- TODO: couldn't access (private?) — need description from Elijah

## Open questions

- WASM vs Lua vs native for community instruments — security vs perf vs ease
- How to handle instruments that need samples (storage, download size)
- Versioning / updating instruments
- Can we support devices beyond Allwinner H700 without maintaining N kernels?
- Revenue model? Free? Donations? Paid premium instruments?
- Name: "PocketWave OS" or something else?
