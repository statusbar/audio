// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Unit tests for audio configuration validation
// Tests the platform-independent configuration validation functions

#include "statusbar/audio/audio.hpp"
#include "statusbar/test/test.hpp"

#include <cstdint>
#include <expected>
#include <print>
#include <system_error>
#include <vector>

using namespace statusbar::audio;

//
// Static asserts: Compile-time verification of constants and constexpr functions
//

// Buffer frame limits
static_assert(min_buffer_frames == 16, "Minimum buffer frames should be 16");
static_assert(max_buffer_frames == 65536, "Maximum buffer frames should be 65536");
static_assert(min_buffer_frames < max_buffer_frames, "Min must be less than max");

// Channel limits
static_assert(min_channels == 1, "Minimum channels should be 1 (mono)");
static_assert(max_channels == 64, "Maximum channels should be 64");
static_assert(min_channels < max_channels, "Min must be less than max");

// SampleFormat enum values
static_assert(static_cast<uint8_t>(SampleFormat::Float32) == 0, "Float32 should be 0");
static_assert(static_cast<uint8_t>(SampleFormat::Int16) == 1, "Int16 should be 1");
static_assert(static_cast<uint8_t>(SampleFormat::Int32) == 2, "Int32 should be 2");

// SampleRate enum values
static_assert(static_cast<uint32_t>(SampleRate::Rate_44100) == 44100, "44.1kHz sample rate");
static_assert(static_cast<uint32_t>(SampleRate::Rate_48000) == 48000, "48kHz sample rate");
static_assert(static_cast<uint32_t>(SampleRate::Rate_96000) == 96000, "96kHz sample rate");
static_assert(static_cast<uint32_t>(SampleRate::Rate_192000) == 192000, "192kHz sample rate");

// ThreadPriority enum values
static_assert(static_cast<uint8_t>(ThreadPriority::Normal) == 0, "Normal priority should be 0");
static_assert(static_cast<uint8_t>(ThreadPriority::Elevated) == 1, "Elevated priority should be 1");
static_assert(static_cast<uint8_t>(ThreadPriority::Realtime) == 2, "Realtime priority should be 2");

// AudioError enum values (first and last)
static_assert(static_cast<uint32_t>(AudioError::DeviceNotFound) == 1, "First error should be 1");
static_assert(static_cast<uint32_t>(AudioError::InitializationFailed) == 12, "Last error should be 12");

// is_valid_format constexpr verification
static_assert(is_valid_format(SampleFormat::Float32), "Float32 is valid");
static_assert(is_valid_format(SampleFormat::Int16), "Int16 is valid");
static_assert(is_valid_format(SampleFormat::Int32), "Int32 is valid");

// bytes_per_sample constexpr verification
static_assert(bytes_per_sample(SampleFormat::Float32) == 4, "Float32 is 4 bytes");
static_assert(bytes_per_sample(SampleFormat::Int16) == 2, "Int16 is 2 bytes");
static_assert(bytes_per_sample(SampleFormat::Int32) == 4, "Int32 is 4 bytes");

// clamp_buffer_frames constexpr verification
static_assert(clamp_buffer_frames(512) == 512, "In-range value passes through");
static_assert(clamp_buffer_frames(8) == min_buffer_frames, "Too small clamps to min");
static_assert(clamp_buffer_frames(0) == min_buffer_frames, "Zero clamps to min");
static_assert(clamp_buffer_frames(100000) == max_buffer_frames, "Too large clamps to max");
static_assert(clamp_buffer_frames(64, 128, 1024) == 128, "Below device min clamps");
static_assert(clamp_buffer_frames(2048, 128, 1024) == 1024, "Above device max clamps");
static_assert(clamp_buffer_frames(512, 128, 1024) == 512, "In device range passes through");

