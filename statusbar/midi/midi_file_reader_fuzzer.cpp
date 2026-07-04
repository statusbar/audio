// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for midi::read_midi_file. Feeds arbitrary bytes as a
/// Standard MIDI File and drives every callback. The reader must not crash,
/// hang, or read out of bounds on any input; it may legally return any error.
/// What we look for is undefined behavior caught by ASan / UBSan.

#include "statusbar/midi/midi_file_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

using namespace statusbar;

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    midi::MidiFileCallbacks cb;
    // Touch every delivered value so the optimizer can't elide the parse.
    cb.on_header = [](int, int, int) {};
    cb.on_message = [](midi::MidiTick, midi::MidiMessage const&) {};
    cb.on_sysex = [](midi::MidiTick, midi::SysexEvent const&) {};
    cb.on_meta = [](midi::MidiTick, midi::MetaEvent const&) {};
    cb.on_track_start = [](int) {};
    cb.on_track_end = [](midi::MidiTick, int) {};
    cb.on_error = [](midi::MidiError) {};

    (void)midi::read_midi_file(std::span<uint8_t const>(data, size), cb);
    return 0;
}
