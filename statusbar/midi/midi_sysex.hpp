#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// MIDI Sysex Message — Fixed-capacity system exclusive message buffer
/// Template parameter MaxSize controls the maximum sysex payload size.
/// Default is 1024 bytes.

#include "statusbar/midi/midi_error.hpp"
#include "statusbar/status/status.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace statusbar::midi {

/// Non-owning view of a parsed sysex event delivered by a file-level parser.
/// `status` is the leading status byte (0xF0 for a regular sysex, 0xF7 for
/// an escape sysex); `body` references the payload bytes that follow the
/// VLQ length in the input stream, and does *not* include the status byte.
/// Both `body`'s bytes live in the caller's input buffer — copy if you
/// need to retain them past the callback.
struct SysexEvent
{
    uint8_t status{};
    std::span<uint8_t const> body{};
};

template <size_t MaxSize = 1024>
class SysexMessage
{
    std::array<uint8_t, MaxSize> data_{};
    size_t length_{};

  public:
    constexpr SysexMessage() = default;

    [[nodiscard]] constexpr auto data() const -> std::span<uint8_t const>
    {
        return std::span<uint8_t const>(data_.data(), length_);
    }

    [[nodiscard]] constexpr auto length() const -> size_t { return length_; }
    [[nodiscard]] constexpr auto capacity() const -> size_t { return MaxSize; }
    [[nodiscard]] constexpr auto is_full() const -> bool { return length_ >= MaxSize; }

    [[nodiscard]] constexpr auto checksum() const -> uint8_t
    {
        uint8_t chk = 0;
        for (size_t i = 0; i < length_; ++i) {
            chk ^= data_[i];
        }
        return chk & 0x7Fu;
    }

    [[nodiscard]] constexpr auto put_byte(uint8_t b) -> Status
    {
        if (length_ >= MaxSize) {
            return failure(MidiError::buffer_overflow);
        }
        data_[length_++] = b;
        return success();
    }

    constexpr auto clear() -> void { length_ = 0; }
};

}  // namespace statusbar::midi
