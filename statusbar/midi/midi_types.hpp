#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// MIDI Types — Core message representation and protocol constants
/// Provides MidiMessage (fixed-size 3-byte value type) and MIDI status/meta constants.

#include <cstdint>

namespace statusbar::midi {

using MidiTick = uint32_t;

namespace status {
inline constexpr uint8_t note_off = 0x80;
inline constexpr uint8_t note_on = 0x90;
inline constexpr uint8_t poly_pressure = 0xA0;
inline constexpr uint8_t control_change = 0xB0;
inline constexpr uint8_t program_change = 0xC0;
inline constexpr uint8_t channel_pressure = 0xD0;
inline constexpr uint8_t pitch_bend = 0xE0;
inline constexpr uint8_t sysex_start = 0xF0;
inline constexpr uint8_t mtc_quarter = 0xF1;
inline constexpr uint8_t song_position = 0xF2;
inline constexpr uint8_t song_select = 0xF3;
inline constexpr uint8_t tune_request = 0xF6;
inline constexpr uint8_t sysex_end = 0xF7;
inline constexpr uint8_t timing_clock = 0xF8;
inline constexpr uint8_t midi_start = 0xFA;
inline constexpr uint8_t midi_continue = 0xFB;
inline constexpr uint8_t midi_stop = 0xFC;
inline constexpr uint8_t active_sense = 0xFE;
inline constexpr uint8_t system_reset = 0xFF;
}  // namespace status

namespace meta {
inline constexpr uint8_t sequence_number = 0x00;
inline constexpr uint8_t text = 0x01;
inline constexpr uint8_t copyright = 0x02;
inline constexpr uint8_t track_name = 0x03;
inline constexpr uint8_t instrument_name = 0x04;
inline constexpr uint8_t lyric = 0x05;
inline constexpr uint8_t marker = 0x06;
inline constexpr uint8_t cue_point = 0x07;
inline constexpr uint8_t channel_prefix = 0x20;
inline constexpr uint8_t end_of_track = 0x2F;
inline constexpr uint8_t tempo = 0x51;
inline constexpr uint8_t smpte_offset = 0x54;
inline constexpr uint8_t time_signature = 0x58;
inline constexpr uint8_t key_signature = 0x59;
}  // namespace meta

/// Get the number of data bytes expected after a status byte.
/// Returns 0 for single-byte messages, 1 for 2-byte, 2 for 3-byte.
/// Returns -1 for variable-length (sysex) or non-status bytes.
[[nodiscard]] constexpr auto data_byte_count(uint8_t status_byte) -> int
{
    if (status_byte < 0x80) {
        return -1;
    }
    if (status_byte < 0xF0) {
        constexpr int lut[] = {2, 2, 2, 2, 1, 1, 2};
        return lut[(status_byte >> 4) - 8];
    }
    switch (status_byte) {
        case status::sysex_start:
            return -1;
        case status::mtc_quarter:
            return 1;
        case status::song_position:
            return 2;
        case status::song_select:
            return 1;
        case status::tune_request:
            return 0;
        case status::sysex_end:
            return 0;
        default:
            return 0;
    }
}

/// Fixed-size MIDI channel or system message (3 bytes + padding).
class MidiMessage
{
    uint8_t status_{};
    uint8_t data1_{};
    uint8_t data2_{};

  public:
    constexpr MidiMessage() = default;

    constexpr explicit MidiMessage(uint8_t status_byte, uint8_t d1 = 0, uint8_t d2 = 0)
        : status_{status_byte}
        , data1_{d1}
        , data2_{d2}
    {}

    [[nodiscard]] constexpr auto status() const -> uint8_t { return status_; }
    [[nodiscard]] constexpr auto data1() const -> uint8_t { return data1_; }
    [[nodiscard]] constexpr auto data2() const -> uint8_t { return data2_; }
    [[nodiscard]] constexpr auto channel() const -> uint8_t { return status_ & 0x0Fu; }
    [[nodiscard]] constexpr auto type() const -> uint8_t { return status_ & 0xF0u; }
    [[nodiscard]] constexpr auto note() const -> uint8_t { return data1_; }
    [[nodiscard]] constexpr auto velocity() const -> uint8_t { return data2_; }
    [[nodiscard]] constexpr auto controller() const -> uint8_t { return data1_; }
    [[nodiscard]] constexpr auto controller_value() const -> uint8_t { return data2_; }
    [[nodiscard]] constexpr auto program() const -> uint8_t { return data1_; }

    [[nodiscard]] constexpr auto pitch_bend() const -> int16_t
    {
        return static_cast<int16_t>((static_cast<uint16_t>(data2_) << 7 | data1_) - 0x2000u);
    }

    [[nodiscard]] constexpr auto message_length() const -> int
    {
        auto const count = data_byte_count(status_);
        return count < 0 ? 1 : 1 + count;
    }

    [[nodiscard]] constexpr auto is_note_on() const -> bool { return type() == status::note_on && data2_ > 0; }

    [[nodiscard]] constexpr auto is_note_off() const -> bool
    {
        return type() == status::note_off || (type() == status::note_on && data2_ == 0);
    }

    [[nodiscard]] constexpr auto is_channel_message() const -> bool { return status_ >= 0x80 && status_ < 0xF0; }

    [[nodiscard]] constexpr auto is_system_message() const -> bool { return status_ >= 0xF0; }

    [[nodiscard]] constexpr auto is_realtime() const -> bool { return status_ >= 0xF8; }

    static constexpr auto note_on(uint8_t ch, uint8_t note, uint8_t vel) -> MidiMessage
    {
        return MidiMessage(status::note_on | (ch & 0x0Fu), note & 0x7Fu, vel & 0x7Fu);
    }

    static constexpr auto note_off(uint8_t ch, uint8_t note, uint8_t vel = 0) -> MidiMessage
    {
        return MidiMessage(status::note_off | (ch & 0x0Fu), note & 0x7Fu, vel & 0x7Fu);
    }

    static constexpr auto control_change(uint8_t ch, uint8_t cc, uint8_t val) -> MidiMessage
    {
        return MidiMessage(status::control_change | (ch & 0x0Fu), cc & 0x7Fu, val & 0x7Fu);
    }

    static constexpr auto program_change(uint8_t ch, uint8_t pg) -> MidiMessage
    {
        return MidiMessage(status::program_change | (ch & 0x0Fu), pg & 0x7Fu);
    }

    static constexpr auto pitch_bend(uint8_t ch, int16_t val) -> MidiMessage
    {
        auto const unsigned_val = static_cast<uint16_t>(val + 0x2000);
        return MidiMessage(
            status::pitch_bend | (ch & 0x0Fu),
            static_cast<uint8_t>(unsigned_val & 0x7Fu),
            static_cast<uint8_t>((unsigned_val >> 7) & 0x7Fu));
    }

    static constexpr auto channel_pressure(uint8_t ch, uint8_t pressure) -> MidiMessage
    {
        return MidiMessage(status::channel_pressure | (ch & 0x0Fu), pressure & 0x7Fu);
    }

    static constexpr auto poly_pressure(uint8_t ch, uint8_t note, uint8_t pressure) -> MidiMessage
    {
        return MidiMessage(status::poly_pressure | (ch & 0x0Fu), note & 0x7Fu, pressure & 0x7Fu);
    }
};

}  // namespace statusbar::midi
