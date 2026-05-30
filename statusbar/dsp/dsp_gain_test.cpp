// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Unit tests for statusbar.dsp:gain module

#include "statusbar/dsp/dsp.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <span>
#include <vector>

using namespace statusbar::dsp;

// Type Traits Tests

TEST(gain_traits, value_type)
{
    using GainDouble = Gain<double>;
    static_assert(std::is_same_v<GainDouble::value_type, double>);
    static_assert(std::is_same_v<GainDouble::item_type, double>);
}

TEST(gain_traits, vector_size_scalar)
{
    using GainDouble = Gain<double>;
    EXPECT_EQ(GainDouble::vector_size, 1U);
    EXPECT_EQ(GainDouble::flattened_size, 1U);
}

TEST(gain_traits, vector_size_simd)
{
    using GainSimd = Gain<simd_float32x4>;
    EXPECT_EQ(GainSimd::vector_size, 4U);
    EXPECT_EQ(GainSimd::flattened_size, 4U);
}

// Construction Tests

TEST(gain_construction, default_ctor)
{
    Gain<double> gain;
    // Should compile and not crash
    (void)gain;
}

TEST(gain_construction, copy_ctor)
{
    Gain<double> gain1;
    gain1.coeffs.set_amplitude(0.5);
    Gain<double> gain2 = gain1;
    EXPECT_EQ(gain2.coeffs.amplitude, 0.5);
}

TEST(gain_construction, copy_assign)
{
    Gain<double> gain1;
    gain1.coeffs.set_amplitude(0.5);
    Gain<double> gain2;
    gain2 = gain1;
    EXPECT_EQ(gain2.coeffs.amplitude, 0.5);
}

TEST(gain_construction, move_ctor)
{
    Gain<double> gain1;
    gain1.coeffs.set_amplitude(0.5);
    Gain<double> gain2 = std::move(gain1);
    EXPECT_EQ(gain2.coeffs.amplitude, 0.5);
}

TEST(gain_construction, move_assignment)
{
    Gain<double> gain1;
    gain1.coeffs.set_amplitude(0.5);
    Gain<double> gain2;
    gain2 = std::move(gain1);
    EXPECT_EQ(gain2.coeffs.amplitude, 0.5);
}

// Coefficients Tests

TEST(gain_coeffs, set_amplitude)
{
    Gain<double> gain;
    gain.coeffs.set_amplitude(0.75);
    EXPECT_EQ(gain.coeffs.amplitude, 0.75);
}

TEST(gain_coeffs, set_amplitude_db_unity)
{
    Gain<double> gain;
    gain.coeffs.set_amplitude_db(0.0);  // 0 dB = unity
    EXPECT_TRUE(approx_equal(gain.coeffs.amplitude, 1.0, 1e-10));
}

TEST(gain_coeffs, set_amplitude_db_minus6)
{
    Gain<double> gain;
    gain.coeffs.set_amplitude_db(-6.0);  // -6 dB ≈ 0.5
    EXPECT_TRUE(approx_equal(gain.coeffs.amplitude, 0.501187, 1e-5));
}

TEST(gain_coeffs, set_amplitude_db_plus6)
{
    Gain<double> gain;
    gain.coeffs.set_amplitude_db(6.0);  // +6 dB ≈ 2.0
    EXPECT_TRUE(approx_equal(gain.coeffs.amplitude, 1.99526, 1e-4));
}

TEST(gain_coeffs, set_bypass)
{
    Gain<double> gain;
    gain.coeffs.set_bypass();
    EXPECT_EQ(gain.coeffs.amplitude, 1.0);
}

TEST(gain_coeffs, set_mute)
{
    Gain<double> gain;
    gain.coeffs.set_mute();
    EXPECT_EQ(gain.coeffs.amplitude, 0.0);
}

TEST(gain_coeffs, set_time_constant)
{
    Gain<double> gain;
    constexpr double sample_rate = 48000.0;
    constexpr double time_seconds = 0.050;  // 50 ms

    gain.coeffs.set_time_constant(sample_rate, time_seconds);

    // time_constant should be 1 / (time * sample_rate)
    double expected_tc = 1.0 / (time_seconds * sample_rate);
    EXPECT_TRUE(approx_equal(gain.coeffs.time_constant, expected_tc, 1e-10));
    EXPECT_TRUE(approx_equal(gain.coeffs.one_minus_time_constant, 1.0 - expected_tc, 1e-10));
}