// AudioConfig defaults
static_assert(AudioConfig{}.channels == 2, "Default channels is stereo");
static_assert(AudioConfig{}.buffer_frames == 512, "Default buffer frames is 512");
static_assert(AudioConfig{}.non_interleaved == true, "Default is non-interleaved");
static_assert(static_cast<uint32_t>(AudioConfig{}.sample_rate) == 48000, "Default sample rate is 48kHz");
static_assert(AudioConfig{}.format == SampleFormat::Float32, "Default format is Float32");
static_assert(AudioConfig{}.thread_priority == ThreadPriority::Elevated, "Default thread priority is Elevated");

// DeviceInfo defaults
static_assert(DeviceInfo{}.max_input_channels == 0, "Default max input channels is 0");
static_assert(DeviceInfo{}.max_output_channels == 0, "Default max output channels is 0");
static_assert(DeviceInfo{}.is_default_input == false, "Default is_default_input is false");
static_assert(DeviceInfo{}.is_default_output == false, "Default is_default_output is false");

// bytes_per_frame constexpr verification (requires AudioConfig)
static_assert(bytes_per_frame(AudioConfig{.channels = 2, .format = SampleFormat::Float32}) == 8, "Stereo float32 is 8 bytes/frame");
static_assert(bytes_per_frame(AudioConfig{.channels = 1, .format = SampleFormat::Int16}) == 2, "Mono int16 is 2 bytes/frame");
static_assert(bytes_per_frame(AudioConfig{.channels = 8, .format = SampleFormat::Int32}) == 32, "8ch int32 is 32 bytes/frame");

// Sample rate validation tests

TEST(audio_config, validate_sample_rate_supported)
{
    std::vector<uint32_t> supported = {44100, 48000, 96000};

    auto result = validate_sample_rate(SampleRate::Rate_44100, supported);
    EXPECT_TRUE(result.has_value());

    result = validate_sample_rate(SampleRate::Rate_48000, supported);
    EXPECT_TRUE(result.has_value());

    result = validate_sample_rate(SampleRate::Rate_96000, supported);
    EXPECT_TRUE(result.has_value());
}

TEST(audio_config, validate_sample_rate_unsupported)
{
    std::vector<uint32_t> supported = {44100, 48000};

    auto result = validate_sample_rate(SampleRate::Rate_96000, supported);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), make_error_code(AudioError::UnsupportedSampleRate));
}

TEST(audio_config, validate_sample_rate_empty_list)
{
    // Empty list means any rate is OK
    std::vector<uint32_t> supported;

    auto result = validate_sample_rate(SampleRate::Rate_192000, supported);
    EXPECT_TRUE(result.has_value());
}

// Channel validation tests

TEST(audio_config, validate_channels_valid)
{
    // 2 channels with 8 available
    auto result = validate_channels(2, 8);
    EXPECT_TRUE(result.has_value());

    // 1 channel (mono) with 2 available
    result = validate_channels(1, 2);
    EXPECT_TRUE(result.has_value());

    // Maximum matching available
    result = validate_channels(8, 8);
    EXPECT_TRUE(result.has_value());
}

TEST(audio_config, validate_channels_too_many)
{
    // Requesting more than available
    auto result = validate_channels(8, 2);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), make_error_code(AudioError::InvalidConfig));
}

TEST(audio_config, validate_channels_zero)
{
    // Zero channels is invalid
    auto result = validate_channels(0, 8);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), make_error_code(AudioError::InvalidConfig));
}

TEST(audio_config, validate_channels_exceeds_max)
{
    // More than max_channels (64) is invalid even if device claims to support it
    auto result = validate_channels(100, 100);
    EXPECT_FALSE(result.has_value());
}

// Buffer size validation tests

TEST(audio_config, validate_buffer_frames_valid)
{
    // Valid buffer sizes with default limits
    auto result = validate_buffer_frames(256);
    EXPECT_TRUE(result.has_value());

    result = validate_buffer_frames(512);
    EXPECT_TRUE(result.has_value());

    result = validate_buffer_frames(1024);
    EXPECT_TRUE(result.has_value());
}

