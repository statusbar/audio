#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// OSC Base Types - Open Sound Control Protocol Fundamental Types
/// Based on OSC 1.0 specification

#include "statusbar/ieee/ieee.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>

namespace statusbar::osc {

using statusbar::ieee::quadlet_t;

//
// OSC Constants
//
/// OSC bundle identifier string (8 bytes, NUL-terminated).
constexpr std::array<uint8_t, 8> OSC_BUNDLE_ID{'#', 'b', 'u', 'n', 'd', 'l', 'e', '\0'};

/// Maximum address pattern length
constexpr size_t OSC_MAX_ADDRESS_LENGTH = 256;

/// Maximum type tag string length
constexpr size_t OSC_MAX_TYPETAGS = 128;

/// Maximum nesting depth for bundles-within-bundles (stack-overflow guard).
constexpr size_t OSC_MAX_BUNDLE_DEPTH = 32;

//
// OSC Type Tags
//
/// Standard OSC type tag characters
namespace type_tag {
constexpr char int32 = 'i';      ///< 32-bit big-endian signed integer
constexpr char float32 = 'f';    ///< 32-bit big-endian IEEE 754 float
constexpr char string = 's';     ///< Null-terminated string, padded to 4 bytes
constexpr char blob = 'b';       ///< Length-prefixed binary data, padded to 4 bytes
constexpr char int64 = 'h';      ///< 64-bit big-endian signed integer
constexpr char timetag = 't';    ///< 64-bit NTP time tag
constexpr char float64 = 'd';    ///< 64-bit big-endian IEEE 754 double
constexpr char true_val = 'T';   ///< True (no data bytes)
constexpr char false_val = 'F';  ///< False (no data bytes)
constexpr char nil = 'N';        ///< Nil/Null (no data bytes)
constexpr char impulse = 'I';    ///< Infinitum/Impulse/Bang (no data bytes)
constexpr char avb_time = 'a';   ///< AVB-style 64-bit timetag (custom extension)
}  // namespace type_tag

//
// NTP Timetag
//
/// NTP-style 64-bit timetag (OSC timetag format)
/// High 32 bits: seconds since January 1, 1900
/// Low 32 bits: fractional seconds (1/2^32 second units)
struct NtpTimetag
{
    /// Size on wire
    static constexpr size_t LENGTH = 8;

    /// Seconds since 1900-01-01 00:00:00 UTC
    quadlet_t seconds{};

    /// Fractional part (1/2^32 second units)
    quadlet_t fraction{};

    /// Default constructor - creates zero timetag
    constexpr NtpTimetag() noexcept = default;

    /// Construct from seconds and fraction
    constexpr NtpTimetag(uint32_t sec, uint32_t frac) noexcept
        : seconds{sec}
        , fraction{frac}
    {}

    /// Create the "immediately" timetag (0x0000000000000001)
    [[nodiscard]] static constexpr auto immediate() noexcept -> NtpTimetag { return NtpTimetag{0, 1}; }

    /// Check if this is the "immediately" timetag
    [[nodiscard]] constexpr auto is_immediate() const noexcept -> bool { return seconds.get() == 0 && fraction.get() == 1; }

    /// Check if this is a zero timetag
    [[nodiscard]] constexpr auto is_zero() const noexcept -> bool { return seconds.get() == 0 && fraction.get() == 0; }

    /// Get seconds value
    [[nodiscard]] constexpr auto get_seconds() const noexcept -> uint32_t { return seconds.get(); }

    /// Get fraction value
    [[nodiscard]] constexpr auto get_fraction() const noexcept -> uint32_t { return fraction.get(); }

    /// Set seconds value
    constexpr void set_seconds(uint32_t sec) noexcept { seconds = sec; }

    /// Set fraction value
    constexpr void set_fraction(uint32_t frac) noexcept { fraction = frac; }

    /// Comparison
    auto operator<=>(NtpTimetag const& rhs) const noexcept -> std::strong_ordering = default;
};

// Compile-time layout verification
static_assert(sizeof(NtpTimetag) == 8, "NtpTimetag must be exactly 8 bytes");
static_assert(alignof(NtpTimetag) <= 4, "NtpTimetag alignment must not exceed 4 bytes");

//
// OSC Alignment Helpers
//
/// Round up to next 4-byte boundary
/// @param len Length to round up
[[nodiscard]] constexpr auto osc_round_up(size_t len) noexcept -> size_t
{
    return (len + 3) & ~static_cast<size_t>(3);
}

/// Calculate padded size for a null-terminated string
/// Includes the null terminator, then pads to 4-byte boundary
/// @param string_length Length of the string (excluding null terminator)
[[nodiscard]] constexpr auto osc_padded_string_size(size_t string_length) noexcept -> size_t
{
    return osc_round_up(string_length + 1);  // +1 for null terminator
}

/// Calculate padded size for blob data
/// Includes 4-byte length prefix, then data padded to 4-byte boundary
/// @param blob_length Length of the blob data in bytes
[[nodiscard]] constexpr auto osc_padded_blob_size(size_t blob_length) noexcept -> size_t
{
    return 4 + osc_round_up(blob_length);  // 4 bytes for length prefix
}

}  // namespace statusbar::osc
