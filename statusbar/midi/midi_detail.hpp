#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// MIDI Detail — Internal utilities for VLQ encoding/decoding

#include "statusbar/buffer/buffer.hpp"
#include "statusbar/midi/midi_error.hpp"
#include "statusbar/status/status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace statusbar::midi::detail {

/// Read a variable-length quantity from a span, advancing offset.
[[nodiscard]] inline auto read_vlq(std::span<uint8_t const> data, size_t& offset) -> StatusValue<uint32_t>
{
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        if (offset >= data.size()) {
            return failure(MidiError::invalid_vlq);
        }
        auto const byte = data[offset++];
        value = (value << 7) | (byte & 0x7Fu);
        if ((byte & 0x80u) == 0) {
            return value;
        }
    }
    return failure(MidiError::invalid_vlq);
}

/// Write a variable-length quantity into a MutableBuffer.
[[nodiscard]] inline auto write_vlq(MutableBuffer& buf, uint32_t value) -> Status
{
    uint8_t bytes[4];
    int count = 0;

    bytes[count++] = static_cast<uint8_t>(value & 0x7Fu);
    value >>= 7;

    while (value > 0) {
        bytes[count++] = static_cast<uint8_t>((value & 0x7Fu) | 0x80u);
        value >>= 7;
    }

    // Write in reverse order (MSB first)
    for (int i = count - 1; i >= 0; --i) {
        auto const s = buf.append(std::span<uint8_t const>(&bytes[i], 1));
        if (is_failure(s)) {
            return failure(MidiError::buffer_overflow);
        }
    }
    return success();
}

}  // namespace statusbar::midi::detail
