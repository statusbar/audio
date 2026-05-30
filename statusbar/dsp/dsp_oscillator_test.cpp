// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Unit tests for statusbar.dsp:oscillator module

#include "statusbar/dsp/dsp.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <vector>

using namespace statusbar::dsp;

// Lookup Table Tests

TEST(osc_tables, octave_multipliers)
{
    EXPECT_TRUE(approx_equal(oscillator_octave_multiplier_table[0], 0.125));  // 1/8
    EXPECT_TRUE(approx_equal(oscillator_octave_multiplier_table[1], 0.25));   // 1/4
    EXPECT_TRUE(approx_equal(oscillator_octave_multiplier_table[2], 0.5));    // 1/2
    EXPECT_TRUE(approx_equal(oscillator_octave_multiplier_table[3], 1.0));    // 1
    EXPECT_TRUE(approx_equal(oscillator_octave_multiplier_table[4], 2.0));    // 2
    EXPECT_TRUE(approx_equal(oscillator_octave_multiplier_table[5], 4.0));    // 4
    EXPECT_TRUE(approx_equal(oscillator_octave_multiplier_table[6], 8.0));    // 8
}

TEST(osc_tables, octave_multipliers_float)
{
    EXPECT_TRUE(approx_equal(oscillator_octave_multiplier_table_f[0], 0.125f));
    EXPECT_TRUE(approx_equal(oscillator_octave_multiplier_table_f[3], 1.0f));
    EXPECT_TRUE(approx_equal(oscillator_octave_multiplier_table_f[6], 8.0f));
}

TEST(osc_tables, note_frequencies_a440)
{
    // A4 = 440 Hz
    EXPECT_TRUE(approx_equal(oscillator_note_frequencies_a440[0], 440.0, 0.01));
    // Check that frequencies increase chromatically
    for (int i = 1; i < 12; ++i) {
        EXPECT_TRUE(oscillator_note_frequencies_a440[i] > oscillator_note_frequencies_a440[i - 1]);
    }
}

TEST(osc_tables, note_frequencies_float)
{
    EXPECT_TRUE(approx_equal(oscillator_note_frequencies_a440_f[0], 440.0f, 0.01f));
}

// Type Traits Tests

TEST(osc_traits, value_type_double)
{
    using OscDouble = Oscillator<double>;
    static_assert(std::is_same_v<OscDouble::value_type, double>);
    static_assert(std::is_same_v<OscDouble::item_type, double>);
}

TEST(osc_traits, osc_vector_size_scalar)
{
    using OscDouble = Oscillator<double>;
    EXPECT_EQ(OscDouble::vector_size, 1);
    EXPECT_EQ(OscDouble::flattened_size, 1);
}

TEST(osc_traits, osc_vector_size_simd)
{
    using OscSimd = Oscillator<simd_float32x4>;
    EXPECT_EQ(OscSimd::vector_size, 4);
    EXPECT_EQ(OscSimd::flattened_size, 4);
}

// Construction Tests

TEST(osc_construction, default_ctor)
{
    Oscillator<double> osc;
    // Should compile and not crash
    (void)osc;
}

TEST(osc_construction, osc_copy_ctor)
{
    Oscillator<double> osc1;
    osc1.coeffs_.set_amplitude(0.5, 0);
    osc1.state_.set_frequency(
        FrequencyParameters<double>{.sample_rate_recip = 1.0 / 48000.0, .frequency = 440.0, .phase_in_radians = 0.0});

    Oscillator<double> osc2 = osc1;
    EXPECT_EQ(osc2.coeffs_.amplitude_, 0.5);
}

// Coefficients Tests

TEST(osc_coeffs, set_amplitude)
{
    Oscillator<double> osc;
    osc.coeffs_.set_amplitude(0.75, 0);
    EXPECT_EQ(osc.coeffs_.amplitude_, 0.75);
}

