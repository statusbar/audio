#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// MIDI Error Types — Error codes and error category for MIDI operations

#include <string>
#include <system_error>

namespace statusbar::midi {

enum class MidiError
{
    unexpected_byte = 1,
    buffer_overflow,
    invalid_status,
    malformed_file,
    invalid_header,
    unexpected_eof,
    track_overflow,
    invalid_vlq,
    invalid_meta_event,
};

class MidiErrorCategory : public std::error_category
{
  public:
    [[nodiscard]] auto name() const noexcept -> char const* override { return "statusbar.midi"; }
    [[nodiscard]] auto message(int ev) const -> std::string override;
};

[[nodiscard]] auto midi_error_category() noexcept -> MidiErrorCategory const&;
[[nodiscard]] auto make_error_code(MidiError e) noexcept -> std::error_code;

}  // namespace statusbar::midi

template <>
struct std::is_error_code_enum<statusbar::midi::MidiError> : std::true_type
{};
