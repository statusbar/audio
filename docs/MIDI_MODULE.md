[← back to module index](README.md)

# midi

MIDI 1.0 primitives in a single namespace: a fixed-size `MidiMessage` value
type with status/meta constants, a byte-by-byte stream parser that handles
running status and sysex framing, Standard MIDI File (SMF) Type 0 and Type 1
read/write, a read-process-write pipeline, a note-on bookkeeping matrix, and a
ready-made callback set that prints a human-readable transcript.

## Overview

The module is layered so callers can pull in only what they need; the
`midi.hpp` module header includes everything.

1. **Wire-level types.** `midi_types.hpp` defines `MidiMessage` (three bytes,
   stored by value) plus the `status::` and `meta::` byte constants and
   `data_byte_count()` (the canonical table mapping a status byte to its
   payload length). `midi_sysex.hpp` adds `SysexMessage<MaxSize>` (a
   fixed-capacity owning buffer used by the live parser) and `SysexEvent` (a
   non-owning view used by the file reader). `midi_meta.hpp` adds `MetaEvent`
   with typed accessors for tempo (microseconds-per-beat and BPM), text,
   time signature, and key signature.
2. **Stream parser.** `MidiParser<MaxSysex>` is a small state machine driven
   one byte at a time. It tracks running status, holds an embedded
   `SysexMessage<MaxSysex>` for `F0 … F7` accumulation, and delivers
   realtime bytes (`>= 0xF8`) immediately so they can interleave inside
   other messages without disrupting state. Callbacks are
   `sg14::inplace_function`s with a 64-byte capture buffer — no heap.
3. **SMF file I/O.** `read_midi_file()` parses an entire SMF Type 0 or
   Type 1 file from a single `std::span<uint8_t const>` and dispatches
   header, per-track, message, sysex, and meta callbacks. `MidiFileWriter`
   appends a Type 0 or Type 1 file into a caller-owned `MutableBuffer`,
   handling delta-time encoding, running status, and back-patching the
   `MTrk` length when `end_track()` is called.
4. **Processing.** `process_midi_file()` chains a reader and writer, calling
   optional per-event lambdas in between; unset lambdas are pass-through.
5. **State and inspection.** `MidiMatrix` keeps per-channel/per-note
   reference counts and damper-pedal state so a renderer can answer "is this
   note still held?". `make_fileshow_callbacks(FILE*)` builds a
   `MidiFileCallbacks` set that prints every event to a `FILE*`.

The reader operates on a single in-memory span — it does **not** stream from
a `FILE*` or `std::istream`. Likewise the writer appends into a caller-owned
`MutableBuffer`; flushing that buffer to disk is the caller's job. Callbacks
deliver sysex bodies and meta payloads as `std::span<uint8_t const>` views
into the caller's input buffer, so they are valid only for the duration of
the callback. `MidiParser` and `MidiMessage` are allocation-free; the file
reader/writer are allocation-free at the codec layer (any allocation comes
from the caller-provided buffer or the inplace-function callbacks). Format 2
SMF files are not supported.

## Key types

