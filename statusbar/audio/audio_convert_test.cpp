// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Unit tests for audio sample format conversion
// Tests the platform-independent conversion functions

#include "statusbar/audio/audio.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <print>
#include <span>
#include <vector>

using namespace statusbar::audio;

//
// Static asserts: Compile-time verification of conversion constexpr functions
//

// clamp_sample constexpr verification
static_assert(clamp_sample(0.0f) == 0.0f, "Zero passes through unchanged");
static_assert(clamp_sample(0.5f) == 0.5f, "0.5 passes through unchanged");
static_assert(clamp_sample(-0.5f) == -0.5f, "-0.5 passes through unchanged");
static_assert(clamp_sample(1.0f) == 1.0f, "1.0 passes through unchanged");
static_assert(clamp_sample(-1.0f) == -1.0f, "-1.0 passes through unchanged");
static_assert(clamp_sample(1.5f) == 1.0f, "Values > 1.0 clamp to 1.0");
static_assert(clamp_sample(-1.5f) == -1.0f, "Values < -1.0 clamp to -1.0");
static_assert(clamp_sample(100.0f) == 1.0f, "Large positive values clamp to 1.0");
static_assert(clamp_sample(-100.0f) == -1.0f, "Large negative values clamp to -1.0");

// convert_sample_s16_to_float constexpr verification
static_assert(convert_sample_s16_to_float(0) == 0.0f, "S16 zero -> float zero");
static_assert(convert_sample_s16_to_float(-32768) == -1.0f, "S16 min -> -1.0f");
// Note: 32767 / 32768.0 = 0.999969... in float

// convert_sample_s32_to_float constexpr verification
static_assert(convert_sample_s32_to_float(0) == 0.0f, "S32 zero -> float zero");
static_assert(convert_sample_s32_to_float(-2147483648) == -1.0f, "S32 min -> -1.0f");

// Single sample conversion tests

TEST(audio_convert, clamp_sample_in_range)
{
    // Values within range should pass through unchanged
    EXPECT_EQ(clamp_sample(0.0f), 0.0f);
    EXPECT_EQ(clamp_sample(0.5f), 0.5f);
    EXPECT_EQ(clamp_sample(-0.5f), -0.5f);
    EXPECT_EQ(clamp_sample(1.0f), 1.0f);
    EXPECT_EQ(clamp_sample(-1.0f), -1.0f);
}

TEST(audio_convert, clamp_sample_overflow)
{
    // Values above 1.0 should clamp to 1.0
    EXPECT_EQ(clamp_sample(1.1f), 1.0f);
    EXPECT_EQ(clamp_sample(2.0f), 1.0f);
    EXPECT_EQ(clamp_sample(100.0f), 1.0f);
}

TEST(audio_convert, clamp_sample_underflow)
{
    // Values below -1.0 should clamp to -1.0
    EXPECT_EQ(clamp_sample(-1.1f), -1.0f);
    EXPECT_EQ(clamp_sample(-2.0f), -1.0f);
    EXPECT_EQ(clamp_sample(-100.0f), -1.0f);
}

// Float to S16 conversion tests

TEST(audio_convert, float_to_s16_boundaries)
{
    // Test boundary values
    EXPECT_EQ(convert_sample_float_to_s16(0.0f), 0);
    EXPECT_EQ(convert_sample_float_to_s16(1.0f), 32767);    // Max positive
    EXPECT_EQ(convert_sample_float_to_s16(-1.0f), -32768);  // Max negative
}

TEST(audio_convert, float_to_s16_midpoints)
{
    // Test midpoint values (approximately half range)
    int16_t half_pos = convert_sample_float_to_s16(0.5f);
    EXPECT_TRUE(half_pos > 16000 && half_pos < 17000);  // Approximately 16384

    int16_t half_neg = convert_sample_float_to_s16(-0.5f);
    EXPECT_TRUE(half_neg < -16000 && half_neg > -17000);  // Approximately -16384
}

