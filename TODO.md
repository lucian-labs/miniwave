# TODO

## Slot Lifetime Follow-Ups

- Wrap the initial `/events` `rack_status` snapshot in `slot_read_begin()` / `slot_read_end()`.
  Current gap: [common/server.h](/Users/elijahlucian/repos/miniwave/common/server.h#L1289) builds and sends the first rack snapshot on SSE connect without entering the reader protocol.
  Expected result: slot churn during a new SSE connection cannot race the initial snapshot builder.

- Wrap autosave `state_save()` in the reader protocol, or otherwise guarantee it runs under the same slot-lifetime protection as other rack readers.
  Current gap: [common/server.h](/Users/elijahlucian/repos/miniwave/common/server.h#L1398) calls `state_save()` without `slot_read_begin()` / `slot_read_end()`, while [common/rack.h](/Users/elijahlucian/repos/miniwave/common/rack.h#L373) walks `slot->state`, `slot->keyseq`, and `slot->seq`.
  Expected result: autosave cannot race concurrent slot teardown from other threads.

## Chord Sequencer Mode
- New keyseq mode alongside `algo` and `offsets`
- Held chord treated as a unit — all notes move together or arpeggio through them
- Two potential flavors:
  1. **Chord algo**: algo expression transforms the entire held chord (transpose, invert, spread)
  2. **Arpeggiator**: cycle through held notes on a shared clock (up, down, up-down, random, as-played)
- Needs design input from Elijah on exact behavior
