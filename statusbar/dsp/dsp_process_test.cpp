// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Unit tests for statusbar.dsp:process module

#include "statusbar/dsp/dsp.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <cmath>
#include <span>
#include <vector>

using namespace statusbar::dsp;

// Simple test plugin that doubles its input
struct DoublePlugin
{
    double operator()(double input) noexcept { return input * 2.0; }
};

// Simple test plugin that adds a constant
struct AddPlugin
{
    double offset;
    double operator()(double input) noexcept { return input + offset; }
};

// Stateful plugin that accumulates
struct AccumulatorPlugin
{
    double total = 0.0;
    double operator()(double input) noexcept
    {
        total += input;
        return total;
    }
};

// ===== process_in_place Tests =====

TEST(in_place, basic)
{
    DoublePlugin plugin;
    std::vector<double> samples = {1.0, 2.0, 3.0, 4.0, 5.0};
    process_in_place(plugin, std::span<double>(samples));

    EXPECT_EQ(samples[0], 2.0);
    EXPECT_EQ(samples[1], 4.0);
    EXPECT_EQ(samples[2], 6.0);
    EXPECT_EQ(samples[3], 8.0);
    EXPECT_EQ(samples[4], 10.0);
}

TEST(in_place, empty_span)
{
    DoublePlugin plugin;
    std::span<double> empty;
    process_in_place(plugin, empty);  // Should not crash
}

TEST(in_place, single_element)
{
    DoublePlugin plugin;
    std::vector<double> samples = {7.0};
    process_in_place(plugin, std::span<double>(samples));
    EXPECT_EQ(samples[0], 14.0);
}

TEST(in_place, fixed_size_span)
{
    DoublePlugin plugin;
    std::array<double, 4> samples = {1.0, 2.0, 3.0, 4.0};
    process_in_place(plugin, std::span<double, 4>(samples));

    EXPECT_EQ(samples[0], 2.0);
    EXPECT_EQ(samples[1], 4.0);
    EXPECT_EQ(samples[2], 6.0);
    EXPECT_EQ(samples[3], 8.0);
}

TEST(in_place, stateful_plugin)
{
    AccumulatorPlugin plugin;
    std::vector<double> samples = {1.0, 2.0, 3.0};
    process_in_place(plugin, std::span<double>(samples));

    // Accumulator: 1, 1+2=3, 3+3=6
    EXPECT_EQ(samples[0], 1.0);
    EXPECT_EQ(samples[1], 3.0);
    EXPECT_EQ(samples[2], 6.0);
    EXPECT_EQ(plugin.total, 6.0);
}

// ===== process_mix_in_place Tests =====

TEST(mix_in_place, basic)
{
    DoublePlugin plugin;
    std::vector<double> samples = {1.0, 2.0, 3.0, 4.0, 5.0};
    process_mix_in_place(plugin, std::span<double>(samples));

    // sample += plugin(sample) = sample + 2*sample = 3*sample
    EXPECT_EQ(samples[0], 3.0);
    EXPECT_EQ(samples[1], 6.0);
    EXPECT_EQ(samples[2], 9.0);
    EXPECT_EQ(samples[3], 12.0);
    EXPECT_EQ(samples[4], 15.0);
}

TEST(mix_in_place, empty_span)
{
    DoublePlugin plugin;
    std::span<double> empty;
    process_mix_in_place(plugin, empty);  // Should not crash
}

TEST(mix_in_place, fixed_size_span)
{
    DoublePlugin plugin;
    std::array<double, 4> samples = {1.0, 2.0, 3.0, 4.0};
    process_mix_in_place(plugin, std::span<double, 4>(samples));

    EXPECT_EQ(samples[0], 3.0);
    EXPECT_EQ(samples[1], 6.0);
    EXPECT_EQ(samples[2], 9.0);
    EXPECT_EQ(samples[3], 12.0);
}

TEST(mix_in_place, with_offset_plugin)
{
    AddPlugin plugin{10.0};
    std::vector<double> samples = {1.0, 2.0, 3.0};
    process_mix_in_place(plugin, std::span<double>(samples));

    // sample += plugin(sample) = sample + (sample + 10) = 2*sample + 10
    EXPECT_EQ(samples[0], 12.0);  // 1 + (1+10) = 12
    EXPECT_EQ(samples[1], 14.0);  // 2 + (2+10) = 14
    EXPECT_EQ(samples[2], 16.0);  // 3 + (3+10) = 16
}

// process(separate buffers) Tests =====

TEST(separate, basic)
{
    DoublePlugin plugin;
    std::vector<double> const input = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> output(5);
    process(plugin, std::span<double const>(input), std::span<double>(output));

    EXPECT_EQ(output[0], 2.0);
    EXPECT_EQ(output[1], 4.0);
    EXPECT_EQ(output[2], 6.0);
    EXPECT_EQ(output[3], 8.0);
    EXPECT_EQ(output[4], 10.0);
}

