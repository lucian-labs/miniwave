# KeySeq Expression Language & API Spec

## Expression Language

### Variables
| Name | Description | Range |
|------|------------|-------|
| `n` | Current MIDI note (float, fractional = microtonal) | 0-127 |
| `v` | Current velocity | 0-1 |
| `t` | Current step time (beats) | >0 |
| `g` | Current gate ratio (fraction of step time) | 0+ (1 = full step) |
| `i` | Step index (0 = root note) | 0+ |
| `root` | Original triggered MIDI note | 0-127 |
| `rv` | Original trigger velocity (normalized) | 0-1 |
| `time` | Seconds since sequence started | 0+ |
| `bu` | Normalized beat position within step | 0-1 |
| `gate` | Normalized position within gate period | 0-1 |
| `held` | Root key still held | 0 or 1 |
| `dt` | Seconds per sample (1/sampleRate) | ~0.00002 |

### Constants
| Name | Value |
|------|-------|
| `pi` | 3.14159... |
| `tau` | 6.28318... |

### Operators
`+` `-` `*` `/` `%` `>` `<` `>=` `<=`

### Functions
| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `noise(x)` | 1 | 0-1 | Perlin noise (1D diagonal) |
| `noise(x,y)` | 2 | 0-1 | Perlin noise 2D |
| `noise(x,y,z)` | 3 | 0-1 | Perlin noise 3D |
| `noiseb(x)` | 1 | -1 to 1 | Perlin noise bipolar |
| `noiseb(x,y)` | 2 | -1 to 1 | Perlin noise 2D bipolar |
| `noiseb(x,y,z)` | 3 | -1 to 1 | Perlin noise 3D bipolar |
| `sin(x)` | 1 | -1 to 1 | Sine |
| `cos(x)` | 1 | -1 to 1 | Cosine |
| `abs(x)` | 1 | 0+ | Absolute value |
| `rand()` | 0 | 0-1 | Deterministic PRNG (seeded) |
| `if(c,a,b)` | 3 | a or b | Ternary: c != 0 ? a : b |
| `floor(x)` | 1 | int | Floor |
| `ceil(x)` | 1 | int | Ceiling |
| `min(a,b)` | 2 | num | Minimum |
| `max(a,b)` | 2 | num | Maximum |
| `clamp(x,lo,hi)` | 3 | num | Clamp x to [lo, hi] |
| `step(edge,x)` | 2 | 0 or 1 | 0 if x < edge, else 1 |
| `smoothstep(e0,e1,x)` | 3 | 0-1 | Hermite interpolation |

---

## DSL Format

Semicolon-delimited tokens. Expressions can contain spaces.

```
t0.3; g0.41; gated; algo; n:n + noise(i) * 12; v:v * 0.9; end:i >= 12; seed:n
```

### Tokens
| Token | Description |
|-------|------------|
| `t<beats>` | Step time in beats (default 0.125) |
| `g<ratio>` | Gate as ratio of step time (default 1.0, >1 = overlap) |
| `gated` | Stop sequence on key release |
| `loop` | Loop offsets (offsets mode only) |
| `algo` | Enable algorithm mode (vs offsets mode) |
| `n:<expr>` | Note expression (algo mode) |
| `v:<expr>` | Velocity expression (algo mode) |
| `t:<expr>` | Step time expression (algo mode) |
| `g:<expr>` | Gate expression (algo mode) |
| `end:<expr>` | End condition (nonzero = stop) |
| `seed:<expr>` | Seed expression (evaluated at note-on, `v` = raw MIDI velocity 0-127) |
| `frame:<expr>` | Per-sample cents modulation |
| `<name>:<expr>` | Param bus (routes to instrument osc_handle) |
| `frame_<name>:<expr>` | Per-sample param modulation |
| `<ints>` | Offsets mode: comma-separated semitones (e.g. `0,12,7,12`) |
| `v<floats>` | Offsets mode: comma-separated velocity levels (e.g. `v1,0.7,0.5`) |

