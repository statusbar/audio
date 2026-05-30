#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// MIDI Parser — Byte-by-byte state machine for real-time MIDI stream parsing.
/// Handles running status, sysex accumulation, and interleaved realtime bytes.

#include "statusbar/midi/midi_error.hpp"
#include "statusbar/midi/midi_sysex.hpp"
#include "statusbar/midi/midi_types.hpp"
#include "statusbar/sg14/inplace_function.h"

#include <cstdint>

namespace statusbar::midi {

template <size_t MaxSysex = 1024>
class MidiParser
{
  public:
    using MessageCallback = statusbar::sg14::inplace_function<void(MidiMessage const&), 64>;
    using SysexCallback = statusbar::sg14::inplace_function<void(SysexMessage<MaxSysex> const&), 64>;
    using ErrorCallback = statusbar::sg14::inplace_function<void(MidiError), 64>;

    MidiParser(MessageCallback on_message = {}, SysexCallback on_sysex = {}, ErrorCallback on_error = {})
        : on_message_{on_message ? std::move(on_message) : MessageCallback{[](MidiMessage const&) {}}}
        , on_sysex_{on_sysex ? std::move(on_sysex) : SysexCallback{[](SysexMessage<MaxSysex> const&) {}}}
        , on_error_{on_error ? std::move(on_error) : ErrorCallback{[](MidiError) {}}}
    {}

    auto parse(uint8_t byte) -> void
    {
        // Realtime bytes are delivered immediately without disrupting state
        if (byte >= 0xF8) {
            on_message_(MidiMessage(byte));
            return;
        }

        if (byte >= 0x80) {
            parse_status_byte(byte);
            return;
        }

        parse_data_byte(byte);
    }

    auto reset() -> void
    {
        state_ = State::find_status;
        running_status_ = 0;
        sysex_.clear();
    }

  private:
    enum class State : uint8_t
    {
        find_status,
        first_of_one,
        first_of_two,
        second_of_two,
        sysex_data,
    };

    auto parse_status_byte(uint8_t byte) -> void
    {
        if (byte == status::sysex_start) {
            sysex_.clear();
            auto const s = sysex_.put_byte(byte);
            if (is_failure(s)) {
                on_error_(MidiError::buffer_overflow);
                state_ = State::find_status;
                return;
            }
            state_ = State::sysex_data;
            return;
        }

        if (byte == status::sysex_end) {
            if (state_ == State::sysex_data) {
                auto const s = sysex_.put_byte(byte);
                if (is_failure(s)) {
                    on_error_(MidiError::buffer_overflow);
                } else {
                    on_sysex_(sysex_);
                }
                sysex_.clear();
            }
            state_ = State::find_status;
            return;
        }

        auto const count = data_byte_count(byte);
        if (count == 2) {
            running_status_ = byte;
            state_ = State::first_of_two;
        } else if (count == 1) {
            running_status_ = byte;
            state_ = State::first_of_one;
        } else if (count == 0) {
            on_message_(MidiMessage(byte));
            state_ = State::find_status;
        } else {
            state_ = State::find_status;
        }
    }

    auto parse_data_byte(uint8_t byte) -> void
    {
        switch (state_) {
            case State::find_status:
                if (running_status_ != 0) {
                    auto const count = data_byte_count(running_status_);
                    if (count == 2) {
                        data1_ = byte;
                        state_ = State::second_of_two;
                    } else if (count == 1) {
                        on_message_(MidiMessage(running_status_, byte));
                    }
                }
                break;

            case State::first_of_one:
                on_message_(MidiMessage(running_status_, byte));
                state_ = State::find_status;
                break;

            case State::first_of_two:
                data1_ = byte;
                state_ = State::second_of_two;
                break;

            case State::second_of_two:
                on_message_(MidiMessage(running_status_, data1_, byte));
                state_ = State::find_status;
                break;

            case State::sysex_data:
                if (is_failure(sysex_.put_byte(byte))) {
                    on_error_(MidiError::buffer_overflow);
                    state_ = State::find_status;
                    sysex_.clear();
                }
                break;
        }
    }

    State state_{State::find_status};
    uint8_t running_status_{};
    uint8_t data1_{};
    SysexMessage<MaxSysex> sysex_{};

    MessageCallback on_message_;
    SysexCallback on_sysex_;
    ErrorCallback on_error_;
};

}  // namespace statusbar::midi
