# Viz Page Handoff

## Goal
Create `/viz` — a fullscreen, landscape-optimized audio visualization page for miniwave. Pure eye candy, no controls. Optimized for social media capture (screen recording on a phone held sideways).

## Data Sources

### Scope buffer (audio waveform)
- **Endpoint**: `POST /api` with `{"type":"scope"}`
- **Returns**: `{"samples":[...]}` — 512 floats, interleaved L/R (256 stereo frames)
- **Poll rate**: 20-30fps via `fetch`
- **Sample rate**: 48kHz, so 256 frames = ~5.3ms of audio per buffer

### Peak levels (per-channel + master)
- **Source**: SSE event `rack_status`
- **Fields**: `slots[].pk[L,R]`, `slots[].hold[L,R]`, `master_pk[L,R]`, `master_hold[L,R]`
- **Update rate**: ~10-20Hz via SSE

### MIDI events (notes, CCs)
- **Source**: SSE event `midi_events`
- **Fields**: `events[]` — arrays of `[status, d1, d2]`
- **Note on**: status & 0xF0 == 0x90 && d2 > 0
- **Note off**: status & 0xF0 == 0x80 || (0x90 && d2 == 0)
- **CC**: status & 0xF0 == 0xB0
- **Channel**: status & 0x0F

### Rack state
- **Source**: SSE event `rack_status`
- **Fields**: `focused_ch`, `bpm`, `slots[].type`, `slots[].active`, `version`, `hostname`

## Architecture
- Single HTML file at `/home/elijah/miniwave/common/web/viz.html`
- Route already pattern-matched in server.h — just add to the config/chords handler chain
- No external dependencies, self-contained
- Canvas or WebGL (WebGL preferred for performance)
- Landscape orientation: `@media (orientation: portrait)` show a "rotate device" message

## Visual Ideas (pick and combine)

### FFT Spectrum
- Frequency bars or smooth curve from the scope buffer
- DFT of the mono mix (average L+R)
- Color-mapped: low freq = warm (red/orange), high freq = cool (blue/purple)
- Bars could pulse/glow with intensity

### Waveform Oscilloscope
- Real-time waveform trace from scope buffer
- Neon glow effect (multiple passes with blur)
- Color follows the focused instrument's accent

### Note Rain / Piano Roll
- MIDI notes fall or rise across the screen
- Horizontal = pitch (C0-C8), vertical = time
- Color per channel
- Velocity = brightness/size

### Channel Meters
- 16 vertical bars, one per slot
- Pulsing glow proportional to peak level
- Inactive slots dim, focused slot highlighted

### Particle System
- MIDI note-on spawns particles
- Velocity = initial speed, pitch = color, channel = spawn position
- Particles drift and fade with physics

### Background
- Subtle grid or radial gradient
- BPM-synced pulse (global brightness modulation at beat intervals)

## Color Palette
```
--bg: #0d0d0d
--panel: #111
--card: #0e1525
--accent: #e94560
--good: #00d4aa
--text: #eee
--dim: #aaa
--border: #2a2a4a
```

Channel colors (suggestion):
```
const CH_COLORS = [
  '#e94560', '#00d4aa', '#8866cc', '#ff8800',
  '#00aaff', '#ffd700', '#ff66aa', '#66ffcc',
  '#aa44ff', '#ff4400', '#44ddff', '#aaff00',
  '#ff88cc', '#44ff88', '#ffaa44', '#88aaff'
];
```

## API helper
```javascript
const api = b => fetch('/api', {method:'POST', body:JSON.stringify(b)}).then(r=>r.json());
```

## SSE connection
```javascript
const es = new EventSource('/events');
es.addEventListener('rack_status', e => { ... });
es.addEventListener('midi_events', e => { ... });
es.addEventListener('ch_status', e => { ... });
```

## Lint rules
- All DOM access must use `?.` or `if (el)` guards
- No `getElementById('x').addEventListener` — use `getElementById('x')?.addEventListener`
- Run `node /home/elijah/miniwave/common/web/lint.js` to verify
- Must pass with 0 errors (warnings OK but minimize)

## Route setup
Add to server.h in the static file handler chain (same pattern as `/config`, `/chords`):
```c
strcmp(path, "/viz") == 0
```
Serve `web/viz.html`.

## File location
`/home/elijah/miniwave/common/web/viz.html`

## Style reference
Look at `/home/elijah/miniwave/common/web/touch.html` and `/home/elijah/miniwave/common/web/config.html` for the design language. Hardware shadow system, dark cards, accent glow.

## Performance target
- 60fps on a mid-range phone (Galaxy S10, Pixel 2 XL)
- Scope poll at 20fps, render at 60fps (interpolate between scope updates)
- Keep draw calls minimal — batch geometry, use WebGL if doing particles
