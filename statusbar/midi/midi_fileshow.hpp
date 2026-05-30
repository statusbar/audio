#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// MIDI File Show — Returns MidiFileCallbacks that print formatted text to a FILE*

#include "statusbar/midi/midi_file_reader.hpp"

#include <cstdio>

namespace statusbar::midi {

/// Build a MidiFileCallbacks set that prints a human-readable transcript of
/// every event to `out`. Implementation in midi_fileshow.cpp.
[[nodiscard]] auto make_fileshow_callbacks(FILE* out) -> MidiFileCallbacks;

}  // namespace statusbar::midi
