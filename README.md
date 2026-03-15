# miniwave

Modular synth rack for waveOS. 16 MIDI channels, pluggable instruments, OSC-native, browser UI.

```
┌─────────────────────────────────────────────┐
│  miniwave                                   │
│  ┌──────┐ ┌──────┐ ┌──────┐     ┌──────┐   │
│  │ ch 1 │ │ ch 2 │ │ ch 3 │ ... │ch 16 │   │
│  │ FM   │ │ FM   │ │(none)│     │(none)│   │
│  └──┬───┘ └──┬───┘ └──────┘     └──────┘   │
│     └────┬───┘                              │
│       mixer → ALSA / shared mem bus         │
│         ↕ OSC :9000                         │
├─────────────────────────────────────────────┤
│  WaveUI (browser :8080)                     │
│  rack view → channel strips → detail panes  │
└─────────────────────────────────────────────┘
```

## Quick Start (Linux)

```bash
cd linux
make
./miniwave -c 4          # FM synth on channels 1-4
node web/bridge.js       # WaveUI at http://localhost:8080
```

## Architecture

**Everything is OSC.** miniwave exposes its full state via UDP OSC on port 9000. The web UI is optional — any OSC client works. Build your own UI. Vibe code it. That's the point.

OSC spec: `http://localhost:8080/osc-spec`

## Instruments

| Type | Name | Description |
|------|------|-------------|
| `fm-synth` | FM Synth (yama-bruh) | 2-operator FM, 99 presets, 16 voices, ADSR |

More coming. The instrument interface is simple — implement init/destroy/midi/render/osc.

## CLI

```
miniwave [options]
  -m DEV    ALSA MIDI input (default: auto-detect)
  -o DEV    ALSA audio output (default: hw:0,0)
  -c N      Pre-configure channels 1-N with FM synth
  -O PORT   OSC port (default: 9000, 0 to disable)
  -P SIZE   Audio period size (default: 64)
```

## WaveUI

Browser control surface at port 8080. Rack view with 16 channel strips, click into any channel for full instrument control — presets, parameter sliders, virtual keyboard.

**WaveUI is an OSC protocol, not just a UI.** The spec is open so anyone can build their own control surface.

## waveOS Integration

miniwave writes to the shared memory instrument bus (`/waveos-bus`) when available. On the Pi, WaveLoop X1 reads the bus and mixes instrument audio into the input stream for recording/looping.

## License

MIT