TEST(audio_config, validate_buffer_frames_too_small)
{
    // Too small (below min_buffer_frames = 16)
    auto result = validate_buffer_frames(8);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), make_error_code(AudioError::InvalidConfig));

    result = validate_buffer_frames(0);
    EXPECT_FALSE(result.has_value());
}

TEST(audio_config, validate_buffer_frames_too_large)
{
    // Too large (above max_buffer_frames = 65536)
    auto result = validate_buffer_frames(100000);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), make_error_code(AudioError::InvalidConfig));
}

TEST(audio_config, validate_buffer_frames_device_limits)
{
    // Valid within device-specific limits
    auto result = validate_buffer_frames(256, 128, 1024);
    EXPECT_TRUE(result.has_value());

    // Below device minimum
    result = validate_buffer_frames(64, 128, 1024);
    EXPECT_FALSE(result.has_value());

    // Above device maximum
    result = validate_buffer_frames(2048, 128, 1024);
    EXPECT_FALSE(result.has_value());
}

// Buffer size clamping tests

TEST(audio_config, clamp_buffer_frames_in_range)
{
    // Value in range should pass through
    EXPECT_EQ(clamp_buffer_frames(512), 512);
    EXPECT_EQ(clamp_buffer_frames(1024), 1024);
}

TEST(audio_config, clamp_buffer_frames_too_small)
{
    // Too small should clamp to minimum
    EXPECT_EQ(clamp_buffer_frames(8), min_buffer_frames);
    EXPECT_EQ(clamp_buffer_frames(0), min_buffer_frames);
}

TEST(audio_config, clamp_buffer_frames_too_large)
{
    // Too large should clamp to maximum
    EXPECT_EQ(clamp_buffer_frames(100000), max_buffer_frames);
}

TEST(audio_config, clamp_buffer_frames_device_limits)
{
    // Clamp with device-specific limits
    EXPECT_EQ(clamp_buffer_frames(64, 128, 1024), 128);     // Below device min
    EXPECT_EQ(clamp_buffer_frames(2048, 128, 1024), 1024);  // Above device max
    EXPECT_EQ(clamp_buffer_frames(512, 128, 1024), 512);    // In range
}

// Full config validation tests

TEST(audio_config, validate_config_valid)
{
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .format = SampleFormat::Float32, .buffer_frames = 512};

    auto result = validate_config(config);
    EXPECT_TRUE(result.has_value());
}

TEST(audio_config, validate_config_with_device)
{
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .format = SampleFormat::Float32, .buffer_frames = 512};

    DeviceInfo device{
        .name = "Test Device",
        .uid = "test",
        .max_input_channels = 2,
        .max_output_channels = 8,
        .supported_sample_rates = {44100, 48000, 96000}};

    auto result = validate_config(config, &device);
    EXPECT_TRUE(result.has_value());
}

TEST(audio_config, validate_config_unsupported_rate)
{
    AudioConfig config{
        .sample_rate = SampleRate::Rate_192000, .channels = 2, .format = SampleFormat::Float32, .buffer_frames = 512};

    DeviceInfo device{.name = "Test Device", .uid = "test", .max_output_channels = 8, .supported_sample_rates = {44100, 48000}};

    auto result = validate_config(config, &device);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), make_error_code(AudioError::UnsupportedSampleRate));
}

TEST(audio_config, validate_config_too_many_channels)
{
    AudioConfig config{
        .sample_rate = SampleRate::Rate_48000, .channels = 16, .format = SampleFormat::Float32, .buffer_frames = 512};

    DeviceInfo device{.name = "Test Device", .uid = "test", .max_output_channels = 2, .supported_sample_rates = {48000}};

    auto result = validate_config(config, &device);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), make_error_code(AudioError::InvalidConfig));
}

// Input/output specific validation tests

TEST(audio_config, validate_input_config_valid)
{
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .format = SampleFormat::Float32, .buffer_frames = 512};

    DeviceInfo device{
        .name = "Test Device",
        .uid = "test",
        .max_input_channels = 8,
        .max_output_channels = 0,  // No output
        .supported_sample_rates = {48000}};

    auto result = validate_input_config(config, device);
    EXPECT_TRUE(result.has_value());
}