TEST(osc_coeffs, set_amplitude_simd)
{
    Oscillator<simd_float32x4> osc;
    osc.coeffs_.set_amplitude(0.5f, 0);
    osc.coeffs_.set_amplitude(0.6f, 1);
    osc.coeffs_.set_amplitude(0.7f, 2);
    osc.coeffs_.set_amplitude(0.8f, 3);

    EXPECT_EQ(osc.coeffs_.amplitude_[0], 0.5f);
    EXPECT_EQ(osc.coeffs_.amplitude_[1], 0.6f);
    EXPECT_EQ(osc.coeffs_.amplitude_[2], 0.7f);
    EXPECT_EQ(osc.coeffs_.amplitude_[3], 0.8f);
}

// State Tests

TEST(osc_state, osc_initial_zero)
{
    Oscillator<double> osc;
    EXPECT_EQ(osc.state_.z1_, 0.0);
    EXPECT_EQ(osc.state_.z2_, 0.0);
}

TEST(osc_state, set_frequency_double)
{
    Oscillator<double> osc;
    double const sample_rate = 48000.0;
    double const freq = 440.0;

    osc.state_.set_frequency(
        FrequencyParameters<double>{.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .phase_in_radians = 0.0});

    // After setting frequency, a_ should be 2*cos(w) where w = 2*pi*f/fs
    double w = 2.0 * std::numbers::pi * freq / sample_rate;
    double expected_a = 2.0 * std::cos(w);
    EXPECT_TRUE(approx_equal(osc.state_.a_, expected_a, 1e-10));
}

TEST(osc_state, set_frequency_float)
{
    Oscillator<float> osc;
    float const sample_rate = 48000.0f;
    float const freq = 440.0f;

    osc.state_.set_frequency(
        FrequencyParameters<float>{.sample_rate_recip = 1.0f / sample_rate, .frequency = freq, .phase_in_radians = 0.0f});

    float w = 2.0f * std::numbers::pi_v<float> * freq / sample_rate;
    float expected_a = 2.0f * std::cos(w);
    EXPECT_TRUE(approx_equal(osc.state_.a_, expected_a, 1e-5f));
}

TEST(osc_state, set_frequency_note)
{
    Oscillator<double> osc;
    double const sample_rate = 48000.0;

    // Set to A4 (octave 3 in the table = 1x multiplier, note 0 = A)
    osc.state_.set_frequency_note(NoteParameters<double>{.sample_rate_recip = 1.0 / sample_rate, .octave = 3, .note = 0});

    // Should be equivalent to setting 440 Hz directly
    Oscillator<double> osc_direct;
    osc_direct.state_.set_frequency(
        FrequencyParameters<double>{.sample_rate_recip = 1.0 / sample_rate, .frequency = 440.0, .phase_in_radians = 0.0});

    EXPECT_TRUE(approx_equal(osc.state_.a_, osc_direct.state_.a_, 1e-10));
}

TEST(osc_state, set_frequency_note_octave)
{
    Oscillator<double> osc_low;
    Oscillator<double> osc_high;
    double const sample_rate = 48000.0;

    // A3 (octave 2 = 1/2 multiplier)
    osc_low.state_.set_frequency_note(NoteParameters<double>{.sample_rate_recip = 1.0 / sample_rate, .octave = 2, .note = 0});
    // A5 (octave 4 = 2x multiplier)
    osc_high.state_.set_frequency_note(NoteParameters<double>{.sample_rate_recip = 1.0 / sample_rate, .octave = 4, .note = 0});

    // a_ values should be different (different frequencies)
    EXPECT_TRUE(osc_low.state_.a_ != osc_high.state_.a_);
}

// Processing Tests

TEST(osc_process, generates_output)
{
    Oscillator<double> osc;
    double const sample_rate = 48000.0;
    double const freq = 440.0;

    osc.coeffs_.set_amplitude(1.0, 0);
    osc.state_.set_frequency(
        FrequencyParameters<double>{.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .phase_in_radians = 0.0});

    // Generate some samples
    std::vector<double> output(1000);
    for (size_t i = 0; i < output.size(); ++i) {
        output[i] = osc(0.0);
    }

    // Output should have non-zero RMS
    double rms = calculate_rms(std::span<double const>(output));
    EXPECT_TRUE(rms > 0.1);
}

