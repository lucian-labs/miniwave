# TODO

## Slot Lifetime Follow-Ups

- Wrap the initial `/events` `rack_status` snapshot in `slot_read_begin()` / `slot_read_end()`.
  Current gap: [common/server.h](/Users/elijahlucian/repos/miniwave/common/server.h#L1289) builds and sends the first rack snapshot on SSE connect without entering the reader protocol.
  Expected result: slot churn during a new SSE connection cannot race the initial snapshot builder.

- Wrap autosave `state_save()` in the reader protocol, or otherwise guarantee it runs under the same slot-lifetime protection as other rack readers.
  Current gap: [common/server.h](/Users/elijahlucian/repos/miniwave/common/server.h#L1398) calls `state_save()` without `slot_read_begin()` / `slot_read_end()`, while [common/rack.h](/Users/elijahlucian/repos/miniwave/common/rack.h#L373) walks `slot->state`, `slot->keyseq`, and `slot->seq`.
  Expected result: autosave cannot race concurrent slot teardown from other threads.

## Scale + Chord System (per-channel, persisted)

### Scale Mode
- Activate via CC/pad → play root note → tap intervals from C (white keys = major, add accidentals)
- Stored per slot, persisted in rack.json
- "From C" paradigm: user always programs relative to C, system transposes to active root

### Chord Shape Definition
- Define shapes as intervals from C (e.g. C-E-G = major triad = [0,4,7])
- Stored per channel alongside scale
- Single key triggers chord shape transposed to nearest scale degree
- Knob control TBD (Elijah thinking about ergonomics)

### Arpeggiator (future)
- Record sequences using "from C" input
- Playback follows active scale + chord context
- Modes: up, down, up-down, random, as-played

### Chord Sequencer Mode (future)
- New keyseq mode alongside `algo` and `offsets`
- Held chord treated as a unit — all notes move together or arpeggio through them

## Arturia MiniLab Integration
- Writable screen + controllable pad colors (protocol at least partially documented)
- Research SysEx/protocol for screen writes and pad RGB control
- Use as primary visual-feedback controller for scale/chord/arp workflow
