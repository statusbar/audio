#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// MIDI File Reader — Parses Type 0 and Type 1 MIDI files from a byte span.
/// Delivers events via callbacks. All spans passed to callbacks are
/// non-owning views into the caller's input buffer; copy them into a
/// container of your choice if you need to retain the bytes.

#include "statusbar/midi/midi_error.hpp"
#include "statusbar/midi/midi_meta.hpp"
#include "statusbar/midi/midi_sysex.hpp"
#include "statusbar/midi/midi_types.hpp"
#include "statusbar/sg14/inplace_function.h"
#include "statusbar/status/status.hpp"

#include <cstdint>
#include <span>

namespace statusbar::midi {

/// Callback set for read_midi_file. Sysex and meta events are delivered
/// as non-owning views (SysexEvent / MetaEvent) referencing bytes inside
/// the caller's input span.
struct MidiFileCallbacks
{
    statusbar::sg14::inplace_function<void(int format, int ntrks, int division), 64> on_header = {};
    statusbar::sg14::inplace_function<void(int track_num), 64> on_track_start = {};
    statusbar::sg14::inplace_function<void(MidiTick time, int track_num), 64> on_track_end = {};
    statusbar::sg14::inplace_function<void(MidiTick time, MidiMessage const&), 64> on_message = {};
    statusbar::sg14::inplace_function<void(MidiTick time, SysexEvent const&), 64> on_sysex = {};
    statusbar::sg14::inplace_function<void(MidiTick time, MetaEvent const&), 64> on_meta = {};
    statusbar::sg14::inplace_function<void(MidiError), 64> on_error = {};
};

/// Parse a Type 0 or Type 1 MIDI file from `data`, dispatching events
/// through `cb`. Implementation in midi_file_reader.cpp.
[[nodiscard]] auto read_midi_file(std::span<uint8_t const> data, MidiFileCallbacks const& cb) -> Status;

}  // namespace statusbar::midi
