#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Audio Module - Sample Format Conversion
// Platform-independent sample format conversion functions
// These are extracted for testability and reuse across platforms

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace statusbar::audio {

/// RT-safe clamp to [-1.0, 1.0]
/// Avoids std::clamp overhead in hot loops.
/// NaN inputs are mapped to 0.0 — without this guard, NaN would propagate
/// through the float→int conversion functions and trigger UB on the final
/// static_cast<int32_t> (NaN comparisons against the int range bounds are
/// always false, so neither clamp branch fires). Silence is the correct
/// fallback at the device boundary; the underlying NaN-producing bug must
/// be fixed upstream, but it shouldn't be allowed to write garbage to the
/// speakers in the meantime.
/// @param x Input sample value to clamp
[[nodiscard]] constexpr auto clamp_sample(float const x) noexcept -> float
{
    if (x > 1.0F) {
        return 1.0F;
    }
    if (x < -1.0F) {
        return -1.0F;
    }
    if (x != x) {  // NaN — only value not equal to itself in IEEE 754
        return 0.0F;
    }
    return x;
}

/// Convert a single float sample to int16
/// Uses symmetric mapping: [-1.0, 1.0] -> [-32768, 32767]
/// @param sample Input float sample in [-1.0, 1.0] range
[[nodiscard]] auto convert_sample_float_to_s16(float sample) noexcept -> int16_t;

/// Convert a single float sample to int32
/// Uses symmetric mapping: [-1.0, 1.0] -> [-2147483648, 2147483647]
/// @param sample Input float sample in [-1.0, 1.0] range
[[nodiscard]] auto convert_sample_float_to_s32(float sample) noexcept -> int32_t;

/// Convert a single int16 sample to float
/// Maps [-32768, 32767] -> [-1.0, ~0.99997]
/// @param sample Input int16 sample
[[nodiscard]] constexpr auto convert_sample_s16_to_float(int16_t const sample) noexcept -> float
{
    constexpr float scale = 1.0F / 32768.0F;
    return static_cast<float>(sample) * scale;
}

/// Convert a single int32 sample to float
/// Maps [-2147483648, 2147483647] -> [-1.0, ~0.9999999995]
/// @param sample Input int32 sample
[[nodiscard]] constexpr auto convert_sample_s32_to_float(int32_t const sample) noexcept -> float
{
    constexpr double scale = 1.0 / 2147483648.0;
    return static_cast<float>(static_cast<double>(sample) * scale);
}

/// Convert float samples to int16 buffer
/// @param input Source float samples
/// @param output Destination int16 buffer (must be at least as large as input)
void convert_buffer_float_to_s16(std::span<float const> input, std::span<int16_t> output) noexcept;

/// Convert float samples to int32 buffer
/// @param input Source float samples
/// @param output Destination int32 buffer (must be at least as large as input)
void convert_buffer_float_to_s32(std::span<float const> input, std::span<int32_t> output) noexcept;

/// Convert int16 samples to float buffer
/// @param input Source int16 samples
/// @param output Destination float buffer (must be at least as large as input)
void convert_buffer_s16_to_float(std::span<int16_t const> input, std::span<float> output) noexcept;

/// Convert int32 samples to float buffer
/// @param input Source int32 samples
/// @param output Destination float buffer (must be at least as large as input)
void convert_buffer_s32_to_float(std::span<int32_t const> input, std::span<float> output) noexcept;

/// Convert float samples using pointer interface (for compatibility with existing code)
/// @param input Source float sample array
/// @param output Destination int16 sample array
/// @param count Number of samples to convert
void convert_float_to_s16(float const* input, int16_t* output, size_t count) noexcept;

/// Convert float samples using pointer interface (for compatibility with existing code)
/// @param input Source float sample array
/// @param output Destination int32 sample array
/// @param count Number of samples to convert
void convert_float_to_s32(float const* input, int32_t* output, size_t count) noexcept;

/// Convert int16 samples using pointer interface (for compatibility with existing code)
/// @param input Source int16 sample array
/// @param output Destination float sample array
/// @param count Number of samples to convert
void convert_s16_to_float(int16_t const* input, float* output, size_t count) noexcept;

/// Convert int32 samples using pointer interface (for compatibility with existing code)
/// @param input Source int32 sample array
/// @param output Destination float sample array
/// @param count Number of samples to convert
void convert_s32_to_float(int32_t const* input, float* output, size_t count) noexcept;

}  // namespace statusbar::audio
