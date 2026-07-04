#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// MIDI File Writer — Stateful builder for writing Type 0 and Type 1 MIDI files.
/// Operates on a MutableBuffer. Handles running status, delta time, and track length patching.

#include "statusbar/buffer/buffer.hpp"
#include "statusbar/midi/midi_detail.hpp"
#include "statusbar/midi/midi_error.hpp"
#include "statusbar/midi/midi_types.hpp"
#include "statusbar/status/status.hpp"

#include <cstdint>
#include <span>

namespace statusbar::midi {

class MidiFileWriter
{
  public:
    explicit MidiFileWriter(MutableBuffer& output)
        : output_{output}
    {}

    [[nodiscard]] auto write_header(int format, int ntrks, int division) -> Status;
    [[nodiscard]] auto begin_track() -> Status;
    [[nodiscard]] auto end_track() -> Status;

    [[nodiscard]] auto write_message(MidiTick time, MidiMessage const& msg) -> Status;
    /// Write a sysex event. `status` is the leading status byte: 0xF0 for a
    /// normal sysex, 0xF7 for an escape/continuation event — preserving it lets
    /// a read→write pipeline round-trip F7 events instead of rewriting them as
    /// F0. `data` is the payload (including any trailing 0xF7 the caller wants).
    [[nodiscard]] auto write_sysex(MidiTick time, std::span<uint8_t const> data, uint8_t status = 0xF0) -> Status;
    [[nodiscard]] auto write_meta(MidiTick time, uint8_t meta_type, std::span<uint8_t const> data) -> Status;

    [[nodiscard]] auto write_tempo(MidiTick time, uint32_t us_per_beat) -> Status;
    [[nodiscard]] auto write_time_signature(
        MidiTick time, uint8_t num, uint8_t denom_power, uint8_t clocks_per_click, uint8_t notated_32nds) -> Status;
    [[nodiscard]] auto write_key_signature(MidiTick time, int8_t sharps_flats, uint8_t minor) -> Status;
    [[nodiscard]] auto write_end_of_track(MidiTick time) -> Status;

  private:
    [[nodiscard]] auto write_delta_time(MidiTick time) -> Status;
    [[nodiscard]] auto append_u16(uint16_t val) -> Status;
    [[nodiscard]] auto append_u32(uint32_t val) -> Status;
    [[nodiscard]] auto append_byte(uint8_t val) -> Status;

    MutableBuffer& output_;
    MidiTick track_time_{};
    uint8_t running_status_{};
    size_t track_length_offset_{};
    size_t track_data_start_{};
    bool in_track_{};
};

}  // namespace statusbar::midi