TEST(audio_config, validate_input_config_no_input)
{
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .format = SampleFormat::Float32, .buffer_frames = 512};

    DeviceInfo device{
        .name = "Output Only Device",
        .uid = "test",
        .max_input_channels = 0,  // No input
        .max_output_channels = 2,
        .supported_sample_rates = {48000}};

    auto result = validate_input_config(config, device);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), make_error_code(AudioError::DeviceNotFound));
}

TEST(audio_config, validate_output_config_valid)
{
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .format = SampleFormat::Float32, .buffer_frames = 512};

    DeviceInfo device{
        .name = "Test Device", .uid = "test", .max_input_channels = 0, .max_output_channels = 8, .supported_sample_rates = {48000}};

    auto result = validate_output_config(config, device);
    EXPECT_TRUE(result.has_value());
}

TEST(audio_config, validate_output_config_no_output)
{
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .format = SampleFormat::Float32, .buffer_frames = 512};

    DeviceInfo device{
        .name = "Input Only Device",
        .uid = "test",
        .max_input_channels = 2,
        .max_output_channels = 0,  // No output
        .supported_sample_rates = {48000}};

    auto result = validate_output_config(config, device);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), make_error_code(AudioError::DeviceNotFound));
}

// Format helper tests

TEST(audio_config, is_valid_format)
{
    EXPECT_TRUE(is_valid_format(SampleFormat::Float32));
    EXPECT_TRUE(is_valid_format(SampleFormat::Int16));
    EXPECT_TRUE(is_valid_format(SampleFormat::Int32));
}

TEST(audio_config, bytes_per_sample)
{
    EXPECT_EQ(bytes_per_sample(SampleFormat::Float32), 4);
    EXPECT_EQ(bytes_per_sample(SampleFormat::Int16), 2);
    EXPECT_EQ(bytes_per_sample(SampleFormat::Int32), 4);
}

TEST(audio_config, bytes_per_frame)
{
    AudioConfig stereo_float{
        .sample_rate = SampleRate::Rate_48000, .channels = 2, .format = SampleFormat::Float32, .buffer_frames = 512};
    EXPECT_EQ(bytes_per_frame(stereo_float), 8);  // 2 channels * 4 bytes

    AudioConfig mono_s16{.sample_rate = SampleRate::Rate_48000, .channels = 1, .format = SampleFormat::Int16, .buffer_frames = 512};
    EXPECT_EQ(bytes_per_frame(mono_s16), 2);  // 1 channel * 2 bytes

    AudioConfig multichannel{
        .sample_rate = SampleRate::Rate_48000, .channels = 8, .format = SampleFormat::Int32, .buffer_frames = 512};
    EXPECT_EQ(bytes_per_frame(multichannel), 32);  // 8 channels * 4 bytes
}

// Constant tests

TEST(audio_config, constants)
{
    EXPECT_EQ(min_buffer_frames, 16);
    EXPECT_EQ(max_buffer_frames, 65536);
    EXPECT_EQ(min_channels, 1);
    EXPECT_EQ(max_channels, 64);
}

// Edge case tests

TEST(audio_config_edge, boundary_buffer_frames)
{
    // Exact boundary values
    auto result = validate_buffer_frames(min_buffer_frames);
    EXPECT_TRUE(result.has_value());

    result = validate_buffer_frames(max_buffer_frames);
    EXPECT_TRUE(result.has_value());

    // One below/above boundary
    result = validate_buffer_frames(min_buffer_frames - 1);
    EXPECT_FALSE(result.has_value());

    result = validate_buffer_frames(max_buffer_frames + 1);
    EXPECT_FALSE(result.has_value());
}

TEST(audio_config_edge, boundary_channels)
{
    // Exact boundary values
    auto result = validate_channels(min_channels, max_channels);
    EXPECT_TRUE(result.has_value());

    result = validate_channels(max_channels, max_channels);
    EXPECT_TRUE(result.has_value());

    // One above max
    result = validate_channels(max_channels + 1, max_channels + 1);
    EXPECT_FALSE(result.has_value());
}

