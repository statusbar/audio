#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// SMPTE LTC Frame Encoder
// Encodes timecode into 80-bit LTC frame format per SMPTE 12M
// Supports multiple frame rates including drop frame

#include "statusbar/ltc/ltc_timecode.hpp"

#include <array>
#include <cstdint>

namespace statusbar::ltc {

/// 80-bit LTC frame structure
/// Bit layout follows SMPTE 12M specification
class LTCFrame
{
  public:
    /// Construct LTC frame from timecode
    /// @param tc SMPTE timecode to encode into the 80-bit frame
    explicit constexpr LTCFrame(Timecode const& tc) noexcept { encode(tc); }

    /// Get bit value at position (0-79)
    /// @param pos Bit position within the 80-bit LTC frame
    [[nodiscard]] constexpr auto get_bit(size_t pos) const noexcept -> bool
    {
        if (pos >= 80) {
            return false;
        }
        size_t const byte_idx = pos / 8;
        size_t const bit_idx = pos % 8;
        return ((data_[byte_idx] >> bit_idx) & 1) != 0;
    }

    /// Get the 80-bit frame as array of bytes
    [[nodiscard]] constexpr auto data() const noexcept -> std::array<uint8_t, 10> const& { return data_; }

  private:
    std::array<uint8_t, 10> data_{};  // 80 bits = 10 bytes

    /// Encode timecode into 80-bit frame
    constexpr auto encode(Timecode const& tc) noexcept -> void
    {
        data_.fill(0);

        // Frame units (bits 0-3)
        set_bcd(0, tc.frames % 10, 4);

        // User bits 1-4 (bits 4-7)
        set_bits(4, (tc.user_bits >> 0) & 0xF, 4);

        // Frame tens (bits 8-9)
        set_bcd(8, tc.frames / 10, 2);

        // Drop frame flag (bit 10) - set for drop frame rates
        set_bit(10, is_drop_frame(tc.rate));

        // Color frame flag (bit 11)
        set_bit(11, tc.color_frame);

        // User bits 5-8 (bits 12-15)
        set_bits(12, (tc.user_bits >> 4) & 0xF, 4);

        // Seconds units (bits 16-19)
        set_bcd(16, tc.seconds % 10, 4);

        // User bits 9-12 (bits 20-23)
        set_bits(20, (tc.user_bits >> 8) & 0xF, 4);

        // Seconds tens (bits 24-26)
        set_bcd(24, tc.seconds / 10, 3);

        // Binary group flag bit 0 (bit 27)
        // Used to indicate the format of user bits
        set_bit(27, false);

        // User bits 13-16 (bits 28-31)
        set_bits(28, (tc.user_bits >> 12) & 0xF, 4);

        // Minutes units (bits 32-35)
        set_bcd(32, tc.minutes % 10, 4);

        // User bits 17-20 (bits 36-39)
        set_bits(36, (tc.user_bits >> 16) & 0xF, 4);

        // Minutes tens (bits 40-42)
        set_bcd(40, tc.minutes / 10, 3);

        // Binary group flag bit 1 (bit 43)
        set_bit(43, false);

        // User bits 21-24 (bits 44-47)
        set_bits(44, (tc.user_bits >> 20) & 0xF, 4);

        // Hours units (bits 48-51)
        set_bcd(48, tc.hours % 10, 4);

        // User bits 25-28 (bits 52-55)
        set_bits(52, (tc.user_bits >> 24) & 0xF, 4);

        // Hours tens (bits 56-57)
        set_bcd(56, tc.hours / 10, 2);

        // Binary group flag bit 2 (bit 58)
        set_bit(58, false);

        // Biphase mark polarity correction bit — cleared here, then computed
        // over the whole frame once every other bit is set (see below). Its
        // position is rate-dependent per SMPTE 12M: bit 27 at 25 fps, else 59.
        set_bit(59, false);

        // User bits 29-32 (bits 60-63)
        set_bits(60, (tc.user_bits >> 28) & 0xF, 4);

        // Sync word (bits 64-79): 0x3FFD = 0011111111111101
        // Bit 64-79 = 0xBFFC in our bit order (LSB first per byte)
        set_bits(64, 0xFC, 8);  // Bits 64-71: 0xFC
        set_bits(72, 0xBF, 8);  // Bits 72-79: 0xBF

        // SMPTE 12M: set the polarity-correction bit so the entire 80-bit frame
        // holds an even number of one-bits, keeping each frame's biphase
        // waveform starting at a consistent polarity. Computed last, over all 80
        // bits including the sync word. At 25 fps the bit is 27 (bit 59 is a
        // binary group flag); at every other rate it is 59.
        size_t const correction_bit = (tc.rate == FrameRate::Rate_25) ? 27 : 59;
        if (count_ones() % 2 != 0) {
            set_bit(correction_bit, true);
        }
    }

    /// Count the one-bits across the whole 80-bit frame.
    [[nodiscard]] constexpr auto count_ones() const noexcept -> size_t
    {
        size_t count = 0;
        for (size_t i = 0; i < 80; ++i) {
            if (get_bit(i)) {
                ++count;
            }
        }
        return count;
    }

    /// Set a single bit at position
    constexpr auto set_bit(size_t pos, bool value) noexcept -> void
    {
        if (pos >= 80) {
            return;
        }
        size_t const byte_idx = pos / 8;
        size_t const bit_idx = pos % 8;
        if (value) {
            data_[byte_idx] |= (1 << bit_idx);
        } else {
            data_[byte_idx] &= ~(1 << bit_idx);
        }
    }

    /// Set multiple bits starting at position
    constexpr auto set_bits(size_t pos, uint8_t value, size_t count) noexcept -> void
    {
        for (size_t i = 0; i < count; ++i) {
            set_bit(pos + i, ((value >> i) & 1) != 0);
        }
    }

    /// Set BCD encoded value
    constexpr auto set_bcd(size_t pos, uint8_t value, size_t count) noexcept -> void { set_bits(pos, value, count); }
};

}  // namespace statusbar::ltc
