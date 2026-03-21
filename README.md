# miniwave

Modular synth rack for waveOS. 16 channel slots, 6 pluggable synthesis engines, UDP OSC + HTTP/SSE dual control surfaces, expression-driven sequencing, and a built-in browser UI.

```
┌─────────────────────────────────────────────┐
│  miniwave                                   │
│  ┌──────┐ ┌──────┐ ┌──────┐     ┌──────┐   │
│  │ ch 1 │ │ ch 2 │ │ ch 3 │ ... │ch 16 │   │
│  │ FM   │ │ Sub  │ │ PD   │     │ Add  │   │
│  └──┬───┘ └──┬───┘ └──┬───┘     └──┬───┘   │
│     └────┬───┴────┬───┘      ┌─────┘        │
│      mixer → limiter → output               │
│   ALSA / CoreAudio / JACK / shared mem bus   │
│       ↕ OSC :9000   ↕ HTTP/SSE :8080        │
├─────────────────────────────────────────────┤
│  WaveUI (browser :8080)                     │
│  rack view → channel strips → detail panes  │
└─────────────────────────────────────────────┘
```

## Quick Start

**macOS:**

```bash
cd macos
make
./miniwave -c 4          # WaveUI at http://localhost:8080
```

**Linux:**

```bash
cd linux
make
./miniwave -c 4          # WaveUI at http://localhost:8080
```

No external dependencies beyond platform audio/MIDI libraries. All synthesis, HTTP serving, OSC handling, and expression evaluation are built into the headers.

## Instruments

| Type | Voices | Description |
|------|--------|-------------|
| `fm-synth` | 16 | 2-operator FM synthesis, 99 presets, ADSR, velocity tracking, feedback |
| `sub-synth` | 2 | Subtractive with 6 waveforms (saw/square/pulse/tri/sine/noise), state-variable filter, dual envelopes |
| `fm-drums` | 12 | Per-MIDI-note FM percussion engine, 16 drum presets, pitch sweep, click/noise mix |
| `ym2413` | 9 | Cycle-accurate OPLL emulator, 15 ROM patches + custom, 5-voice rhythm mode |
| `phase-dist` | 8 | Casio CZ-style phase distortion, 6 modes (resonant/saw/pulse/cosine/sync/wavefold) |
| `additive` | 8 | 6 partial generation modes (harmonic/cluster/formant/metallic/noise/expression), wavetable baking |

The instrument interface is intentionally small: lifecycle, MIDI, render, status, persistence, and optional OSC/param hooks. All instruments are hot-swappable per slot at runtime.

## Architecture

Two independent control surfaces drive the same rack:

- **UDP OSC** on port `9000` — rack control, channel params, MIDI device selection, external integrations
- **HTTP + SSE** on port `8080` — WaveUI, rack state snapshots, live updates, API control

The browser UI is optional. Any OSC client can drive the rack directly.

**Audio pipeline:** MIDI input → slot layer (KeySeq/MiniSeq intercept) → instrument render → per-slot volume/mute/solo → mixer → soft-knee limiter → output (ALSA / CoreAudio / JACK / waveOS bus)

**Sample rate:** 48 kHz stereo. Default period size: 64 samples.

## Sequencing

Sequencing is slot-owned, not instrument-owned. Any synth engine can be driven by either sequencer:

- **MiniSeq** — step sequencer with a compact DSL for note patterns
- **KeySeq** — expression-driven arpeggiator/sequencer with a full evaluation engine

### KeySeq Expression Language

KeySeq supports a complete expression language with variables, operators, functions, and Perlin noise for generative sequencing.

**Variables:** `n` (note), `v` (velocity), `t` (step time), `g` (gate), `i` (step index), `root`, `rv` (root velocity), `time`, `bu` (beat unit), `gate` (gate position), `held`, `dt`

**Functions:** `sin`, `cos`, `abs`, `rand`, `noise` (Perlin), `noiseb` (bipolar), `if`, `floor`, `ceil`, `min`, `max`, `clamp`, `step`, `smoothstep`

**DSL example:**

```
t0.3; g0.5; algo; n:n+sin(i*0.5)*12; v:v*0.9; end:i>=16; seed:n
```

**Modes:**
- **Offsets** — fixed interval patterns (e.g., `0,12,7` for root/octave/fifth)
- **Algo** — per-step expressions for note, velocity, timing, gate, and instrument params

Frame-level modulation (`frame:<expr>`) enables per-sample pitch/param control for vibrato, bends, and LFO-like effects.

Full spec: [common/web/KEYSEQ_SPEC.md](common/web/KEYSEQ_SPEC.md)

## Endpoints

| Path | Description |
|------|-------------|
| `/` | WaveUI — rack view, channel strips, detail panes, virtual keyboard |
| `/keyseq-test` | KeySeq editor and preview workbench |
| `/osc-spec` | OSC endpoint reference (JSON) |
| `/api-help` | Dynamic API reference for LLMs and tooling |
| `/events` | SSE stream — rack status, channel updates, MIDI events |
| `POST /api` | JSON control API — keyseq_dsl, keyseq_preview, note_on/off, rack_status, midi/device |

## State Persistence

Rack state auto-saves to `~/.config/miniwave/rack.json` on changes and restores on startup.

Saved state includes: master volume, MIDI device, per-slot instrument type and params, volume/mute/solo, MiniSeq DSL, KeySeq DSL, and global BPM.

## CLI

```
miniwave [options]
  -m DEV    MIDI input device (default: auto-detect)
  -o DEV    Audio output device (default: hw:0,0 / system default)
  -c N      Pre-configure channels 1-N with FM synth
  -O PORT   OSC port (default: 9000, 0 to disable)
  -P SIZE   Audio period size (default: 64)
```

## Platform Requirements

**macOS:** Clang, CoreMIDI, CoreAudio, AudioToolbox frameworks (ships with Xcode CLI tools)

**Linux:** GCC, ALSA (`libasound-dev`). Optional: PipeWire JACK for preferred audio backend.

## waveOS Integration

miniwave writes to the shared memory instrument bus (`/waveos-bus`) when available. On waveOS hardware, WaveLoop X1 reads the bus and mixes instrument audio into the input stream for recording/looping. Multicast relay on port `9001` (group `239.0.0.42`) broadcasts note events across the network.

## License

MIT