TEST(audio_config_edge, device_buffer_overrides_defaults)
{
    // Device min > default min
    auto result = validate_buffer_frames(32, 64, 1024);
    EXPECT_FALSE(result.has_value());  // 32 < device min 64

    // Device max < default max
    result = validate_buffer_frames(2048, 128, 1024);
    EXPECT_FALSE(result.has_value());  // 2048 > device max 1024

    // Device limits that allow smaller than default
    result = validate_buffer_frames(8, 4, 1024);
    EXPECT_TRUE(result.has_value());  // 8 >= device min 4
}

TEST(audio_config_edge, clamp_with_zero_device_limits)
{
    // Zero device min/max should use defaults
    EXPECT_EQ(clamp_buffer_frames(8, 0, 0), min_buffer_frames);
    EXPECT_EQ(clamp_buffer_frames(100000, 0, 0), max_buffer_frames);
}

TEST(audio_config_edge, all_error_codes_have_messages)
{
    // Verify every AudioError has a non-empty message
    auto const& cat = audio_error_category();
    for (uint32_t i = 1; i <= 12; ++i) {
        std::string msg = cat.message(static_cast<int>(i));
        EXPECT_FALSE(msg.empty());
        EXPECT_NE(msg, "Unknown audio error");
    }
}

TEST(audio_config_edge, unknown_error_code)
{
    auto const& cat = audio_error_category();
    std::string msg = cat.message(999);
    EXPECT_EQ(msg, "Unknown audio error");
}

TEST(audio_config_edge, error_code_comparison)
{
    auto ec1 = make_error_code(AudioError::DeviceNotFound);
    auto ec2 = make_error_code(AudioError::DeviceNotFound);
    auto ec3 = make_error_code(AudioError::DeviceBusy);

    EXPECT_TRUE(ec1 == ec2);
    EXPECT_FALSE(ec1 == ec3);
}

TEST(audio_config_edge, error_category_name)
{
    auto const& cat = audio_error_category();
    EXPECT_EQ(std::string{cat.name()}, "statusbar.audio");
}

TEST(audio_config_edge, validate_config_all_rates_with_empty_device)
{
    // Device with no sample rate restrictions
    DeviceInfo device{.max_output_channels = 8};  // Empty supported_sample_rates

    AudioConfig config{.sample_rate = SampleRate::Rate_192000, .channels = 2, .buffer_frames = 512};
    auto result = validate_config(config, &device);
    EXPECT_TRUE(result.has_value());  // Should pass since device has no restrictions
}

TEST(audio_config_edge, max_channels_device)
{
    // Device with exactly max_channels
    DeviceInfo device{.max_output_channels = max_channels, .supported_sample_rates = {48000}};

    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = max_channels, .buffer_frames = 512};
    auto result = validate_output_config(config, device);
    EXPECT_TRUE(result.has_value());
}

TEST(audio_config_edge, validate_with_null_device)
{
    // validate_config with nullptr device should use max_channels as limit
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = max_channels, .buffer_frames = 512};
    auto result = validate_config(config, nullptr);
    EXPECT_TRUE(result.has_value());

    // Exceeding max_channels should fail even without device
    AudioConfig bad_config{.sample_rate = SampleRate::Rate_48000, .channels = max_channels + 1, .buffer_frames = 512};
    result = validate_config(bad_config, nullptr);
    EXPECT_FALSE(result.has_value());
}

TEST(audio_config_edge, mono_configuration)
{
    // Mono (1 channel) should be valid
    AudioConfig mono{.sample_rate = SampleRate::Rate_48000, .channels = 1, .buffer_frames = 256};
    auto result = validate_config(mono, nullptr);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(bytes_per_frame(mono), 4);  // 1 channel * 4 bytes (Float32)
}

// Test runner

TEST_MAIN(statusbar_audio, audio_config_test)