TEST(osc_process, frequency_accuracy)
{
    Oscillator<double> osc;
    double const sample_rate = 48000.0;
    double const freq = 1000.0;  // 1 kHz for easy measurement

    osc.coeffs_.set_amplitude(1.0, 0);
    osc.state_.set_frequency(
        FrequencyParameters<double>{.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .phase_in_radians = 0.0});

    // Generate 1 second of samples
    size_t const num_samples = static_cast<size_t>(sample_rate);
    std::vector<double> output(num_samples);
    for (size_t i = 0; i < num_samples; ++i) {
        output[i] = osc(0.0);
    }

    // Count zero crossings (2 per cycle)
    size_t crossings = count_zero_crossings(std::span<double const>(output));
    double measured_freq = crossings / 2.0;  // 1 second of samples

    // Should be close to 1000 Hz (within 1%)
    EXPECT_TRUE(std::abs(measured_freq - freq) < freq * 0.01);
}

TEST(osc_process, amplitude_scaling)
{
    Oscillator<double> osc1;
    Oscillator<double> osc2;
    double const sample_rate = 48000.0;
    double const freq = 440.0;

    osc1.coeffs_.set_amplitude(0.5, 0);
    osc1.state_.set_frequency(
        FrequencyParameters<double>{.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .phase_in_radians = 0.0});

    osc2.coeffs_.set_amplitude(1.0, 0);
    osc2.state_.set_frequency(
        FrequencyParameters<double>{.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .phase_in_radians = 0.0});

    // Generate samples
    std::vector<double> output1(1000), output2(1000);
    for (size_t i = 0; i < 1000; ++i) {
        output1[i] = osc1(0.0);
        output2[i] = osc2(0.0);
    }

    double rms1 = calculate_rms(std::span<double const>(output1));
    double rms2 = calculate_rms(std::span<double const>(output2));

    // RMS should scale with amplitude (approximately 2:1 ratio)
    double ratio = rms2 / rms1;
    EXPECT_TRUE(ratio > 1.8 && ratio < 2.2);
}

TEST(osc_process, adds_to_input)
{
    Oscillator<double> osc;
    double const sample_rate = 48000.0;
    double const freq = 440.0;

    osc.coeffs_.set_amplitude(1.0, 0);
    osc.state_.set_frequency(
        FrequencyParameters<double>{.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .phase_in_radians = 0.0});

    // Generate with non-zero input
    double input_dc = 0.5;
    std::vector<double> output(1000);
    double sum = 0.0;
    for (size_t i = 0; i < output.size(); ++i) {
        output[i] = osc(input_dc);
        sum += output[i];
    }

    // Average should be close to input DC (oscillator averages to ~0)
    double avg = sum / output.size();
    EXPECT_TRUE(std::abs(avg - input_dc) < 0.1);
}

TEST(osc_process, phase_offset)
{
    Oscillator<double> osc_zero;
    Oscillator<double> osc_quarter;
    double const sample_rate = 48000.0;
    double const freq = 440.0;

    osc_zero.coeffs_.set_amplitude(1.0, 0);
    osc_zero.state_.set_frequency(
        FrequencyParameters<double>{.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .phase_in_radians = 0.0});

    osc_quarter.coeffs_.set_amplitude(1.0, 0);
    osc_quarter.state_.set_frequency(
        FrequencyParameters<double>{
            .sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .phase_in_radians = std::numbers::pi / 2.0});

    // First sample should be different due to phase offset
    double sample_zero = osc_zero(0.0);
    double sample_quarter = osc_quarter(0.0);

    EXPECT_TRUE(std::abs(sample_zero - sample_quarter) > 0.1);
}