- `MidiMessage` — three-byte value (`status`, `data1`, `data2`) with channel/note/velocity/controller/program accessors, `pitch_bend()` decode, `is_note_on()` / `is_note_off()` / `is_channel_message()` / `is_system_message()` / `is_realtime()` predicates, and `note_on()` / `note_off()` / `control_change()` / `program_change()` / `pitch_bend()` / `channel_pressure()` / `poly_pressure()` `static` factory functions.
- `status::` constants (`note_on`, `note_off`, `control_change`, `program_change`, `pitch_bend`, `channel_pressure`, `poly_pressure`, `sysex_start`, `sysex_end`, `mtc_quarter`, `song_position`, `song_select`, `tune_request`, `timing_clock`, `midi_start` / `midi_continue` / `midi_stop`, `active_sense`, `system_reset`) — the channel-message high nibbles and system status bytes.
- `meta::` constants (`sequence_number`, `text`, `copyright`, `track_name`, `instrument_name`, `lyric`, `marker`, `cue_point`, `channel_prefix`, `end_of_track`, `tempo`, `smpte_offset`, `time_signature`, `key_signature`) — meta-event type bytes.
- `MidiTick` — `uint32_t` alias used for absolute SMF tick positions.
- `data_byte_count(status)` — returns `0` / `1` / `2` for fixed-width messages and `-1` for sysex / non-status bytes.
- `SysexMessage<MaxSize = 1024>` — fixed-capacity sysex buffer with `put_byte()`, `data()`, `length()`, `capacity()`, `is_full()`, `checksum()` (XOR of bytes, masked to 7 bits), and `clear()`.
- `SysexEvent` — `{ uint8_t status; std::span<uint8_t const> body; }`; the file reader delivers `F0` and `F7` sysex with this non-owning view.
- `MetaEvent` — `{ uint8_t type; std::span<uint8_t const> data; }` with `tempo_us_per_beat()`, `tempo_bpm()`, `text()`, `time_signature()`, `key_signature()` decoders that return `StatusValue<T>`.
- `MidiParser<MaxSysex = 1024>` — single-byte-at-a-time state machine; constructor takes optional `on_message`, `on_sysex`, `on_error` callbacks. Realtime bytes are dispatched immediately without altering parser state. `reset()` clears running status and any partial sysex.
- `MidiFileCallbacks` — bundle of `on_header(format, ntrks, division)`, `on_track_start(trk)`, `on_track_end(time, trk)`, `on_message(time, msg)`, `on_sysex(time, ev)`, `on_meta(time, ev)`, `on_error(err)` `inplace_function`s.
- `read_midi_file(span, cb)` — parses an SMF Type 0 or Type 1 file from a span and dispatches `cb`; returns `Status`.
- `MidiFileWriter` — stateful builder over a caller-supplied `MutableBuffer`: `write_header()`, `begin_track()` / `end_track()`, `write_message()`, `write_sysex()`, `write_meta()`, plus convenience writers `write_tempo()`, `write_time_signature()`, `write_key_signature()`, `write_end_of_track()`.
- `MidiFileProcessor` — pair of optional in-place transform lambdas `process_message(MidiTick&, MidiMessage&)` and `process_meta(MidiTick&, uint8_t&, std::span<uint8_t const>&)`.
- `process_midi_file(input, proc, writer)` — read-process-write pipeline that copies tracks through the writer, applying `proc`'s transforms.
- `MidiMatrix` — pure value type that tracks per-channel/per-note on-counts (16 × 128), per-channel totals, a global total, and per-channel damper-pedal (`CC 64`) state. Recognises `CC 123` "all notes off" and clears the channel.
- `make_fileshow_callbacks(FILE*)` — returns a `MidiFileCallbacks` set that prints a human-readable transcript to the given `FILE*`.
- `MidiError` — `enum class` (`unexpected_byte`, `buffer_overflow`, `invalid_status`, `malformed_file`, `invalid_header`, `unexpected_eof`, `track_overflow`, `invalid_vlq`, `invalid_meta_event`) registered as `std::is_error_code_enum`; `midi_error_category()` exposes the `"statusbar.midi"` category.

## Quick example

```cpp
#include "statusbar/midi/midi.hpp"

#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

using namespace statusbar::midi;

int main(int argc, char** argv)
{
    // Slurp the .mid file into memory — the reader works on a single span.
    std::FILE* f = std::fopen(argv[1], "rb");
    std::fseek(f, 0, SEEK_END);
    auto const n = static_cast<size_t>(std::ftell(f));
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> data(n);
    std::fread(data.data(), 1, n, f);
    std::fclose(f);

    MidiMatrix matrix;
    MidiFileCallbacks cb;
    cb.on_header = [](int fmt, int n, int div) {
        std::printf("format=%d tracks=%d division=%d\n", fmt, n, div);
    };
    cb.on_message = [&](MidiTick t, MidiMessage const& m) {
        matrix.process(m);
        if (m.is_note_on()) {
            std::printf("[%6u] note on ch=%u note=%u vel=%u (active=%d)\n",
                        t, m.channel(), m.note(), m.velocity(), matrix.total_count());
        }
    };
    cb.on_meta = [](MidiTick t, MetaEvent const& ev) {
        if (auto const bpm = ev.tempo_bpm(); bpm.has_value()) {
            std::printf("[%6u] tempo %.2f BPM\n", t, bpm.value());
        }
    };

    return is_failure(read_midi_file(std::span<uint8_t const>(data), cb)) ? 1 : 0;
}
```

## Headers

- `statusbar/midi/midi.hpp` — module header; consumers `#include` this.
- `statusbar/midi/midi_types.hpp` — `MidiMessage`, `MidiTick`, `status::` and `meta::` byte constants, `data_byte_count()`.
- `statusbar/midi/midi_sysex.hpp` — `SysexMessage<MaxSize>` owning buffer and `SysexEvent` non-owning view.
- `statusbar/midi/midi_meta.hpp` — `MetaEvent` view with `tempo_us_per_beat()`, `tempo_bpm()`, `text()`, `time_signature()`, `key_signature()` decoders.
- `statusbar/midi/midi_error.hpp` — `MidiError` enum, category, `make_error_code`.
- `statusbar/midi/midi_detail.hpp` — internal `read_vlq` / `write_vlq` for variable-length quantity encoding (publicly reachable, used by the writer).
- `statusbar/midi/midi_parser.hpp` — `MidiParser<MaxSysex>` stream-parser template.
- `statusbar/midi/midi_file_reader.hpp` — `MidiFileCallbacks` and `read_midi_file()`.
- `statusbar/midi/midi_file_writer.hpp` — `MidiFileWriter`.
- `statusbar/midi/midi_processor.hpp` — `MidiFileProcessor` and `process_midi_file()`.
- `statusbar/midi/midi_matrix.hpp` — `MidiMatrix` note-on bookkeeping.
- `statusbar/midi/midi_fileshow.hpp` — `make_fileshow_callbacks(FILE*)`.