TEST(separate, output_smaller)
{
    DoublePlugin plugin;
    std::vector<double> const input = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> output(3);  // Smaller than input
    process(plugin, std::span<double const>(input), std::span<double>(output));

    // Should only process min(input.size(), output.size()) = 3 samples
    EXPECT_EQ(output[0], 2.0);
    EXPECT_EQ(output[1], 4.0);
    EXPECT_EQ(output[2], 6.0);
}

TEST(separate, input_smaller)
{
    DoublePlugin plugin;
    std::vector<double> const input = {1.0, 2.0, 3.0};
    std::vector<double> output(5, 99.0);  // Pre-filled with 99
    process(plugin, std::span<double const>(input), std::span<double>(output));

    // Should only process 3 samples
    EXPECT_EQ(output[0], 2.0);
    EXPECT_EQ(output[1], 4.0);
    EXPECT_EQ(output[2], 6.0);
    EXPECT_EQ(output[3], 99.0);  // Unchanged
    EXPECT_EQ(output[4], 99.0);  // Unchanged
}

TEST(separate, empty_input)
{
    DoublePlugin plugin;
    std::span<double const> empty_input;
    std::vector<double> output(5, 99.0);
    process(plugin, empty_input, std::span<double>(output));

    // Nothing should be processed
    for (auto v : output) {
        EXPECT_EQ(v, 99.0);
    }
}

TEST(separate, empty_output)
{
    DoublePlugin plugin;
    std::vector<double> const input = {1.0, 2.0, 3.0};
    std::span<double> empty_output;
    process(plugin, std::span<double const>(input), empty_output);
    // Should not crash
}

TEST(separate, fixed_size_span)
{
    DoublePlugin plugin;
    std::array<double, 4> const input = {1.0, 2.0, 3.0, 4.0};
    std::array<double, 4> output{};
    process(plugin, std::span<double const, 4>(input), std::span<double, 4>(output));

    EXPECT_EQ(output[0], 2.0);
    EXPECT_EQ(output[1], 4.0);
    EXPECT_EQ(output[2], 6.0);
    EXPECT_EQ(output[3], 8.0);
}

// ===== process_accumulate Tests =====

TEST(accumulate, basic)
{
    DoublePlugin plugin;
    std::vector<double> const input = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> output = {10.0, 20.0, 30.0, 40.0, 50.0};
    process_accumulate(plugin, std::span<double const>(input), std::span<double>(output));

    // output[i] += plugin(input[i]) = output[i] + 2*input[i]
    EXPECT_EQ(output[0], 12.0);  // 10 + 2*1
    EXPECT_EQ(output[1], 24.0);  // 20 + 2*2
    EXPECT_EQ(output[2], 36.0);  // 30 + 2*3
    EXPECT_EQ(output[3], 48.0);  // 40 + 2*4
    EXPECT_EQ(output[4], 60.0);  // 50 + 2*5
}

TEST(accumulate, output_smaller)
{
    DoublePlugin plugin;
    std::vector<double> const input = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> output = {10.0, 20.0, 30.0};  // Smaller
    process_accumulate(plugin, std::span<double const>(input), std::span<double>(output));

    EXPECT_EQ(output[0], 12.0);
    EXPECT_EQ(output[1], 24.0);
    EXPECT_EQ(output[2], 36.0);
}

TEST(accumulate, input_smaller)
{
    DoublePlugin plugin;
    std::vector<double> const input = {1.0, 2.0, 3.0};
    std::vector<double> output = {10.0, 20.0, 30.0, 40.0, 50.0};
    process_accumulate(plugin, std::span<double const>(input), std::span<double>(output));

    EXPECT_EQ(output[0], 12.0);
    EXPECT_EQ(output[1], 24.0);
    EXPECT_EQ(output[2], 36.0);
    EXPECT_EQ(output[3], 40.0);  // Unchanged
    EXPECT_EQ(output[4], 50.0);  // Unchanged
}

TEST(accumulate, empty_spans)
{
    DoublePlugin plugin;
    std::span<double const> empty_input;
    std::span<double> empty_output;
    process_accumulate(plugin, empty_input, empty_output);  // Should not crash
}

TEST(accumulate, fixed_size_span)
{
    DoublePlugin plugin;
    std::array<double, 4> const input = {1.0, 2.0, 3.0, 4.0};
    std::array<double, 4> output = {10.0, 20.0, 30.0, 40.0};
    process_accumulate(plugin, std::span<double const, 4>(input), std::span<double, 4>(output));

    EXPECT_EQ(output[0], 12.0);
    EXPECT_EQ(output[1], 24.0);
    EXPECT_EQ(output[2], 36.0);
    EXPECT_EQ(output[3], 48.0);
}

