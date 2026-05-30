// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Unit tests for audio timing calculations
// Tests the platform-independent timing functions

#include "statusbar/audio/audio.hpp"
#include "statusbar/test/test.hpp"

#include <cmath>
#include <cstdint>
#include <print>

using namespace statusbar::audio;

//
// Static asserts: Compile-time verification of timing constexpr functions
//

// Sample rate constants
static_assert(sample_rates::rate_44100 == 44100.0, "44.1kHz constant");
static_assert(sample_rates::rate_48000 == 48000.0, "48kHz constant");
static_assert(sample_rates::rate_96000 == 96000.0, "96kHz constant");
static_assert(sample_rates::rate_192000 == 192000.0, "192kHz constant");

// frames_to_seconds constexpr verification
static_assert(frames_to_seconds(0, 48000.0) == 0.0, "0 frames = 0 seconds");
static_assert(frames_to_seconds(48000, 48000.0) == 1.0, "48000 frames at 48kHz = 1 second");
static_assert(frames_to_seconds(24000, 48000.0) == 0.5, "24000 frames at 48kHz = 0.5 seconds");
static_assert(frames_to_seconds(1000, 0.0) == 0.0, "Zero sample rate returns 0");

// seconds_to_frames constexpr verification
static_assert(seconds_to_frames(0.0, 48000.0) == 0, "0 seconds = 0 frames");
static_assert(seconds_to_frames(1.0, 48000.0) == 48000, "1 second at 48kHz = 48000 frames");
static_assert(seconds_to_frames(-1.0, 48000.0) == 0, "Negative seconds = 0 frames");
static_assert(seconds_to_frames(1.0, 0.0) == 0, "Zero sample rate = 0 frames");

// frames_per_ms constexpr verification
static_assert(frames_per_ms(48000.0) == 48.0, "48kHz = 48 frames/ms");
static_assert(frames_per_ms(44100.0) == 44.1, "44.1kHz = 44.1 frames/ms");
static_assert(frames_per_ms(96000.0) == 96.0, "96kHz = 96 frames/ms");

// frames_for_latency_ms constexpr verification
static_assert(frames_for_latency_ms(10.0, 48000.0) == 480, "10ms at 48kHz = 480 frames");
static_assert(frames_for_latency_ms(5.0, 48000.0) == 240, "5ms at 48kHz = 240 frames");
static_assert(frames_for_latency_ms(0.0, 48000.0) == 0, "0ms = 0 frames");
static_assert(frames_for_latency_ms(-10.0, 48000.0) == 0, "Negative latency = 0 frames");
static_assert(frames_for_latency_ms(10.0, 0.0) == 0, "Zero sample rate = 0 frames");

// calculate_latency_ms constexpr verification
static_assert(calculate_latency_ms(480, 48000.0) == 10.0, "480 frames at 48kHz = 10ms");
static_assert(calculate_latency_ms(0, 48000.0) == 0.0, "0 frames = 0ms");
static_assert(calculate_latency_ms(512, 0.0) == 0.0, "Zero sample rate = 0ms");

// calculate_period_time_us constexpr verification
static_assert(calculate_period_time_us(48, 48000.0) == 1000.0, "48 frames at 48kHz = 1000us (1ms)");
static_assert(calculate_period_time_us(0, 48000.0) == 0.0, "0 frames = 0us");
static_assert(calculate_period_time_us(512, 0.0) == 0.0, "Zero sample rate = 0us");

// Frames to seconds tests

TEST(audio_timing, frames_to_seconds_44100)
{
    // 44100 frames at 44100 Hz = 1 second
    double result = frames_to_seconds(44100, sample_rates::rate_44100);
    EXPECT_TRUE(std::fabs(result - 1.0) < 0.0001);

    // 22050 frames at 44100 Hz = 0.5 seconds
    result = frames_to_seconds(22050, sample_rates::rate_44100);
    EXPECT_TRUE(std::fabs(result - 0.5) < 0.0001);

    // 0 frames = 0 seconds
    result = frames_to_seconds(0, sample_rates::rate_44100);
    EXPECT_EQ(result, 0.0);
}

TEST(audio_timing, frames_to_seconds_48000)
{
    // 48000 frames at 48000 Hz = 1 second
    double result = frames_to_seconds(48000, sample_rates::rate_48000);
    EXPECT_TRUE(std::fabs(result - 1.0) < 0.0001);

    // 24000 frames at 48000 Hz = 0.5 seconds
    result = frames_to_seconds(24000, sample_rates::rate_48000);
    EXPECT_TRUE(std::fabs(result - 0.5) < 0.0001);
}

TEST(audio_timing, frames_to_seconds_96000)
{
    // 96000 frames at 96000 Hz = 1 second
    double result = frames_to_seconds(96000, sample_rates::rate_96000);
    EXPECT_TRUE(std::fabs(result - 1.0) < 0.0001);
}