TEST(audio_convert, float_to_s16_overflow_clamp)
{
    // Values outside [-1, 1] should clamp
    EXPECT_EQ(convert_sample_float_to_s16(1.5f), 32767);
    EXPECT_EQ(convert_sample_float_to_s16(-1.5f), -32768);
    EXPECT_EQ(convert_sample_float_to_s16(100.0f), 32767);
    EXPECT_EQ(convert_sample_float_to_s16(-100.0f), -32768);
}

// Float to S32 conversion tests

TEST(audio_convert, float_to_s32_boundaries)
{
    // Test boundary values
    EXPECT_EQ(convert_sample_float_to_s32(0.0f), 0);
    EXPECT_EQ(convert_sample_float_to_s32(1.0f), 2147483647);    // Max positive
    EXPECT_EQ(convert_sample_float_to_s32(-1.0f), -2147483648);  // Max negative
}

TEST(audio_convert, float_to_s32_midpoints)
{
    // Test midpoint values
    int32_t half_pos = convert_sample_float_to_s32(0.5f);
    // 0.5 * 2147483648 = 1073741824
    EXPECT_TRUE(half_pos > 1073000000 && half_pos < 1074000000);

    int32_t half_neg = convert_sample_float_to_s32(-0.5f);
    EXPECT_TRUE(half_neg < -1073000000 && half_neg > -1074000000);
}

TEST(audio_convert, float_to_s32_overflow_clamp)
{
    // Values outside [-1, 1] should clamp
    EXPECT_EQ(convert_sample_float_to_s32(1.5f), 2147483647);
    EXPECT_EQ(convert_sample_float_to_s32(-1.5f), -2147483648);
}

// S16 to Float conversion tests

TEST(audio_convert, s16_to_float_boundaries)
{
    // Test boundary values
    EXPECT_EQ(convert_sample_s16_to_float(0), 0.0f);

    float max_pos = convert_sample_s16_to_float(32767);
    EXPECT_TRUE(max_pos > 0.99f && max_pos < 1.0f);  // Close to but not quite 1.0

    float max_neg = convert_sample_s16_to_float(-32768);
    EXPECT_EQ(max_neg, -1.0f);  // Exactly -1.0
}

TEST(audio_convert, s16_to_float_midpoints)
{
    // Test midpoint values
    float half_pos = convert_sample_s16_to_float(16384);
    EXPECT_TRUE(half_pos > 0.49f && half_pos < 0.51f);

    float half_neg = convert_sample_s16_to_float(-16384);
    EXPECT_TRUE(half_neg < -0.49f && half_neg > -0.51f);
}

// S32 to Float conversion tests

TEST(audio_convert, s32_to_float_boundaries)
{
    // Test boundary values
    EXPECT_EQ(convert_sample_s32_to_float(0), 0.0f);

    float max_pos = convert_sample_s32_to_float(2147483647);
    // 2147483647 / 2147483648 = 0.9999999995..., which rounds to 1.0 in float32 precision
    EXPECT_TRUE(max_pos > 0.99f && max_pos <= 1.0f);

    float max_neg = convert_sample_s32_to_float(-2147483648);
    EXPECT_EQ(max_neg, -1.0f);  // Exactly -1.0
}

TEST(audio_convert, s32_to_float_midpoints)
{
    // Test midpoint values
    float half_pos = convert_sample_s32_to_float(1073741824);
    EXPECT_TRUE(half_pos > 0.49f && half_pos < 0.51f);

    float half_neg = convert_sample_s32_to_float(-1073741824);
    EXPECT_TRUE(half_neg < -0.49f && half_neg > -0.51f);
}

// Round-trip tests

TEST(audio_convert, roundtrip_s16)
{
    // Convert float -> s16 -> float should be approximately equal
    std::array<float, 5> test_values = {0.0f, 0.5f, -0.5f, 0.99f, -0.99f};

    for (float original : test_values) {
        int16_t intermediate = convert_sample_float_to_s16(original);
        float recovered = convert_sample_s16_to_float(intermediate);
        float error = std::fabs(original - recovered);
        // 16-bit has about 1/32768 precision = ~0.00003
        EXPECT_TRUE(error < 0.001f);
    }
}

