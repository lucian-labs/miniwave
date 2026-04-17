# CLAUDE.md — miniwave

## Project

C header-only modular synth rack. 16 channel slots, 7 instruments, UDP OSC + HTTP/SSE + Web MIDI. Runs on Pi Zero 2W (`waveloop@pocketwave.local`).

## Build

```bash
cd linux && make          # local build
./deploy-pi.sh            # full pipeline: lint → build → sync → rebuild on Pi → restart → validate
```

## Deploy target

**pocketwave.local** (Pi Zero 2W, `waveloop@192.168.0.112`). NOT waveloop.local (Pi 4).
miniwave runs as a systemd service (`miniwave.service`), auto-starts on boot.

## Web UI Architecture

**SPA shell** — `index.html` is the outer shell with persistent header (nav tabs, ch nav, BPM, master vol, Web MIDI selector). All other pages load inside an `<iframe id="view">`. Web MIDI connection persists across tab switches.

**Pages** (all served from `common/web/`, routes in `server.h`):
- `/` — SPA shell (index.html)
- `/rack?mode=detail` — synth detail for focused channel (rack.html)
- `/rack?mode=grid` — 16-channel grid view (rack.html)
- `/keyseq` — keyseq expression editor (keyseq.html)
- `/effex` — effects chain placeholder (effex.html)
- `/chords` — scale + chord programmer (chords.html)
- `/viz` — fullscreen audio visualization (viz.html)
- `/config` — system dashboard (config.html)
- `/touch` — mobile swipe UI (touch.html, standalone, not in iframe)

## UI Update Rules

**CRITICAL: When modifying any web UI page that loads inside the SPA iframe:**

1. **No duplicate headers** — pages inside the iframe must NOT have their own nav/header. The shell provides the header. If a page has `<div id="header">`, remove it (or hide it) when loaded in iframe context.

2. **No duplicate SSE connections** — each iframe page creates its own EventSource to `/events`. This is fine (server supports multiple SSE clients). But don't create SSE in both the shell AND the iframe for the same purpose.

3. **No duplicate MIDI handling** — Web MIDI lives in the shell only (`index.html`). Iframe pages should NOT call `navigator.requestMIDIAccess()`. MIDI device selection is shell-level.

4. **Element ID conflicts** — the shell and iframe are separate documents, so IDs don't conflict. But if a page is ever loaded standalone (not in iframe), it needs its own SSE connection.

5. **focused_ch** — the shell tracks `focused_ch` from SSE and provides ch-nav buttons. Iframe pages should also follow `focused_ch` from their own SSE connection for consistency.

6. **Lint before deploy** — `node common/web/lint.js` catches:
   - Syntax errors (blocks deploy)
   - Unguarded DOM access (`getElementById('x').foo` without `?.` or null check)
   - Top-level duplicate declarations
   Run it. Fix warnings. All `getElementById` calls must use `?.` for method calls or `if (el)` for assignments.

7. **Version** — bump `MINIWAVE_VERSION` in `common/rack.h` for each deploy. The shell displays it from `rack_status` SSE.

8. **Color palette**:
   ```
   --bg: #0d0d0d; --panel: #111; --card: #0e1525;
   --accent: #e94560; --good: #00d4aa; --text: #eee; --dim: #aaa;
   --border: #2a2a4a;
   ```

9. **Shadow system** — all cards/sections use:
   ```
   --hw-shadow (main), --hw-shadow-sm (small), --hw-pressed (active state)
   ```

10. **Static file routes** — to serve a new file from `common/web/`, add the path to the `strcmp` chain in `server.h`'s HTTP handler. For HTML pages, follow the existing pattern (fopen/fread/send). For assets (js/png/svg/json), add to the static file handler list.

## API

All via `POST /api` with JSON body `{type:"...", ...}`.

Key endpoints: `rack_status`, `ch_status`, `note_on`, `note_off`, `midi_raw`, `slot_set`, `slot_clear`, `keyseq_dsl`, `keyseq_enable`, `set_scale`, `set_chord`, `scale_program`, `focus_ch`, `bpm`, `master_volume`, `midi_device`, `scope`, `gain_test`, `peak_reset`, `panic`, `quit`, `shutdown`.

## SSE Events

`GET /events` — Server-Sent Events stream.
Events: `rack_status`, `ch_status`, `rack_types`, `midi_devices`, `midi_events`.

## Instruments

fm-synth (idx 0), ym2413 (1), sub-synth (2), fm-drums (3), additive (4), phase-dist (5), bird (6).

Type index order matters — `rack.h` render_mix uses a switch on `slot->type_idx` for cents_mod dispatch.

## Audio

48kHz stereo, period 64 samples (1.3ms), 4-period ALSA buffer. SCHED_FIFO priority 50. Render loop in `linux/miniwave.c`, mix in `common/rack.h:render_mix()`.

Scope buffer: 256 stereo frames captured per render cycle, served via `{type:"scope"}` API for FFT/viz.
