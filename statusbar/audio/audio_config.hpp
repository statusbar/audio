#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Audio Module - Configuration Validation
// Platform-independent configuration validation functions
// These are extracted for testability and reuse across platforms

#include "statusbar/audio/audio_error.hpp"
#include "statusbar/audio/audio_types.hpp"
#include "statusbar/status/status.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <vector>

namespace statusbar::audio {

/// Minimum allowed buffer size in frames
constexpr uint32_t min_buffer_frames = 16;

/// Maximum allowed buffer size in frames
constexpr uint32_t max_buffer_frames = 65536;

/// Minimum allowed channels
constexpr uint32_t min_channels = 1;

/// Maximum allowed channels
constexpr uint32_t max_channels = 64;

/// Validate that a sample rate is supported by the device
/// @param rate The sample rate to check
/// @param supported_rates List of sample rates supported by the device
/// @return Status indicating success or UnsupportedSampleRate error
[[nodiscard]] auto validate_sample_rate(SampleRate rate, std::vector<uint32_t> const& supported_rates) noexcept -> Status;

/// Validate that the requested channel count is available
/// @param requested_channels Number of channels requested
/// @param max_available Maximum channels available on the device
/// @return Status indicating success or InvalidConfig error
[[nodiscard]] auto validate_channels(uint32_t requested_channels, uint32_t max_available) noexcept -> Status;

/// Validate that the buffer size is within acceptable range
/// @param requested_frames Buffer size in frames
/// @param device_min Minimum buffer size supported by device (0 = use default)
/// @param device_max Maximum buffer size supported by device (0 = use default)
/// @return Status indicating success or InvalidConfig error
[[nodiscard]] auto validate_buffer_frames(uint32_t requested_frames, uint32_t device_min = 0, uint32_t device_max = 0) noexcept
    -> Status;

/// Clamp buffer size to device-supported range
/// @param requested_frames Requested buffer size in frames
/// @param device_min Minimum buffer size supported by device (0 = use default)
/// @param device_max Maximum buffer size supported by device (0 = use default)
/// @return Clamped buffer size
[[nodiscard]] constexpr auto clamp_buffer_frames(
    uint32_t const requested_frames, uint32_t const device_min = 0, uint32_t const device_max = 0) noexcept -> uint32_t
{
    uint32_t const effective_min = device_min > 0 ? device_min : min_buffer_frames;
    uint32_t const effective_max = device_max > 0 ? device_max : max_buffer_frames;

    if (requested_frames < effective_min) {
        return effective_min;
    }

    if (requested_frames > effective_max) {
        return effective_max;
    }

    return requested_frames;
}

/// Validate a complete audio configuration
/// @param config The configuration to validate
/// @param device Device information for capability checking (optional)
/// @return Status indicating success or specific error
[[nodiscard]] auto validate_config(AudioConfig const& config, DeviceInfo const* device = nullptr) noexcept -> Status;

/// Validate input configuration against device capabilities
/// @param config The configuration to validate
/// @param device Device information for capability checking
/// @return Status indicating success or specific error
[[nodiscard]] auto validate_input_config(AudioConfig const& config, DeviceInfo const& device) noexcept -> Status;

/// Validate output configuration against device capabilities
/// @param config The configuration to validate
/// @param device Device information for capability checking
/// @return Status indicating success or specific error
[[nodiscard]] auto validate_output_config(AudioConfig const& config, DeviceInfo const& device) noexcept -> Status;

/// Check if a sample format is supported
/// @param format The format to check
/// @return true if the format is a valid enum value
[[nodiscard]] constexpr auto is_valid_format(SampleFormat const format) noexcept -> bool
{
    switch (format) {
        case SampleFormat::Float32:
        case SampleFormat::Int16:
        case SampleFormat::Int32:
            return true;
    }
    return false;
}

/// Get bytes per sample for a format
/// @param format The sample format
/// @return Bytes per sample (0 for invalid format)
[[nodiscard]] constexpr auto bytes_per_sample(SampleFormat const format) noexcept -> uint32_t
{
    switch (format) {
        case SampleFormat::Float32:
            return 4;
        case SampleFormat::Int16:
            return 2;
        case SampleFormat::Int32:
            return 4;
    }
    return 0;
}

/// Get bytes per frame for a configuration
/// @param config The audio configuration
/// @return Bytes per frame
[[nodiscard]] constexpr auto bytes_per_frame(AudioConfig const& config) noexcept -> uint32_t
{
    return bytes_per_sample(config.format) * config.channels;
}

}  // namespace statusbar::audio