TEST(audio_convert, roundtrip_s32)
{
    // Convert float -> s32 -> float should be very close
    std::array<float, 5> test_values = {0.0f, 0.5f, -0.5f, 0.99f, -0.99f};

    for (float original : test_values) {
        int32_t intermediate = convert_sample_float_to_s32(original);
        float recovered = convert_sample_s32_to_float(intermediate);
        float error = std::fabs(original - recovered);
        // 32-bit should have much better precision
        EXPECT_TRUE(error < 0.0001f);
    }
}

// Buffer conversion tests

TEST(audio_convert, buffer_float_to_s16)
{
    std::array<float, 4> input = {0.0f, 0.5f, -0.5f, 1.0f};
    std::array<int16_t, 4> output{};

    convert_buffer_float_to_s16(input, output);

    EXPECT_EQ(output[0], 0);
    EXPECT_TRUE(output[1] > 16000 && output[1] < 17000);
    EXPECT_TRUE(output[2] < -16000 && output[2] > -17000);
    EXPECT_EQ(output[3], 32767);
}

TEST(audio_convert, buffer_float_to_s32)
{
    std::array<float, 4> input = {0.0f, 0.5f, -0.5f, 1.0f};
    std::array<int32_t, 4> output{};

    convert_buffer_float_to_s32(input, output);

    EXPECT_EQ(output[0], 0);
    EXPECT_TRUE(output[1] > 1073000000);
    EXPECT_TRUE(output[2] < -1073000000);
    EXPECT_EQ(output[3], 2147483647);
}

TEST(audio_convert, buffer_s16_to_float)
{
    std::array<int16_t, 4> input = {0, 16384, -16384, 32767};
    std::array<float, 4> output{};

    convert_buffer_s16_to_float(input, output);

    EXPECT_EQ(output[0], 0.0f);
    EXPECT_TRUE(output[1] > 0.49f && output[1] < 0.51f);
    EXPECT_TRUE(output[2] < -0.49f && output[2] > -0.51f);
    EXPECT_TRUE(output[3] > 0.99f);
}

TEST(audio_convert, buffer_s32_to_float)
{
    std::array<int32_t, 4> input = {0, 1073741824, -1073741824, 2147483647};
    std::array<float, 4> output{};

    convert_buffer_s32_to_float(input, output);

    EXPECT_EQ(output[0], 0.0f);
    EXPECT_TRUE(output[1] > 0.49f && output[1] < 0.51f);
    EXPECT_TRUE(output[2] < -0.49f && output[2] > -0.51f);
    EXPECT_TRUE(output[3] > 0.99f);
}

TEST(audio_convert, buffer_size_mismatch)
{
    // When output is smaller than input, only convert up to output size
    std::array<float, 4> input = {0.1f, 0.2f, 0.3f, 0.4f};
    std::array<int16_t, 2> output{};

    convert_buffer_float_to_s16(input, output);

    // Should have converted only first 2 samples
    EXPECT_TRUE(output[0] != 0);
    EXPECT_TRUE(output[1] != 0);
}

// Pointer interface tests(compatibility) =====

TEST(audio_convert, pointer_interface_float_to_s16)
{
    std::array<float, 3> input = {0.0f, 0.5f, -1.0f};
    std::array<int16_t, 3> output{};

    convert_float_to_s16(input.data(), output.data(), input.size());

    EXPECT_EQ(output[0], 0);
    EXPECT_TRUE(output[1] > 16000);
    EXPECT_EQ(output[2], -32768);
}

TEST(audio_convert, pointer_interface_s16_to_float)
{
    std::array<int16_t, 3> input = {0, 16384, -32768};
    std::array<float, 3> output{};

    convert_s16_to_float(input.data(), output.data(), input.size());

    EXPECT_EQ(output[0], 0.0f);
    EXPECT_TRUE(output[1] > 0.49f && output[1] < 0.51f);
    EXPECT_EQ(output[2], -1.0f);
}

// Edge case tests

TEST(audio_convert_edge, denormal_values)
{
    // Test very small (denormal) values pass through clamp unchanged
    float tiny = 1e-40f;
    EXPECT_EQ(clamp_sample(tiny), tiny);
    EXPECT_EQ(clamp_sample(-tiny), -tiny);
}