TEST(audio_timing, frames_to_seconds_large_count)
{
    // Test large frame counts (1 hour at 48kHz)
    uint64_t one_hour_frames = 48000 * 60 * 60;
    double result = frames_to_seconds(one_hour_frames, sample_rates::rate_48000);
    EXPECT_TRUE(std::fabs(result - 3600.0) < 0.001);
}

TEST(audio_timing, frames_to_seconds_zero_rate)
{
    // Zero sample rate should return 0
    double result = frames_to_seconds(48000, 0.0);
    EXPECT_EQ(result, 0.0);
}

// Seconds to frames tests

TEST(audio_timing, seconds_to_frames_48000)
{
    // 1 second at 48000 Hz = 48000 frames
    uint64_t result = seconds_to_frames(1.0, sample_rates::rate_48000);
    EXPECT_EQ(result, 48000);

    // 0.5 seconds at 48000 Hz = 24000 frames
    result = seconds_to_frames(0.5, sample_rates::rate_48000);
    EXPECT_EQ(result, 24000);
}

TEST(audio_timing, seconds_to_frames_44100)
{
    // 1 second at 44100 Hz = 44100 frames
    uint64_t result = seconds_to_frames(1.0, sample_rates::rate_44100);
    EXPECT_EQ(result, 44100);
}

TEST(audio_timing, seconds_to_frames_zero)
{
    // 0 seconds = 0 frames
    uint64_t result = seconds_to_frames(0.0, sample_rates::rate_48000);
    EXPECT_EQ(result, 0);

    // Negative seconds = 0 frames
    result = seconds_to_frames(-1.0, sample_rates::rate_48000);
    EXPECT_EQ(result, 0);

    // Zero rate = 0 frames
    result = seconds_to_frames(1.0, 0.0);
    EXPECT_EQ(result, 0);
}

TEST(audio_timing, seconds_to_frames_roundtrip)
{
    // Convert frames -> seconds -> frames should be equal
    uint64_t original = 48000;
    double seconds = frames_to_seconds(original, sample_rates::rate_48000);
    uint64_t recovered = seconds_to_frames(seconds, sample_rates::rate_48000);
    EXPECT_EQ(recovered, original);
}

// Period/buffer time tests

TEST(audio_timing, period_time_us_512)
{
    // 512 frames at 48000 Hz = 10.667 ms = 10667 us
    double result = calculate_period_time_us(512, sample_rates::rate_48000);
    EXPECT_TRUE(std::fabs(result - 10666.67) < 1.0);
}

TEST(audio_timing, period_time_us_256)
{
    // 256 frames at 48000 Hz = 5.333 ms = 5333 us
    double result = calculate_period_time_us(256, sample_rates::rate_48000);
    EXPECT_TRUE(std::fabs(result - 5333.33) < 1.0);
}

TEST(audio_timing, buffer_time_equals_period_time)
{
    // buffer_time_us and period_time_us should be equivalent
    double period = calculate_period_time_us(512, sample_rates::rate_48000);
    double buffer = calculate_buffer_time_us(512, sample_rates::rate_48000);
    EXPECT_EQ(period, buffer);
}

// Latency calculation tests

TEST(audio_timing, latency_ms_512)
{
    // 512 frames at 48000 Hz = 10.667 ms
    double result = calculate_latency_ms(512, sample_rates::rate_48000);
    EXPECT_TRUE(std::fabs(result - 10.667) < 0.01);
}

TEST(audio_timing, latency_ms_1024)
{
    // 1024 frames at 48000 Hz = 21.333 ms
    double result = calculate_latency_ms(1024, sample_rates::rate_48000);
    EXPECT_TRUE(std::fabs(result - 21.333) < 0.01);
}

TEST(audio_timing, latency_ms_zero_rate)
{
    // Zero sample rate should return 0
    double result = calculate_latency_ms(512, 0.0);
    EXPECT_EQ(result, 0.0);
}

// Frames per millisecond tests

TEST(audio_timing, frames_per_ms_48000)
{
    // At 48000 Hz: 48 frames per ms
    double result = frames_per_ms(sample_rates::rate_48000);
    EXPECT_EQ(result, 48.0);
}

TEST(audio_timing, frames_per_ms_44100)
{
    // At 44100 Hz: 44.1 frames per ms
    double result = frames_per_ms(sample_rates::rate_44100);
    EXPECT_EQ(result, 44.1);
}

TEST(audio_timing, frames_per_ms_96000)
{
    // At 96000 Hz: 96 frames per ms
    double result = frames_per_ms(sample_rates::rate_96000);
    EXPECT_EQ(result, 96.0);
}

// Frames for latency tests

TEST(audio_timing, frames_for_latency_10ms)
{
    // 10 ms at 48000 Hz = 480 frames
    uint32_t result = frames_for_latency_ms(10.0, sample_rates::rate_48000);
    EXPECT_EQ(result, 480);
}

TEST(audio_timing, frames_for_latency_5ms)
{
    // 5 ms at 48000 Hz = 240 frames
    uint32_t result = frames_for_latency_ms(5.0, sample_rates::rate_48000);
    EXPECT_EQ(result, 240);
}

