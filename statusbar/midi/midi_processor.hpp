#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// MIDI File Processor — Optional per-event-type transform callbacks.
/// Unset callbacks pass events through unchanged (identity).
/// Used with process_midi_file() to create read-process-write pipelines.

#include "statusbar/midi/midi_file_writer.hpp"
#include "statusbar/midi/midi_types.hpp"
#include "statusbar/sg14/inplace_function.h"
#include "statusbar/status/status.hpp"

#include <cstdint>
#include <span>

namespace statusbar::midi {

struct MidiFileProcessor
{
    statusbar::sg14::inplace_function<void(MidiTick&, MidiMessage&), 64> process_message = {};
    statusbar::sg14::inplace_function<void(MidiTick&, uint8_t&, std::span<uint8_t const>&), 64> process_meta = {};
};

/// Read a MIDI file, pass events through a processor, and write the result.
/// The processor's unset callbacks act as identity (passthrough).
/// Implementation in midi_processor.cpp.
[[nodiscard]] auto process_midi_file(std::span<uint8_t const> input, MidiFileProcessor const& proc, MidiFileWriter& writer)
    -> Status;

}  // namespace statusbar::midi
