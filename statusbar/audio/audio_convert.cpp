// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/audio/audio_convert.hpp"

namespace statusbar::audio {

auto convert_sample_float_to_s16(float const sample) noexcept -> int16_t
{
    // Use lrintf for proper rounding instead of truncation
    int32_t v = static_cast<int32_t>(std::lrintf(clamp_sample(sample) * 32768.0F));
    // Clamp to int16 range (handles rare overflow from rounding)
    if (v > 32767) {
        v = 32767;
    }
    if (v < -32768) {
        v = -32768;
    }
    return static_cast<int16_t>(v);
}

auto convert_sample_float_to_s32(float const sample) noexcept -> int32_t
{
    // Scale by 2^31 (standard audio convention: -1.0f -> INT32_MIN exactly,
    // +1.0f -> clamped to INT32_MAX, asymmetric by 1 LSB — matches CoreAudio/ALSA).
    double v = clamp_sample(sample) * 2147483648.0;
    // Clamp to int32 range (positive side is one less than scale factor)
    if (v > 2147483647.0) {
        v = 2147483647.0;
    }
    if (v < -2147483648.0) {
        v = -2147483648.0;
    }
    return static_cast<int32_t>(v);
}

void convert_buffer_float_to_s16(std::span<float const> const input, std::span<int16_t> const output) noexcept
{
    size_t const count = input.size() < output.size() ? input.size() : output.size();
    for (size_t i = 0; i < count; ++i) {
        output[i] = convert_sample_float_to_s16(input[i]);
    }
}

void convert_buffer_float_to_s32(std::span<float const> const input, std::span<int32_t> const output) noexcept
{
    size_t const count = input.size() < output.size() ? input.size() : output.size();
    for (size_t i = 0; i < count; ++i) {
        output[i] = convert_sample_float_to_s32(input[i]);
    }
}

void convert_buffer_s16_to_float(std::span<int16_t const> const input, std::span<float> const output) noexcept
{
    size_t const count = input.size() < output.size() ? input.size() : output.size();
    for (size_t i = 0; i < count; ++i) {
        output[i] = convert_sample_s16_to_float(input[i]);
    }
}

void convert_buffer_s32_to_float(std::span<int32_t const> const input, std::span<float> const output) noexcept
{
    size_t const count = input.size() < output.size() ? input.size() : output.size();
    for (size_t i = 0; i < count; ++i) {
        output[i] = convert_sample_s32_to_float(input[i]);
    }
}

void convert_float_to_s16(float const* const input, int16_t* const output, size_t const count) noexcept
{
    for (size_t i = 0; i < count; ++i) {
        output[i] = convert_sample_float_to_s16(input[i]);
    }
}

void convert_float_to_s32(float const* const input, int32_t* const output, size_t const count) noexcept
{
    for (size_t i = 0; i < count; ++i) {
        output[i] = convert_sample_float_to_s32(input[i]);
    }
}

void convert_s16_to_float(int16_t const* const input, float* const output, size_t const count) noexcept
{
    for (size_t i = 0; i < count; ++i) {
        output[i] = convert_sample_s16_to_float(input[i]);
    }
}

void convert_s32_to_float(int32_t const* const input, float* const output, size_t const count) noexcept
{
    for (size_t i = 0; i < count; ++i) {
        output[i] = convert_sample_s32_to_float(input[i]);
    }
}

}  // namespace statusbar::audio