TEST(audio_timing, frames_for_latency_rounds_up)
{
    // 10.5 ms at 48000 Hz = 504 frames (should round up)
    uint32_t result = frames_for_latency_ms(10.5, sample_rates::rate_48000);
    EXPECT_EQ(result, 504);

    // 10.1 ms at 48000 Hz = 484.8 frames -> 485 (round up)
    result = frames_for_latency_ms(10.1, sample_rates::rate_48000);
    EXPECT_TRUE(result >= 485);
}

TEST(audio_timing, frames_for_latency_zero)
{
    // Zero latency = 0 frames
    uint32_t result = frames_for_latency_ms(0.0, sample_rates::rate_48000);
    EXPECT_EQ(result, 0);

    // Negative latency = 0 frames
    result = frames_for_latency_ms(-10.0, sample_rates::rate_48000);
    EXPECT_EQ(result, 0);

    // Zero rate = 0 frames
    result = frames_for_latency_ms(10.0, 0.0);
    EXPECT_EQ(result, 0);
}

// Edge case tests

TEST(audio_timing_edge, very_high_sample_rate)
{
    // Test with 192kHz sample rate
    double result = frames_to_seconds(192000, sample_rates::rate_192000);
    EXPECT_TRUE(std::fabs(result - 1.0) < 0.0001);

    uint64_t frames = seconds_to_frames(1.0, sample_rates::rate_192000);
    EXPECT_EQ(frames, 192000);

    // 10ms at 192kHz = 1920 frames
    uint32_t latency_frames = frames_for_latency_ms(10.0, sample_rates::rate_192000);
    EXPECT_EQ(latency_frames, 1920);
}

TEST(audio_timing_edge, very_large_frame_count)
{
    // Test 24-hour playback at 96kHz (8.294 billion frames)
    uint64_t frames_24h = static_cast<uint64_t>(96000) * 60 * 60 * 24;
    double seconds = frames_to_seconds(frames_24h, sample_rates::rate_96000);
    EXPECT_TRUE(std::fabs(seconds - 86400.0) < 1.0);  // Within 1 second

    // Round-trip
    uint64_t recovered = seconds_to_frames(seconds, sample_rates::rate_96000);
    EXPECT_EQ(recovered, frames_24h);
}

TEST(audio_timing_edge, fractional_ms_latency)
{
    // 0.5ms at 48kHz = 24 frames
    uint32_t frames = frames_for_latency_ms(0.5, sample_rates::rate_48000);
    EXPECT_EQ(frames, 24);

    // 0.1ms at 48kHz = 4.8 frames -> 5 (rounded up)
    frames = frames_for_latency_ms(0.1, sample_rates::rate_48000);
    EXPECT_TRUE(frames >= 4 && frames <= 5);
}

TEST(audio_timing_edge, period_time_common_sizes)
{
    // 64 frames at 48kHz = 1.333ms = 1333.33us
    double us_64 = calculate_period_time_us(64, sample_rates::rate_48000);
    EXPECT_TRUE(std::fabs(us_64 - 1333.33) < 1.0);

    // 128 frames at 48kHz = 2.667ms = 2666.67us
    double us_128 = calculate_period_time_us(128, sample_rates::rate_48000);
    EXPECT_TRUE(std::fabs(us_128 - 2666.67) < 1.0);

    // 1024 frames at 48kHz = 21.333ms = 21333.33us
    double us_1024 = calculate_period_time_us(1024, sample_rates::rate_48000);
    EXPECT_TRUE(std::fabs(us_1024 - 21333.33) < 1.0);
}

TEST(audio_timing_edge, very_small_frame_count)
{
    // 1 frame at 48kHz
    double seconds = frames_to_seconds(1, sample_rates::rate_48000);
    double expected = 1.0 / 48000.0;
    EXPECT_TRUE(std::fabs(seconds - expected) < 1e-10);

    double latency_ms = calculate_latency_ms(1, sample_rates::rate_48000);
    EXPECT_TRUE(std::fabs(latency_ms - (1000.0 / 48000.0)) < 0.001);
}

TEST(audio_timing_edge, frames_per_ms_all_rates)
{
    // Verify frames_per_ms for all standard sample rates
    EXPECT_EQ(frames_per_ms(sample_rates::rate_44100), 44.1);
    EXPECT_EQ(frames_per_ms(sample_rates::rate_48000), 48.0);
    EXPECT_EQ(frames_per_ms(sample_rates::rate_96000), 96.0);
    EXPECT_EQ(frames_per_ms(sample_rates::rate_192000), 192.0);
}

// Sample rate constants tests

TEST(audio_timing, sample_rate_constants)
{
    EXPECT_EQ(sample_rates::rate_44100, 44100.0);
    EXPECT_EQ(sample_rates::rate_48000, 48000.0);
    EXPECT_EQ(sample_rates::rate_96000, 96000.0);
    EXPECT_EQ(sample_rates::rate_192000, 192000.0);
}

// Test runner

TEST_MAIN(statusbar_audio, audio_timing_test)