TEST(osc_process, span_process_in_place)
{
    Oscillator<double> osc;
    double const sample_rate = 48000.0;
    double const freq = 1000.0;

    osc.coeffs_.set_amplitude(1.0, 0);
    osc.state_.set_frequency(
        FrequencyParameters<double>{.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .phase_in_radians = 0.0});

    // Generate samples using process_in_place() with zero input
    std::vector<double> output(1000, 0.0);
    process_in_place(osc, std::span<double>(output));

    // Output should have non-zero RMS
    double rms = calculate_rms(std::span<double const>(output));
    EXPECT_TRUE(rms > 0.1);

    // Verify frequency by counting zero crossings
    size_t crossings = count_zero_crossings(std::span<double const>(output));
    double measured_freq = crossings * sample_rate / (2.0 * output.size());
    EXPECT_TRUE(std::abs(measured_freq - freq) < freq * 0.05);
}

TEST(osc_process, span_process_mix_in_place)
{
    Oscillator<double> osc;
    double const sample_rate = 48000.0;
    double const freq = 440.0;

    osc.coeffs_.set_amplitude(1.0, 0);
    osc.state_.set_frequency(
        FrequencyParameters<double>{.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .phase_in_radians = 0.0});

    // Fill buffer with DC offset
    double const dc_offset = 0.5;
    std::vector<double> output(1000, dc_offset);

    // Mix oscillator output with existing content using process_mix_in_place
    // Note: process_mix_in_place does sample += plugin(sample)
    // For oscillator, plugin(dc_offset) = oscillator_output + dc_offset
    // So result = dc_offset + (oscillator_output + dc_offset) = 2*dc_offset + oscillator_output
    process_mix_in_place(osc, std::span<double>(output));

    // Average should be close to 2*DC offset (osc averages to ~0, plus original dc_offset gets doubled)
    double sum = 0.0;
    for (auto s : output) {
        sum += s;
    }
    double avg = sum / output.size();
    EXPECT_TRUE(std::abs(avg - 2.0 * dc_offset) < 0.15);

    // RMS should be higher than DC alone due to added oscillation
    double rms = calculate_rms(std::span<double const>(output));
    EXPECT_TRUE(rms > dc_offset);
}

TEST(osc_process, span_process_matches_operator)
{
    // Two oscillators with same settings
    Oscillator<double> osc1;
    Oscillator<double> osc2;
    double const sample_rate = 48000.0;
    double const freq = 440.0;

    osc1.coeffs_.set_amplitude(1.0, 0);
    osc1.state_.set_frequency(
        FrequencyParameters<double>{.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .phase_in_radians = 0.0});

    osc2.coeffs_.set_amplitude(1.0, 0);
    osc2.state_.set_frequency(
        FrequencyParameters<double>{.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .phase_in_radians = 0.0});

    // Generate using operator()
    std::vector<double> output1(100);
    for (size_t i = 0; i < output1.size(); ++i) {
        output1[i] = osc1(0.0);
    }

    // Generate using process_in_place() with zero-initialized buffer
    std::vector<double> output2(100, 0.0);
    process_in_place(osc2, std::span<double>(output2));

    // Results should be identical
    for (size_t i = 0; i < output1.size(); ++i) {
        EXPECT_EQ(output1[i], output2[i]);
    }
}

TEST(osc_process, fixed_span_process_in_place)
{
    Oscillator<double> osc1;
    Oscillator<double> osc2;
    double const sample_rate = 48000.0;
    double const freq = 440.0;

    osc1.coeffs_.set_amplitude(1.0, 0);
    osc1.state_.set_frequency(
        FrequencyParameters<double>{.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .phase_in_radians = 0.0});

    osc2.coeffs_.set_amplitude(1.0, 0);
    osc2.state_.set_frequency(
        FrequencyParameters<double>{.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .phase_in_radians = 0.0});

    // Generate using dynamic span
    std::array<double, 64> output1{};
    process_in_place(osc1, std::span<double>(output1));

    // Generate using fixed-size span
    std::array<double, 64> output2{};
    process_in_place(osc2, std::span<double, 64>(output2));

    // Results should be identical
    for (size_t i = 0; i < 64; ++i) {
        EXPECT_EQ(output1[i], output2[i]);
    }
}