// State Tests

TEST(gain_state, initial_zero)
{
    Gain<double> gain;
    EXPECT_EQ(gain.state.current_amplitude, 0.0);
}

TEST(gain_state, reset)
{
    Gain<double> gain;
    gain.state.current_amplitude = 0.5;
    gain.reset();
    EXPECT_EQ(gain.state.current_amplitude, 0.0);
}

TEST(gain_state, snap_to_target)
{
    Gain<double> gain;
    gain.coeffs.set_amplitude(0.75);
    gain.state.snap_to_target(gain.coeffs);
    EXPECT_EQ(gain.state.current_amplitude, 0.75);
}

// Processing Tests

TEST(gain_process, unity_gain_after_settling)
{
    Gain<double> gain;
    constexpr double sample_rate = 48000.0;
    constexpr double time_seconds = 0.001;  // 1 ms for fast settling

    gain.coeffs.set_time_constant(sample_rate, time_seconds);
    gain.coeffs.set_bypass();                // Unity gain
    gain.state.snap_to_target(gain.coeffs);  // Instant start

    // With snapped state, should pass signal through unchanged
    EXPECT_TRUE(approx_equal(gain(1.0), 1.0, 0.01));
    EXPECT_TRUE(approx_equal(gain(0.5), 0.5, 0.01));
    EXPECT_TRUE(approx_equal(gain(-0.25), -0.25, 0.01));
}

TEST(gain_process, mute_after_settling)
{
    Gain<double> gain;
    constexpr double sample_rate = 48000.0;
    constexpr double time_seconds = 0.001;  // 1 ms

    gain.coeffs.set_time_constant(sample_rate, time_seconds);
    gain.coeffs.set_mute();

    // Process many samples to let gain ramp down
    for (int i = 0; i < 1000; ++i) {
        (void)gain(1.0);
    }

    // After settling, output should be near zero
    double output = gain(1.0);
    EXPECT_TRUE(std::abs(output) < 0.01);
}

TEST(gain_process, half_gain)
{
    Gain<double> gain;
    constexpr double sample_rate = 48000.0;

    gain.coeffs.set_time_constant(sample_rate, 0.001);
    gain.coeffs.set_amplitude(0.5);
    gain.state.snap_to_target(gain.coeffs);

    EXPECT_TRUE(approx_equal(gain(1.0), 0.5, 0.01));
    EXPECT_TRUE(approx_equal(gain(2.0), 1.0, 0.01));
}

TEST(gain_process, ramping_from_zero)
{
    Gain<double> gain;
    constexpr double sample_rate = 48000.0;

    gain.coeffs.set_time_constant(sample_rate, 0.010);  // 10 ms
    gain.coeffs.set_bypass();                           // Target = 1.0
    // State starts at 0

    // First sample should be small (ramping from 0)
    double first = gain(1.0);
    EXPECT_TRUE(first < 0.1);

    // After many samples, should approach 1.0
    double output = 0.0;
    for (int i = 0; i < 10000; ++i) {
        output = gain(1.0);
    }
    EXPECT_TRUE(approx_equal(output, 1.0, 0.01));
}

TEST(gain_process, ramping_down)
{
    Gain<double> gain;
    constexpr double sample_rate = 48000.0;

    gain.coeffs.set_time_constant(sample_rate, 0.010);  // 10 ms
    gain.coeffs.set_bypass();
    gain.state.snap_to_target(gain.coeffs);  // Start at 1.0

    // Now change target to 0
    gain.coeffs.set_mute();

    // First sample should still be near 1.0
    double first = gain(1.0);
    EXPECT_TRUE(first > 0.9);

    // After many samples, should approach 0.0
    double output = 0.0;
    for (int i = 0; i < 10000; ++i) {
        output = gain(1.0);
    }
    EXPECT_TRUE(output < 0.01);
}