## Dependencies

- **Statusbar modules:** `` `core::status` `` (`Status` / `StatusValue<T>` are the return types of every checked operation; `MidiError` flows through `failure()`), `` `core::buffer` `` (`MutableBuffer` is the output sink of `MidiFileWriter` and the VLQ writer; `span_utils.hpp` provides `as_string_view` used by `MetaEvent::text()`), `` `core::sg14` `` (the vendored `inplace_function` powers every parser / file-reader / processor callback with a 64-byte capture).
- **System / external:** `<array>`, `<cstddef>`, `<cstdint>`, `<cstdio>` (used by `midi_fileshow.hpp` for `FILE*`), `<print>` (used by `midi_fileshow.cpp`), `<span>`, `<string>`, `<string_view>`, `<system_error>` from the C++23 standard library. No `<fstream>`, no threads, no allocation inside the codec itself.

## Notes & caveats

- **SMF versions:** `read_midi_file()` and `MidiFileWriter` cover Type 0 (single track) and Type 1 (multi-track, simultaneous) files. Type 2 is not handled — passing a Type 2 header through the writer will produce a syntactically valid Type 2 file but the reader has no special semantics for it.
- **Division field:** `read_midi_file()` and `write_header()` pass the 16-bit `division` value through untouched. Ticks-per-quarter and SMPTE forms are not interpreted by the module; callers handle the bit layout themselves.
- **Running status:** the live parser, the file reader, and the file writer all honour MIDI running status. The writer suppresses the status byte when the next channel message has the same status as the previous one; any sysex or meta event resets running status to `0` per the SMF spec.
- **Realtime bytes interleave:** `MidiParser` emits any byte `>= 0xF8` immediately as a one-byte `MidiMessage` without disturbing the in-progress message or running status — same rule as the MIDI 1.0 hardware spec.
- **Variable-length quantities:** `detail::read_vlq` / `write_vlq` cap VLQs at four bytes (28 bits, the SMF maximum). Longer encodings return `MidiError::invalid_vlq`.
- **Sysex capacity is compile-time:** `SysexMessage<MaxSize>` and `MidiParser<MaxSysex>` carry a `std::array<uint8_t, MaxSize>` inline. `MaxSize` defaults to 1024; bump it as a template argument if you need larger payloads. Overflow during stream parsing fires `on_error(MidiError::buffer_overflow)` and discards the in-progress sysex.
- **Non-owning views:** `SysexEvent::body` and `MetaEvent::data` point into the caller's input span. They are valid only for the duration of the callback — copy out anything you need to retain.
- **Writer ordering:** `MidiFileWriter` requires `write_header()` before any track, then `begin_track()` / writes / `write_end_of_track()` / `end_track()`. `end_track()` back-patches the `MTrk` length at the offset captured by `begin_track()`. Nested or out-of-order calls return `MidiError::malformed_file`. Event times within a track must be monotonically non-decreasing — `write_delta_time()` returns `MidiError::malformed_file` on a backward jump.
- **Tempo encoding:** `MetaEvent::tempo_us_per_beat()` requires exactly three bytes; `tempo_bpm()` is the convenience derivative (`60_000_000 / us`). `write_tempo()` and the `0x51` meta event are the only tempo path — there is no separate "tempo map" structure.
- **`MidiMatrix` thresholds:** sustain (`CC 64`) tracks `>= 64` as "down" per the MIDI 1.0 spec; `CC 123` (all notes off) clears the channel's note counts. Other "all sound off" / "all controllers off" controllers are not specially recognised.
- **Real-time / RT-safety:** `MidiMessage`, `MidiMatrix`, `data_byte_count()`, and `MidiParser` (parsing only) perform no allocation and no I/O, and are suitable for use in an audio callback once the parser instance and its callbacks are constructed. `MidiFileWriter`, `read_midi_file()`, `process_midi_file()`, and `make_fileshow_callbacks()` are not RT-safe — they invoke user lambdas, the writer dereferences a `MutableBuffer`, and `fileshow` formats to a `FILE*`.
- **Reproducibility:** `MidiFileWriter` output is deterministic for a given input sequence: the same calls in the same order produce byte-identical files (the `MTrk` length is the live track-data byte count, and running-status suppression is purely a function of the message stream).

## Further reading

- `` `core::buffer` `` — `MutableBuffer` is the output sink consumed by `MidiFileWriter`; `span_utils.hpp` supplies the `as_string_view` adapter used by `MetaEvent::text()`.
- `` `core::status` `` — `Status` and `StatusValue<T>` returned by every checked operation here.
