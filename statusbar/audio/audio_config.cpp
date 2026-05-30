// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/audio/audio_config.hpp"

namespace statusbar::audio {

auto validate_sample_rate(SampleRate const rate, std::vector<uint32_t> const& supported_rates) noexcept -> Status
{
    if (supported_rates.empty()) {
        // If no rates specified, assume any rate is OK
        return success();
    }

    uint32_t const rate_value = static_cast<uint32_t>(rate);
    for (uint32_t const supported : supported_rates) {
        if (supported == rate_value) {
            return success();
        }
    }

    return failure(AudioError::UnsupportedSampleRate);
}

auto validate_channels(uint32_t const requested_channels, uint32_t const max_available) noexcept -> Status
{
    if (requested_channels < min_channels) {
        return failure(AudioError::InvalidConfig);
    }

    if (requested_channels > max_channels) {
        return failure(AudioError::InvalidConfig);
    }

    if (requested_channels > max_available) {
        return failure(AudioError::InvalidConfig);
    }

    return success();
}

auto validate_buffer_frames(uint32_t const requested_frames, uint32_t const device_min, uint32_t const device_max) noexcept
    -> Status
{
    uint32_t const effective_min = device_min > 0 ? device_min : min_buffer_frames;
    uint32_t const effective_max = device_max > 0 ? device_max : max_buffer_frames;

    if (requested_frames < effective_min) {
        return failure(AudioError::InvalidConfig);
    }

    if (requested_frames > effective_max) {
        return failure(AudioError::InvalidConfig);
    }

    return success();
}

auto validate_config(AudioConfig const& config, DeviceInfo const* const device) noexcept -> Status
{
    // Validate channels
    uint32_t const max_out_channels = device != nullptr ? device->max_output_channels : max_channels;
    auto channel_status = validate_channels(config.channels, max_out_channels);
    if (!channel_status) {
        return channel_status;
    }

    // Validate sample rate if device info available
    if (device != nullptr && !device->supported_sample_rates.empty()) {
        auto rate_status = validate_sample_rate(config.sample_rate, device->supported_sample_rates);
        if (!rate_status) {
            return rate_status;
        }
    }

    // Validate buffer size
    auto buffer_status = validate_buffer_frames(config.buffer_frames);
    if (!buffer_status) {
        return buffer_status;
    }

    return success();
}

auto validate_input_config(AudioConfig const& config, DeviceInfo const& device) noexcept -> Status
{
    // Check device has input channels
    if (device.max_input_channels == 0) {
        return failure(AudioError::DeviceNotFound);
    }

    // Validate channels against input capability
    auto channel_status = validate_channels(config.channels, device.max_input_channels);
    if (!channel_status) {
        return channel_status;
    }

    // Validate sample rate
    if (!device.supported_sample_rates.empty()) {
        auto rate_status = validate_sample_rate(config.sample_rate, device.supported_sample_rates);
        if (!rate_status) {
            return rate_status;
        }
    }

    // Validate buffer size
    auto buffer_status = validate_buffer_frames(config.buffer_frames);
    if (!buffer_status) {
        return buffer_status;
    }

    return success();
}

auto validate_output_config(AudioConfig const& config, DeviceInfo const& device) noexcept -> Status
{
    // Check device has output channels
    if (device.max_output_channels == 0) {
        return failure(AudioError::DeviceNotFound);
    }

    // Validate channels against output capability
    auto channel_status = validate_channels(config.channels, device.max_output_channels);
    if (!channel_status) {
        return channel_status;
    }

    // Validate sample rate
    if (!device.supported_sample_rates.empty()) {
        auto rate_status = validate_sample_rate(config.sample_rate, device.supported_sample_rates);
        if (!rate_status) {
            return rate_status;
        }
    }

    // Validate buffer size
    auto buffer_status = validate_buffer_frames(config.buffer_frames);
    if (!buffer_status) {
        return buffer_status;
    }

    return success();
}

}  // namespace statusbar::audio
