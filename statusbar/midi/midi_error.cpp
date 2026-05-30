// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/midi/midi_error.hpp"

#include <string>
#include <system_error>

namespace statusbar::midi {

auto MidiErrorCategory::message(int ev) const -> std::string
{
    switch (static_cast<MidiError>(ev)) {
        case MidiError::unexpected_byte:
            return "Unexpected MIDI byte";
        case MidiError::buffer_overflow:
            return "MIDI buffer overflow";
        case MidiError::invalid_status:
            return "Invalid MIDI status byte";
        case MidiError::malformed_file:
            return "Malformed MIDI file";
        case MidiError::invalid_header:
            return "Invalid MIDI file header";
        case MidiError::unexpected_eof:
            return "Unexpected end of MIDI data";
        case MidiError::track_overflow:
            return "MIDI track data overflow";
        case MidiError::invalid_vlq:
            return "Invalid variable-length quantity";
        case MidiError::invalid_meta_event:
            return "Invalid MIDI meta event";
        default:
            return "Unknown MIDI error";
    }
}

auto midi_error_category() noexcept -> MidiErrorCategory const&
{
    static MidiErrorCategory const instance;
    return instance;
}

auto make_error_code(MidiError e) noexcept -> std::error_code
{
    return {static_cast<int>(e), midi_error_category()};
}

}  // namespace statusbar::midi