// Integration Tests with Real DSP Plugins

TEST(integration, biquad_bypass)
{
    BiQuad<double> filter;
    filter.coeffs.set_bypass();

    std::vector<double> samples = {1.0, 2.0, 3.0, 4.0, 5.0};
    process_in_place(filter, std::span<double>(samples));

    // Bypass filter should not change values
    EXPECT_EQ(samples[0], 1.0);
    EXPECT_EQ(samples[1], 2.0);
    EXPECT_EQ(samples[2], 3.0);
    EXPECT_EQ(samples[3], 4.0);
    EXPECT_EQ(samples[4], 5.0);
}

TEST(integration, oscillator_generate)
{
    Oscillator<double> osc;
    double const sample_rate = 48000.0;
    double const freq = 440.0;

    osc.coeffs_.set_amplitude(1.0, 0);
    osc.state_.set_frequency(
        FrequencyParameters<double>{.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .phase_in_radians = 0.0});

    // Generate samples by processing zero-filled buffer
    std::vector<double> output(1000, 0.0);
    process_in_place(osc, std::span<double>(output));

    // Output should have non-zero RMS
    double rms = calculate_rms(std::span<double const>(output));
    EXPECT_TRUE(rms > 0.1);
}

TEST(integration, chain_biquad_oscillator)
{
    // Generate oscillator output, then filter it
    Oscillator<double> osc;
    BiQuad<double> filter;
    double const sample_rate = 48000.0;

    osc.coeffs_.set_amplitude(1.0, 0);
    osc.state_.set_frequency(
        FrequencyParameters<double>{.sample_rate_recip = 1.0 / sample_rate, .frequency = 1000.0, .phase_in_radians = 0.0});
    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = 500.0, .q = 0.707});

    // Generate oscillator output
    std::vector<double> buffer(1000, 0.0);
    process_in_place(osc, std::span<double>(buffer));

    double rms_before = calculate_rms(std::span<double const>(buffer));

    // Filter the output (1 kHz signal through 500 Hz lowpass)
    process_in_place(filter, std::span<double>(buffer));

    double rms_after = calculate_rms(std::span<double const>(buffer));

    // Signal should be attenuated (1 kHz is above 500 Hz cutoff)
    EXPECT_TRUE(rms_after < rms_before);
}

// Float variant math function tests

TEST(dsp_math_float, generate_sine_float)
{
    std::array<float, 480> buffer{};
    float const sample_rate = 48000.0f;
    float const frequency = 1000.0f;

    generate_sine(std::span<float>(buffer), frequency, sample_rate);

    // Check that the buffer contains a sine wave (not all zeros)
    float min_val = 0.0f;
    float max_val = 0.0f;
    for (float sample : buffer) {
        if (sample < min_val) {
            min_val = sample;
        }
        if (sample > max_val) {
            max_val = sample;
        }
    }

    // A sine wave should have values from -1 to 1
    EXPECT_TRUE(max_val > 0.9f);
    EXPECT_TRUE(min_val < -0.9f);
}

TEST(dsp_math_float, calculate_rms_float)
{
    std::array<float, 100> buffer{};

    // Fill with constant value of 0.5
    for (auto& sample : buffer) {
        sample = 0.5f;
    }

    float rms = calculate_rms(std::span<float const>(buffer));

    // RMS of constant 0.5 should be 0.5
    EXPECT_TRUE(std::fabs(rms - 0.5f) < 0.001f);
}

TEST(dsp_math_float, calculate_rms_float_sine)
{
    std::array<float, 4800> buffer{};
    float const sample_rate = 48000.0f;

    // Generate a 100Hz sine wave (48 complete cycles at 48kHz)
    generate_sine(std::span<float>(buffer), 100.0f, sample_rate);

    float rms = calculate_rms(std::span<float const>(buffer));

    // RMS of sine wave with amplitude 1 should be ~0.707 (1/sqrt(2))
    EXPECT_TRUE(std::fabs(rms - 0.707f) < 0.01f);
}

TEST(dsp_math_float, count_zero_crossings_float)
{
    std::array<float, 4800> buffer{};
    float const sample_rate = 48000.0f;

    // Generate a 100Hz sine wave
    generate_sine(std::span<float>(buffer), 100.0f, sample_rate);

    size_t crossings = count_zero_crossings(std::span<float const>(buffer));

    // 100Hz sine wave in 0.1 seconds (4800 samples at 48kHz) should have
    // approximately 10 full cycles = 20 zero crossings
    EXPECT_TRUE(crossings >= 18 && crossings <= 22);
}

// Main test runner function required bycreate_test_sourcelist =====
TEST_MAIN(statusbar_dsp, dsp_process_test)