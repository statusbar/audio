#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// MIDI Matrix — Tracks note-on state across all 16 channels and 128 notes.
/// Maintains per-note counts, per-channel totals, global total, and sustain pedal state.
/// Pure value type with no dynamic allocation or virtual dispatch.

#include "statusbar/midi/midi_types.hpp"

#include <array>
#include <cstdint>

namespace statusbar::midi {

class MidiMatrix
{
  public:
    static constexpr int num_channels = 16;
    static constexpr int num_notes = 128;
    static constexpr uint8_t damper_controller = 64;

    constexpr MidiMatrix() = default;

    /// Process a MIDI message, updating the matrix state.
    constexpr auto process(MidiMessage const& msg) -> void
    {
        if (!msg.is_channel_message()) {
            return;
        }

        auto const ch = msg.channel();

        switch (msg.type()) {
            case status::note_on:
                if (msg.velocity() > 0) {
                    inc_note_count(ch, msg.note());
                } else {
                    dec_note_count(ch, msg.note());
                }
                break;

            case status::note_off:
                dec_note_count(ch, msg.note());
                break;

            case status::control_change:
                if (msg.controller() == damper_controller) {
                    hold_pedal_[ch] = (msg.controller_value() >= 64);
                } else if (msg.controller() == 123) {
                    // All Notes Off
                    clear_channel(ch);
                }
                break;

            default:
                break;
        }
    }

    /// Get the note-on count for a specific channel and note.
    [[nodiscard]] constexpr auto note_count(uint8_t channel, uint8_t note) const -> uint8_t
    {
        return note_on_count_[channel][note];
    }

    /// Get the total active note count for a channel.
    [[nodiscard]] constexpr auto channel_count(uint8_t channel) const -> int { return channel_count_[channel]; }

    /// Get the total active note count across all channels.
    [[nodiscard]] constexpr auto total_count() const -> int { return total_count_; }

    /// Get the sustain pedal state for a channel.
    [[nodiscard]] constexpr auto hold_pedal(uint8_t channel) const -> bool { return hold_pedal_[channel]; }

    /// Clear all state for a specific channel.
    constexpr auto clear_channel(uint8_t channel) -> void
    {
        for (int note = 0; note < num_notes; ++note) {
            total_count_ -= note_on_count_[channel][note];
            note_on_count_[channel][note] = 0;
        }
        channel_count_[channel] = 0;
        hold_pedal_[channel] = false;
    }

    /// Clear all state across all channels.
    constexpr auto clear() -> void
    {
        for (int ch = 0; ch < num_channels; ++ch) {
            for (int note = 0; note < num_notes; ++note) {
                note_on_count_[ch][note] = 0;
            }
            channel_count_[ch] = 0;
            hold_pedal_[ch] = false;
        }
        total_count_ = 0;
    }

  private:
    constexpr auto inc_note_count(uint8_t channel, uint8_t note) -> void
    {
        ++note_on_count_[channel][note];
        ++channel_count_[channel];
        ++total_count_;
    }

    constexpr auto dec_note_count(uint8_t channel, uint8_t note) -> void
    {
        if (note_on_count_[channel][note] > 0) {
            --note_on_count_[channel][note];
            --channel_count_[channel];
            --total_count_;
        }
    }

    std::array<std::array<uint8_t, num_notes>, num_channels> note_on_count_{};
    std::array<int, num_channels> channel_count_{};
    std::array<bool, num_channels> hold_pedal_{};
    int total_count_{};
};

}  // namespace statusbar::midi