TEST(gain_process, time_constant_affects_speed)
{
    // Fast ramp
    Gain<double> gain_fast;
    gain_fast.coeffs.set_time_constant(48000.0, 0.001);  // 1 ms
    gain_fast.coeffs.set_bypass();

    // Slow ramp
    Gain<double> gain_slow;
    gain_slow.coeffs.set_time_constant(48000.0, 0.100);  // 100 ms
    gain_slow.coeffs.set_bypass();

    // Process same number of samples
    double fast_output = 0.0;
    double slow_output = 0.0;
    for (int i = 0; i < 100; ++i) {
        fast_output = gain_fast(1.0);
        slow_output = gain_slow(1.0);
    }

    // Fast should be closer to target than slow
    EXPECT_TRUE(fast_output > slow_output);
}

// Batch Processing Tests

TEST(gain_batch, process_in_place)
{
    Gain<double> gain;
    gain.coeffs.set_time_constant(48000.0, 0.001);
    gain.coeffs.set_amplitude(0.5);
    gain.state.snap_to_target(gain.coeffs);

    std::vector<double> samples = {1.0, 2.0, 3.0, 4.0, 5.0};
    process_in_place(gain, std::span<double>(samples));

    // All values should be approximately halved
    EXPECT_TRUE(approx_equal(samples[0], 0.5, 0.01));
    EXPECT_TRUE(approx_equal(samples[1], 1.0, 0.01));
    EXPECT_TRUE(approx_equal(samples[2], 1.5, 0.01));
    EXPECT_TRUE(approx_equal(samples[3], 2.0, 0.01));
    EXPECT_TRUE(approx_equal(samples[4], 2.5, 0.01));
}

TEST(gain_batch, process_separate_buffers)
{
    Gain<double> gain;
    gain.coeffs.set_time_constant(48000.0, 0.001);
    gain.coeffs.set_amplitude(2.0);
    gain.state.snap_to_target(gain.coeffs);

    std::vector<double> const input = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> output(5);

    process(gain, std::span<double const>(input), std::span<double>(output));

    // All values should be approximately doubled
    EXPECT_TRUE(approx_equal(output[0], 2.0, 0.02));
    EXPECT_TRUE(approx_equal(output[1], 4.0, 0.04));
    EXPECT_TRUE(approx_equal(output[2], 6.0, 0.06));
    EXPECT_TRUE(approx_equal(output[3], 8.0, 0.08));
    EXPECT_TRUE(approx_equal(output[4], 10.0, 0.1));
}

TEST(gain_batch, process_accumulate)
{
    Gain<double> gain;
    gain.coeffs.set_time_constant(48000.0, 0.001);
    gain.coeffs.set_bypass();
    gain.state.snap_to_target(gain.coeffs);

    std::vector<double> const input = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> output = {10.0, 20.0, 30.0, 40.0, 50.0};

    process_accumulate(gain, std::span<double const>(input), std::span<double>(output));

    // output += input * gain
    EXPECT_TRUE(approx_equal(output[0], 11.0, 0.1));
    EXPECT_TRUE(approx_equal(output[1], 22.0, 0.2));
    EXPECT_TRUE(approx_equal(output[2], 33.0, 0.3));
    EXPECT_TRUE(approx_equal(output[3], 44.0, 0.4));
    EXPECT_TRUE(approx_equal(output[4], 55.0, 0.5));
}

// Z-Domain Tests

TEST(gain_z_domain, unity_gain)
{
    Gain<double> gain;
    gain.coeffs.set_bypass();

    constexpr double sample_rate = 48000.0;
    double const omega = 2.0 * std::numbers::pi * 1000.0 / sample_rate;
    std::complex<double> const z1(std::cos(-omega), std::sin(-omega));

    auto response = gain.coeffs.process_z_domain(z1);
    double const magnitude = std::abs(response);

    EXPECT_TRUE(approx_equal(magnitude, 1.0, 1e-10));
}