### Behavior
- Step 0 = root note (unmodified), algo starts at step 1
- Gate is a ratio of step time: `g0.5` = note plays for 50% of step, `g1` = full step, `g2` = 2x step (overlap)
- Previous note is always released before new note fires
- End condition releases last note on termination
- Seed: empty = random per note-on (clock-based), expression = deterministic from note/velocity
- `noise()` seed is set per-sequence from the runtime seed

---

## HTTP API

All endpoints: `POST /api` with `Content-Type: application/json`

### `keyseq_dsl` — Load keyseq on a channel
```json
{ "type": "keyseq_dsl", "channel": 0, "dsl": "t0.3; algo; n:n+1; v:v-0.03; end:v<=0" }
```
Response: `{ "ok": true, "steps": 1 }`

### `keyseq_preview` — Get computed preview from channel state
Server is source of truth. No DSL in request — reads from whatever is loaded on the channel.
```json
{ "type": "keyseq_preview", "channel": 0, "note": 60, "velocity": 100 }
```
Response:
```json
{
  "channel": 0,
  "dsl": "t0.3; algo; n:n+noise(i)*12; v:v; end:i>=12; seed:n",
  "seed": 159266144,
  "bpm": 120.0,
  "algo": 1,
  "gated": 0,
  "loop": 0,
  "step_beats": 0.300,
  "gate_beats": 1.000,
  "steps": [
    { "i": 0, "n": 60.00, "midi": 60, "v": 0.7874, "t": 0.300, "g": 1.000, "cents": 0.0, "beat": 0.000 },
    { "i": 1, "n": 61.19, "midi": 61, "v": 0.7874, "t": 0.300, "g": 1.000, "cents": 19.0, "beat": 0.300 }
  ],
  "end_step": 12,
  "end_reason": "i >= 12",
  "total_beats": 3.900,
  "total_steps": 13
}
```

### `keyseq_stop` — Stop and disable keyseq on a channel
```json
{ "type": "keyseq_stop", "channel": 0 }
```

### `note_on` / `note_off` — Trigger notes (activates keyseq if loaded)
```json
{ "type": "note_on", "channel": 0, "note": 60, "velocity": 100 }
{ "type": "note_off", "channel": 0, "note": 60 }
```

---

## SSE Events

Connect to `/events` (EventSource).

| Event | Description |
|-------|------------|
| `hello` | `{ "client_id": N }` — use in `/api/detail` requests |
| `rack_status` | Full rack state (slots, volumes, MIDI, bus, etc.) |
| `rack_types` | Available instrument types |
| `midi_devices` | MIDI device list |
| `ch_status` | Channel detail (instrument-specific params) |
| `keyseq_trigger` | `{ "type": "keyseq_trigger", "dsl": "..." }` — fired on MIDI note-on that activates a keyseq |

---

## Workflow

1. Send `keyseq_dsl` to load/update algo on a channel
2. Send `keyseq_preview` to get computed graph (debounce ~150ms on field changes)
3. On field change: send `keyseq_dsl` first (updates server), then `keyseq_preview` (refreshes preview)
4. MIDI controller notes automatically use the latest loaded keyseq
5. Listen for `keyseq_trigger` SSE event for visualizer updates

## Instrument Params (for param bus)

### fm-synth
`carrier_ratio` `mod_ratio` `mod_index` `attack` `decay` `sustain` `release` `feedback`

### sub-synth
`waveform` `pulse_width` `filter_cutoff` `filter_reso` `filter_env_depth` `filt_attack` `filt_decay` `filt_sustain` `filt_release` `amp_attack` `amp_decay` `amp_sustain` `amp_release`

### ym2413
`instrument` `volume` `rhythm` `vibrato` `portamento` `pitchbend`

### fm-drums
`carrier_freq` `mod_freq` `mod_index` `pitch_sweep` `pitch_decay` `decay` `noise_amt` `click_amt` `feedback`
