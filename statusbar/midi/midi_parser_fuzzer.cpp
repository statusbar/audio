// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for the byte-by-byte MidiParser stream state machine.
/// Feeds arbitrary bytes one at a time (running status, sysex accumulation,
/// interleaved realtime). The parser must not crash, hang, or read/write out
/// of bounds on any input. What we look for is undefined behavior caught by
/// ASan / UBSan.

#include "statusbar/midi/midi_parser.hpp"

#include <cstddef>
#include <cstdint>

using namespace statusbar;

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    midi::MidiParser<256> parser(
        [](midi::MidiMessage const&) {},
        [](midi::SysexMessage<256> const&) {},
        [](midi::MidiError) {});

    for (size_t i = 0; i < size; ++i) {
        parser.parse(data[i]);
    }
    return 0;
}