TEST(gain_z_domain, half_gain)
{
    Gain<double> gain;
    gain.coeffs.set_amplitude(0.5);

    constexpr double sample_rate = 48000.0;
    double const omega = 2.0 * std::numbers::pi * 1000.0 / sample_rate;
    std::complex<double> const z1(std::cos(-omega), std::sin(-omega));

    auto response = gain.coeffs.process_z_domain(z1);
    double const magnitude = std::abs(response);

    EXPECT_TRUE(approx_equal(magnitude, 0.5, 1e-10));
}

TEST(gain_z_domain, frequency_independent)
{
    Gain<double> gain;
    gain.coeffs.set_amplitude(0.707);

    constexpr double sample_rate = 48000.0;

    // Check at multiple frequencies - gain should be constant
    for (double freq : {100.0, 1000.0, 5000.0, 10000.0, 20000.0}) {
        double const omega = 2.0 * std::numbers::pi * freq / sample_rate;
        std::complex<double> const z1(std::cos(-omega), std::sin(-omega));

        auto response = gain.coeffs.process_z_domain(z1);
        double const magnitude = std::abs(response);

        EXPECT_TRUE(approx_equal(magnitude, 0.707, 1e-10));
    }
}

TEST(gain_z_domain, zero_phase)
{
    Gain<double> gain;
    gain.coeffs.set_amplitude(0.5);

    constexpr double sample_rate = 48000.0;
    double const omega = 2.0 * std::numbers::pi * 1000.0 / sample_rate;
    std::complex<double> const z1(std::cos(-omega), std::sin(-omega));

    auto response = gain.coeffs.process_z_domain(z1);
    double const phase = std::arg(response);

    // Gain should have zero phase shift
    EXPECT_TRUE(approx_equal(phase, 0.0, 1e-10));
}

// SIMD Tests

TEST(gain_simd, construction)
{
    Gain<simd_float32x4> gain;
    (void)gain;  // Should compile and not crash
}

TEST(gain_simd, set_amplitude_per_channel)
{
    Gain<simd_float32x4> gain;

    gain.coeffs.set_amplitude(0.25f, 0);
    gain.coeffs.set_amplitude(0.50f, 1);
    gain.coeffs.set_amplitude(0.75f, 2);
    gain.coeffs.set_amplitude(1.00f, 3);

    EXPECT_EQ(gain.coeffs.amplitude[0], 0.25f);
    EXPECT_EQ(gain.coeffs.amplitude[1], 0.50f);
    EXPECT_EQ(gain.coeffs.amplitude[2], 0.75f);
    EXPECT_EQ(gain.coeffs.amplitude[3], 1.00f);
}

TEST(gain_simd, set_bypass_per_channel)
{
    Gain<simd_float32x4> gain;

    for (size_t ch = 0; ch < 4; ++ch) {
        gain.coeffs.set_bypass(ch);
    }

    EXPECT_EQ(gain.coeffs.amplitude[0], 1.0f);
    EXPECT_EQ(gain.coeffs.amplitude[1], 1.0f);
    EXPECT_EQ(gain.coeffs.amplitude[2], 1.0f);
    EXPECT_EQ(gain.coeffs.amplitude[3], 1.0f);
}

TEST(gain_simd, process_sample)
{
    Gain<simd_float32x4> gain;

    // Set different gains per channel
    gain.coeffs.set_amplitude(0.5f, 0);
    gain.coeffs.set_amplitude(1.0f, 1);
    gain.coeffs.set_amplitude(2.0f, 2);
    gain.coeffs.set_amplitude(0.0f, 3);

    // Set fast time constant and snap to target
    for (size_t ch = 0; ch < 4; ++ch) {
        gain.coeffs.set_time_constant(48000.0, 0.001, ch);
    }
    gain.state.snap_to_target(gain.coeffs);

    simd_float32x4 input{1.0f, 1.0f, 1.0f, 1.0f};
    simd_float32x4 output = gain(input);

    EXPECT_TRUE(approx_equal(output[0], 0.5f, 0.01f));
    EXPECT_TRUE(approx_equal(output[1], 1.0f, 0.01f));
    EXPECT_TRUE(approx_equal(output[2], 2.0f, 0.02f));
    EXPECT_TRUE(approx_equal(output[3], 0.0f, 0.01f));
}

