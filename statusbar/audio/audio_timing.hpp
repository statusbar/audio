#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Audio Module - Timing Calculations
// Platform-independent timing and frame/time conversion functions
// These are extracted for testability and reuse across platforms

#include "statusbar/dsp/dsp.hpp"

#include <cstdint>

namespace statusbar::audio {

/// Convert frame count to time in seconds
/// @param frames Number of audio frames
/// @param sample_rate Sample rate in Hz
/// @return Time in seconds
[[nodiscard]] constexpr auto frames_to_seconds(uint64_t const frames, double const sample_rate) noexcept -> double
{
    if (sample_rate <= 0.0) {
        return 0.0;
    }
    return static_cast<double>(frames) / sample_rate;
}

/// Convert time in seconds to frame count
/// @param seconds Time in seconds
/// @param sample_rate Sample rate in Hz
/// @return Number of frames (rounded to nearest)
[[nodiscard]] constexpr auto seconds_to_frames(double const seconds, double const sample_rate) noexcept -> uint64_t
{
    if (seconds <= 0.0 || sample_rate <= 0.0) {
        return 0;
    }
    return static_cast<uint64_t>(dsp::lround(seconds * sample_rate));
}

/// Calculate period duration in microseconds
/// @param period_frames Number of frames per period
/// @param sample_rate Sample rate in Hz
/// @return Period duration in microseconds
[[nodiscard]] constexpr auto calculate_period_time_us(uint32_t const period_frames, double const sample_rate) noexcept -> double
{
    if (sample_rate <= 0.0) {
        return 0.0;
    }
    return (static_cast<double>(period_frames) / sample_rate) * 1000000.0;
}

/// Calculate buffer duration in microseconds
/// @param buffer_frames Total buffer size in frames
/// @param sample_rate Sample rate in Hz
/// @return Buffer duration in microseconds
[[nodiscard]] constexpr auto calculate_buffer_time_us(uint32_t const buffer_frames, double const sample_rate) noexcept -> double
{
    return calculate_period_time_us(buffer_frames, sample_rate);
}

/// Calculate latency in milliseconds for a given buffer size
/// @param buffer_frames Buffer size in frames
/// @param sample_rate Sample rate in Hz
/// @return Latency in milliseconds
[[nodiscard]] constexpr auto calculate_latency_ms(uint32_t const buffer_frames, double const sample_rate) noexcept -> double
{
    if (sample_rate <= 0.0) {
        return 0.0;
    }
    return (static_cast<double>(buffer_frames) / sample_rate) * 1000.0;
}

/// Calculate frames per millisecond for a given sample rate
/// @param sample_rate Sample rate in Hz
/// @return Frames per millisecond
[[nodiscard]] constexpr auto frames_per_ms(double const sample_rate) noexcept -> double
{
    return sample_rate / 1000.0;
}

/// Calculate minimum buffer size for target latency
/// @param target_latency_ms Desired latency in milliseconds
/// @param sample_rate Sample rate in Hz
/// @return Minimum buffer size in frames (rounded up)
[[nodiscard]] constexpr auto frames_for_latency_ms(double const target_latency_ms, double const sample_rate) noexcept -> uint32_t
{
    if (target_latency_ms <= 0.0 || sample_rate <= 0.0) {
        return 0;
    }
    // Round up to ensure we meet or exceed the target latency
    return static_cast<uint32_t>((target_latency_ms * sample_rate / 1000.0) + 0.999999);
}

/// Common sample rates as double for timing calculations
namespace sample_rates {
constexpr double rate_44100 = 44100.0;
constexpr double rate_48000 = 48000.0;
constexpr double rate_96000 = 96000.0;
constexpr double rate_192000 = 192000.0;
}  // namespace sample_rates

}  // namespace statusbar::audio
