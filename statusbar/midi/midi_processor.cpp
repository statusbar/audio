// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/midi/midi_processor.hpp"

#include "statusbar/midi/midi_error.hpp"
#include "statusbar/midi/midi_file_reader.hpp"
#include "statusbar/midi/midi_meta.hpp"
#include "statusbar/midi/midi_sysex.hpp"
#include "statusbar/midi/midi_types.hpp"

namespace statusbar::midi {

auto process_midi_file(std::span<uint8_t const> input, MidiFileProcessor const& proc, MidiFileWriter& writer) -> Status
{
    // First pass: extract header info
    int file_format = 0;
    int file_ntrks = 0;
    int file_division = 0;
    {
        MidiFileCallbacks hdr_cb;
        hdr_cb.on_header = [&](int fmt, int ntrks, int div) {
            file_format = fmt;
            file_ntrks = ntrks;
            file_division = div;
        };
        auto const s = read_midi_file(input, hdr_cb);
        if (is_failure(s)) {
            return s;
        }
    }

    // Write header
    auto s = writer.write_header(file_format, file_ntrks, file_division);
    if (is_failure(s)) {
        return s;
    }

    // Second pass: process and write events
    MidiFileCallbacks cb;

    cb.on_track_start = [&](int) {
        if (is_failure(s)) {
            return;  // don't overwrite an earlier track's failure
        }
        s = writer.begin_track();
    };

    cb.on_message = [&](MidiTick time, MidiMessage const& msg) {
        if (is_failure(s)) {
            return;
        }
        auto t = time;
        auto m = msg;
        if (proc.process_message) {
            proc.process_message(t, m);
        }
        s = writer.write_message(t, m);
    };

    cb.on_sysex = [&](MidiTick time, SysexEvent const& sx) {
        if (is_failure(s)) {
            return;
        }
        s = writer.write_sysex(time, sx.body, sx.status);
    };

    cb.on_meta = [&](MidiTick time, MetaEvent const& ev) {
        if (is_failure(s)) {
            return;
        }
        if (ev.type == meta::end_of_track) {
            return;  // handled by on_track_end
        }
        auto t = time;
        auto mt = ev.type;
        auto md = ev.data;
        if (proc.process_meta) {
            proc.process_meta(t, mt, md);
        }
        s = writer.write_meta(t, mt, md);
    };

    cb.on_track_end = [&](MidiTick time, int) {
        if (is_failure(s)) {
            return;
        }
        s = writer.write_end_of_track(time);
        if (is_failure(s)) {
            return;
        }
        s = writer.end_track();
    };

    cb.on_error = [&](MidiError err) { s = failure(err); };

    auto const read_s = read_midi_file(input, cb);
    if (is_failure(read_s)) {
        return read_s;
    }
    return s;
}

}  // namespace statusbar::midi