TEST(gain_simd, different_time_constants)
{
    Gain<simd_float32x4> gain;

    // All channels target 1.0
    for (size_t ch = 0; ch < 4; ++ch) {
        gain.coeffs.set_bypass(ch);
    }

    // Different smoothing times
    gain.coeffs.set_time_constant(48000.0, 0.001, 0);  // 1 ms (fast)
    gain.coeffs.set_time_constant(48000.0, 0.010, 1);  // 10 ms
    gain.coeffs.set_time_constant(48000.0, 0.050, 2);  // 50 ms
    gain.coeffs.set_time_constant(48000.0, 0.100, 3);  // 100 ms (slow)

    // Process some samples
    simd_float32x4 input{1.0f, 1.0f, 1.0f, 1.0f};
    simd_float32x4 output;
    for (int i = 0; i < 100; ++i) {
        output = gain(input);
    }

    // Faster channels should be closer to target
    EXPECT_TRUE(output[0] > output[1]);
    EXPECT_TRUE(output[1] > output[2]);
    EXPECT_TRUE(output[2] > output[3]);
}

// Edge Case Tests

TEST(gain_edge, zero_gain)
{
    Gain<double> gain;
    gain.coeffs.set_amplitude(0.0);
    gain.coeffs.set_time_constant(48000.0, 0.001);
    gain.state.snap_to_target(gain.coeffs);

    double output = gain(1.0);
    EXPECT_EQ(output, 0.0);
}

TEST(gain_edge, unity_gain)
{
    Gain<double> gain;
    gain.coeffs.set_bypass();
    gain.coeffs.set_time_constant(48000.0, 0.001);
    gain.state.snap_to_target(gain.coeffs);

    double output = gain(0.5);
    EXPECT_EQ(output, 0.5);
}

TEST(gain_edge, negative_input)
{
    Gain<double> gain;
    gain.coeffs.set_amplitude(2.0);
    gain.coeffs.set_time_constant(48000.0, 0.001);
    gain.state.snap_to_target(gain.coeffs);

    double output = gain(-0.5);
    EXPECT_EQ(output, -1.0);
}

TEST(gain_edge, very_small_time_constant)
{
    Gain<double> gain;
    gain.coeffs.set_amplitude(1.0);
    gain.coeffs.set_time_constant(48000.0, 0.0001);  // 0.1 ms
    gain.state.current_amplitude = 0.0;

    // Should converge very quickly
    double output = 0.0;
    for (int i = 0; i < 10; ++i) {
        output = gain(1.0);
    }
    EXPECT_TRUE(output > 0.9);
}

TEST(gain_edge, very_large_time_constant)
{
    Gain<double> gain;
    gain.coeffs.set_amplitude(1.0);
    gain.coeffs.set_time_constant(48000.0, 1.0);  // 1 second
    gain.state.current_amplitude = 0.0;

    // Should converge very slowly
    double output = 0.0;
    for (int i = 0; i < 10; ++i) {
        output = gain(1.0);
    }
    EXPECT_TRUE(output < 0.01);  // Still very small after 10 samples
}

TEST(gain_edge, state_reset)
{
    Gain<double> gain;
    gain.coeffs.set_amplitude(1.0);
    gain.coeffs.set_time_constant(48000.0, 0.001);
    gain.state.current_amplitude = 0.5;

    gain.state.reset();
    EXPECT_EQ(gain.state.current_amplitude, 0.0);
}

TEST(gain_edge, snap_from_different_value)
{
    Gain<double> gain;
    gain.coeffs.set_amplitude(0.8);
    gain.coeffs.set_time_constant(48000.0, 0.001);
    gain.state.current_amplitude = 0.2;

    gain.state.snap_to_target(gain.coeffs);
    EXPECT_EQ(gain.state.current_amplitude, 0.8);
}

TEST(gain_edge, zero_input)
{
    Gain<double> gain;
    gain.coeffs.set_amplitude(2.0);
    gain.coeffs.set_time_constant(48000.0, 0.001);
    gain.state.snap_to_target(gain.coeffs);

    double output = gain(0.0);
    EXPECT_EQ(output, 0.0);
}

// Main test runner function required bycreate_test_sourcelist =====
TEST_MAIN(statusbar_dsp, dsp_gain_test)