TEST(osc_process, fixed_span_process_mix_in_place)
{
    Oscillator<double> osc1;
    Oscillator<double> osc2;
    double const sample_rate = 48000.0;
    double const freq = 440.0;

    osc1.coeffs_.set_amplitude(1.0, 0);
    osc1.state_.set_frequency(
        FrequencyParameters<double>{.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .phase_in_radians = 0.0});

    osc2.coeffs_.set_amplitude(1.0, 0);
    osc2.state_.set_frequency(
        FrequencyParameters<double>{.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .phase_in_radians = 0.0});

    // Fill with DC offset
    std::array<double, 64> output1{};
    std::array<double, 64> output2{};
    for (size_t i = 0; i < 64; ++i) {
        output1[i] = 0.5;
        output2[i] = 0.5;
    }

    // Mix using dynamic span
    process_mix_in_place(osc1, std::span<double>(output1));

    // Mix using fixed-size span
    process_mix_in_place(osc2, std::span<double, 64>(output2));

    // Results should be identical
    for (size_t i = 0; i < 64; ++i) {
        EXPECT_EQ(output1[i], output2[i]);
    }
}

// SIMD Tests

TEST(osc_simd, construction)
{
    Oscillator<simd_float32x4> osc;
    // Should compile and not crash
    (void)osc;
}

TEST(osc_simd, different_frequencies_per_channel)
{
    Oscillator<simd_float32x4> osc;
    float const sample_rate = 48000.0f;

    // Set different frequencies for each channel
    osc.coeffs_.set_amplitude(1.0f, 0);
    osc.coeffs_.set_amplitude(1.0f, 1);
    osc.coeffs_.set_amplitude(1.0f, 2);
    osc.coeffs_.set_amplitude(1.0f, 3);

    osc.state_.set_frequency(
        FrequencyParameters<float>{.sample_rate_recip = 1.0f / sample_rate, .frequency = 440.0f, .phase_in_radians = 0.0f}, 0);
    osc.state_.set_frequency(
        FrequencyParameters<float>{.sample_rate_recip = 1.0f / sample_rate, .frequency = 880.0f, .phase_in_radians = 0.0f}, 1);
    osc.state_.set_frequency(
        FrequencyParameters<float>{.sample_rate_recip = 1.0f / sample_rate, .frequency = 1320.0f, .phase_in_radians = 0.0f}, 2);
    osc.state_.set_frequency(
        FrequencyParameters<float>{.sample_rate_recip = 1.0f / sample_rate, .frequency = 1760.0f, .phase_in_radians = 0.0f}, 3);

    // Each channel should have different a_ coefficient
    EXPECT_NE(osc.state_.a_[0], osc.state_.a_[1]);
    EXPECT_NE(osc.state_.a_[1], osc.state_.a_[2]);
    EXPECT_NE(osc.state_.a_[2], osc.state_.a_[3]);
}

TEST(osc_simd, process_sample)
{
    Oscillator<simd_float32x4> osc;
    float const sample_rate = 48000.0f;

    for (size_t ch = 0; ch < 4; ++ch) {
        osc.coeffs_.set_amplitude(1.0f, ch);
        osc.state_.set_frequency(
            FrequencyParameters<float>{
                .sample_rate_recip = 1.0f / sample_rate,
                .frequency = 440.0f * static_cast<float>(ch + 1),
                .phase_in_radians = 0.0f},
            ch);
    }

    simd_float32x4 input = simd_float32x4::zero();
    simd_float32x4 output = osc(input);

    // All channels should produce output
    // (after a few samples to let the oscillator ramp up)
    for (int i = 0; i < 10; ++i) {
        output = osc(input);
    }

    // At least some channels should have non-zero output
    bool has_output = false;
    for (size_t ch = 0; ch < 4; ++ch) {
        if (std::abs(output[ch]) > 0.01f) {
            has_output = true;
            break;
        }
    }
    EXPECT_TRUE(has_output);
}

// Main test runner function required bycreate_test_sourcelist =====
TEST_MAIN(statusbar_dsp, dsp_oscillator_test)