TEST(audio_convert_edge, s16_full_range)
{
    // Test all special S16 values
    EXPECT_EQ(convert_sample_float_to_s16(0.0f), 0);
    EXPECT_EQ(convert_sample_float_to_s16(1.0f), 32767);
    EXPECT_EQ(convert_sample_float_to_s16(-1.0f), -32768);

    // Verify max positive S16 converts back correctly
    float from_max = convert_sample_s16_to_float(32767);
    EXPECT_TRUE(from_max > 0.999f && from_max < 1.0f);
}

TEST(audio_convert_edge, s32_full_range)
{
    // Test all special S32 values
    EXPECT_EQ(convert_sample_float_to_s32(0.0f), 0);
    EXPECT_EQ(convert_sample_float_to_s32(1.0f), 2147483647);
    EXPECT_EQ(convert_sample_float_to_s32(-1.0f), -2147483648);

    // Verify max positive S32 converts back correctly
    float from_max = convert_sample_s32_to_float(2147483647);
    EXPECT_TRUE(from_max > 0.999f && from_max <= 1.0f);
}

TEST(audio_convert_edge, empty_buffer_conversion)
{
    // Empty spans should work without issue
    std::span<float const> empty_float{};
    std::span<int16_t> empty_s16{};

    convert_buffer_float_to_s16(empty_float, empty_s16);
    // Should not crash

    std::span<int16_t const> empty_s16_const{};
    std::span<float> empty_float_out{};
    convert_buffer_s16_to_float(empty_s16_const, empty_float_out);
    // Should not crash
}

TEST(audio_convert_edge, output_larger_than_input)
{
    // When output is larger than input, only input.size() samples converted
    std::array<float, 2> input = {0.5f, -0.5f};
    std::array<int16_t, 10> output{};

    convert_buffer_float_to_s16(input, output);

    // First 2 should be converted
    EXPECT_TRUE(output[0] > 16000);
    EXPECT_TRUE(output[1] < -16000);
    // Rest should remain 0
    EXPECT_EQ(output[2], 0);
    EXPECT_EQ(output[9], 0);
}

TEST(audio_convert_edge, pointer_interface_zero_count)
{
    // Zero count should be safe
    float input = 1.0f;
    int16_t output = 0;
    convert_float_to_s16(&input, &output, 0);
    EXPECT_EQ(output, 0);  // Should remain unchanged
}

TEST(audio_convert_edge, alternating_extreme_values)
{
    // Test buffer with alternating extreme values
    std::array<float, 4> input = {1.0f, -1.0f, 1.0f, -1.0f};
    std::array<int16_t, 4> output{};

    convert_buffer_float_to_s16(input, output);

    EXPECT_EQ(output[0], 32767);
    EXPECT_EQ(output[1], -32768);
    EXPECT_EQ(output[2], 32767);
    EXPECT_EQ(output[3], -32768);
}

// NaN guard — clamp_sample maps NaN to 0.0 so downstream float→int
// conversions don't trigger UB on the final static_cast<int32_t>(NaN).
TEST(audio_convert_edge, nan_clamps_to_zero_silence)
{
    float const nan = std::nanf("");

    EXPECT_EQ(clamp_sample(nan), 0.0F);
    EXPECT_EQ(convert_sample_float_to_s16(nan), int16_t{0});
    EXPECT_EQ(convert_sample_float_to_s32(nan), int32_t{0});

    // Buffer paths inherit clamp_sample's NaN handling.
    std::array<float, 4> in{1.0F, nan, -1.0F, nan};
    std::array<int16_t, 4> out16{};
    convert_buffer_float_to_s16(std::span<float const>(in), std::span<int16_t>(out16));
    EXPECT_EQ(out16[0], int16_t{32767});
    EXPECT_EQ(out16[1], int16_t{0});
    EXPECT_EQ(out16[2], int16_t{-32768});
    EXPECT_EQ(out16[3], int16_t{0});

    std::array<int32_t, 4> out32{};
    convert_buffer_float_to_s32(std::span<float const>(in), std::span<int32_t>(out32));
    EXPECT_EQ(out32[1], int32_t{0});
    EXPECT_EQ(out32[3], int32_t{0});
}

// Test runner

TEST_MAIN(statusbar_audio, audio_convert_test)