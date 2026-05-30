// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Unit tests for statusbar.dsp:biquad module

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

TEST(biquad_traits, value_type)
{
    using FilterDouble = BiQuad<double>;
    static_assert(std::is_same_v<FilterDouble::value_type, double>);
    static_assert(std::is_same_v<FilterDouble::item_type, double>);
}

TEST(biquad_traits, vector_size_scalar)
{
    using FilterDouble = BiQuad<double>;
    EXPECT_EQ(FilterDouble::vector_size, 1U);
    EXPECT_EQ(FilterDouble::flattened_size, 1U);
}

TEST(biquad_traits, vector_size_simd)
{
    using FilterSimd = BiQuad<simd_float32x4>;
    EXPECT_EQ(FilterSimd::vector_size, 4U);
    EXPECT_EQ(FilterSimd::flattened_size, 4U);
}

// Construction Tests

TEST(biquad_construction, default)
{
    BiQuad<double> filter;
    // Should compile and not crash
    (void)filter;
}

TEST(biquad_construction, copy_ctor)
{
    BiQuad<double> filter1;
    filter1.coeffs.set_bypass();
    BiQuad<double> filter2 = filter1;
    EXPECT_EQ(filter2.coeffs.a0, 1.0);
}

TEST(biquad_construction, copy_assign)
{
    BiQuad<double> filter1;
    filter1.coeffs.set_bypass();
    BiQuad<double> filter2;
    filter2 = filter1;
    EXPECT_EQ(filter2.coeffs.a0, 1.0);
}

TEST(biquad_construction, move)
{
    BiQuad<double> filter1;
    filter1.coeffs.set_bypass();
    BiQuad<double> filter2 = std::move(filter1);
    EXPECT_EQ(filter2.coeffs.a0, 1.0);
}

TEST(biquad_construction, move_assignment)
{
    BiQuad<double> filter1;
    filter1.coeffs.set_bypass();
    BiQuad<double> filter2;
    filter2 = std::move(filter1);
    EXPECT_EQ(filter2.coeffs.a0, 1.0);
}

// Coefficients Tests

TEST(biquad_coeffs, set_bypass)
{
    BiQuad<double> filter;
    filter.coeffs.set_bypass();

    EXPECT_EQ(filter.coeffs.a0, 1.0);
    EXPECT_EQ(filter.coeffs.a1, 0.0);
    EXPECT_EQ(filter.coeffs.a2, 0.0);
    EXPECT_EQ(filter.coeffs.b1, 0.0);
    EXPECT_EQ(filter.coeffs.b2, 0.0);
}

TEST(biquad_coeffs, set_manual)
{
    BiQuad<double> filter;
    filter.coeffs.set(0.5, 0.25, 0.125, 0.1, 0.05);

    EXPECT_EQ(filter.coeffs.a0, 0.5);
    EXPECT_EQ(filter.coeffs.a1, 0.25);
    EXPECT_EQ(filter.coeffs.a2, 0.125);
    EXPECT_EQ(filter.coeffs.b1, 0.1);
    EXPECT_EQ(filter.coeffs.b2, 0.05);
}

TEST(biquad_coeffs, lowpass_produces_valid_coefficients)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double freq = 1000.0;
    constexpr double q = 0.707;

    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .q = q});

    // Coefficients should be finite and within reasonable range
    EXPECT_TRUE(std::isfinite(filter.coeffs.a0));
    EXPECT_TRUE(std::isfinite(filter.coeffs.a1));
    EXPECT_TRUE(std::isfinite(filter.coeffs.a2));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b1));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b2));

    // a0 should be positive for lowpass
    EXPECT_TRUE(filter.coeffs.a0 > 0.0);
}

TEST(biquad_coeffs, highpass_produces_valid_coefficients)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double freq = 1000.0;
    constexpr double q = 0.707;

    filter.coeffs.calculate_highpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .q = q});

    EXPECT_TRUE(std::isfinite(filter.coeffs.a0));
    EXPECT_TRUE(std::isfinite(filter.coeffs.a1));
    EXPECT_TRUE(std::isfinite(filter.coeffs.a2));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b1));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b2));

    // a0 should be positive for highpass
    EXPECT_TRUE(filter.coeffs.a0 > 0.0);
}

TEST(biquad_coeffs, bandpass_produces_valid_coefficients)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double freq = 1000.0;
    constexpr double q = 1.0;

    filter.coeffs.calculate_bandpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .q = q});

    EXPECT_TRUE(std::isfinite(filter.coeffs.a0));
    EXPECT_TRUE(std::isfinite(filter.coeffs.a1));
    EXPECT_TRUE(std::isfinite(filter.coeffs.a2));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b1));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b2));

    // a1 should be zero for symmetric bandpass
    EXPECT_TRUE(approx_equal(filter.coeffs.a1, 0.0));
}

TEST(biquad_coeffs, notch_produces_valid_coefficients)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double freq = 1000.0;
    constexpr double q = 1.0;

    filter.coeffs.calculate_notch({.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .q = q});

    EXPECT_TRUE(std::isfinite(filter.coeffs.a0));
    EXPECT_TRUE(std::isfinite(filter.coeffs.a1));
    EXPECT_TRUE(std::isfinite(filter.coeffs.a2));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b1));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b2));

    // a0 and a2 should be equal for notch filter
    EXPECT_TRUE(approx_equal(filter.coeffs.a0, filter.coeffs.a2));
}

TEST(biquad_coeffs, peak_boost_produces_valid_coefficients)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double freq = 1000.0;
    constexpr double q = 1.0;
    constexpr double gain_db = 6.0;

    filter.coeffs.calculate_peak({.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .q = q, .gain_db = gain_db});

    EXPECT_TRUE(std::isfinite(filter.coeffs.a0));
    EXPECT_TRUE(std::isfinite(filter.coeffs.a1));
    EXPECT_TRUE(std::isfinite(filter.coeffs.a2));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b1));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b2));
}

TEST(biquad_coeffs, peak_cut_produces_valid_coefficients)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double freq = 1000.0;
    constexpr double q = 1.0;
    constexpr double gain_db = -6.0;

    filter.coeffs.calculate_peak({.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .q = q, .gain_db = gain_db});

    EXPECT_TRUE(std::isfinite(filter.coeffs.a0));
    EXPECT_TRUE(std::isfinite(filter.coeffs.a1));
    EXPECT_TRUE(std::isfinite(filter.coeffs.a2));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b1));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b2));
}

TEST(biquad_coeffs, lowshelf_boost_produces_valid_coefficients)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double freq = 200.0;
    constexpr double gain_db = 6.0;

    filter.coeffs.calculate_lowshelf({.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .gain_db = gain_db});

    EXPECT_TRUE(std::isfinite(filter.coeffs.a0));
    EXPECT_TRUE(std::isfinite(filter.coeffs.a1));
    EXPECT_TRUE(std::isfinite(filter.coeffs.a2));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b1));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b2));
}

TEST(biquad_coeffs, lowshelf_cut_produces_valid_coefficients)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double freq = 200.0;
    constexpr double gain_db = -6.0;

    filter.coeffs.calculate_lowshelf({.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .gain_db = gain_db});

    EXPECT_TRUE(std::isfinite(filter.coeffs.a0));
    EXPECT_TRUE(std::isfinite(filter.coeffs.a1));
    EXPECT_TRUE(std::isfinite(filter.coeffs.a2));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b1));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b2));
}

TEST(biquad_coeffs, highshelf_boost_produces_valid_coefficients)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double freq = 8000.0;
    constexpr double gain_db = 6.0;

    filter.coeffs.calculate_highshelf({.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .gain_db = gain_db});

    EXPECT_TRUE(std::isfinite(filter.coeffs.a0));
    EXPECT_TRUE(std::isfinite(filter.coeffs.a1));
    EXPECT_TRUE(std::isfinite(filter.coeffs.a2));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b1));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b2));
}

TEST(biquad_coeffs, highshelf_cut_produces_valid_coefficients)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double freq = 8000.0;
    constexpr double gain_db = -6.0;

    filter.coeffs.calculate_highshelf({.sample_rate_recip = 1.0 / sample_rate, .frequency = freq, .gain_db = gain_db});

    EXPECT_TRUE(std::isfinite(filter.coeffs.a0));
    EXPECT_TRUE(std::isfinite(filter.coeffs.a1));
    EXPECT_TRUE(std::isfinite(filter.coeffs.a2));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b1));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b2));
}

// State Tests

TEST(biquad_state, initial_zero)
{
    BiQuad<double> filter;
    // State should start at zero (default constructed)
    EXPECT_EQ(filter.state.z1, 0.0);
    EXPECT_EQ(filter.state.z2, 0.0);
}

TEST(biquad_state, reset)
{
    BiQuad<double> filter;
    filter.coeffs.set_bypass();

    // Process some samples to change state
    (void)filter(1.0);
    (void)filter(0.5);

    // State should be non-zero after processing
    // (but bypass filter has b1=b2=0, so state might still be zero)

    // Reset and verify
    filter.reset();
    EXPECT_EQ(filter.state.z1, 0.0);
    EXPECT_EQ(filter.state.z2, 0.0);
}

TEST(biquad_state, state_reset_method)
{
    BiQuad<double> filter;
    filter.state.z1 = 1.5;
    filter.state.z2 = 2.5;

    filter.state.reset();

    EXPECT_EQ(filter.state.z1, 0.0);
    EXPECT_EQ(filter.state.z2, 0.0);
}

// Processing Tests

TEST(biquad_process, bypass_passes_signal_unchanged)
{
    BiQuad<double> filter;
    filter.coeffs.set_bypass();

    EXPECT_EQ(filter(1.0), 1.0);
    EXPECT_EQ(filter(0.5), 0.5);
    EXPECT_EQ(filter(-0.25), -0.25);
    EXPECT_EQ(filter(0.0), 0.0);
}

TEST(biquad_process, lowpass_attenuates_high_frequency)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double cutoff = 1000.0;  // 1 kHz cutoff
    constexpr double q = 0.707;

    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = cutoff, .q = q});

    // Generate a high-frequency sine wave (10 kHz, well above cutoff)
    std::vector<double> input(4800);
    generate_sine(input, 10000.0, sample_rate);
    std::vector<double> output(input.size());

    // Let filter settle
    for (size_t i = 0; i < 480; ++i) {
        (void)filter(input[i]);
    }

    // Process remaining samples
    for (size_t i = 480; i < input.size(); ++i) {
        output[i] = filter(input[i]);
    }

    // Calculate RMS of output (should be significantly lower than input)
    double input_rms = calculate_rms(std::span<double const>(input));
    double output_rms = calculate_rms(std::span<double const>(output).subspan(480));

    // High frequency should be attenuated by at least 20 dB
    double attenuation_db = 20.0 * std::log10(output_rms / input_rms);
    EXPECT_TRUE(attenuation_db < -20.0);
}

TEST(biquad_process, lowpass_passes_low_frequency)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double cutoff = 10000.0;  // 10 kHz cutoff
    constexpr double q = 0.707;

    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = cutoff, .q = q});

    // Generate a low-frequency sine wave (100 Hz, well below cutoff)
    std::vector<double> input(4800);
    generate_sine(input, 100.0, sample_rate);
    std::vector<double> output(input.size());

    // Let filter settle
    for (size_t i = 0; i < 480; ++i) {
        (void)filter(input[i]);
    }

    // Process remaining samples
    for (size_t i = 480; i < input.size(); ++i) {
        output[i] = filter(input[i]);
    }

    // Calculate RMS of output (should be close to input)
    double input_rms = calculate_rms(std::span<double const>(input).subspan(480));
    double output_rms = calculate_rms(std::span<double const>(output).subspan(480));

    // Low frequency should pass with minimal attenuation (within 1 dB)
    double ratio = output_rms / input_rms;
    EXPECT_TRUE(ratio > 0.9);
    EXPECT_TRUE(ratio < 1.1);
}

TEST(biquad_process, highpass_attenuates_low_frequency)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double cutoff = 1000.0;  // 1 kHz cutoff
    constexpr double q = 0.707;

    filter.coeffs.calculate_highpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = cutoff, .q = q});

    // Generate a low-frequency sine wave (50 Hz, well below cutoff)
    std::vector<double> input(4800);
    generate_sine(input, 50.0, sample_rate);
    std::vector<double> output(input.size());

    // Let filter settle
    for (size_t i = 0; i < 480; ++i) {
        (void)filter(input[i]);
    }

    // Process remaining samples
    for (size_t i = 480; i < input.size(); ++i) {
        output[i] = filter(input[i]);
    }

    // Calculate RMS
    double input_rms = calculate_rms(std::span<double const>(input));
    double output_rms = calculate_rms(std::span<double const>(output).subspan(480));

    // Low frequency should be attenuated by at least 20 dB
    double attenuation_db = 20.0 * std::log10(output_rms / input_rms);
    EXPECT_TRUE(attenuation_db < -20.0);
}

// Batch Processing Tests(using free functions from dsp_process) =====

TEST(biquad_batch, process_in_place)
{
    BiQuad<double> filter;
    filter.coeffs.set_bypass();

    std::vector<double> samples = {1.0, 2.0, 3.0, 4.0, 5.0};
    process_in_place(filter, std::span<double>(samples));

    // Bypass should not change values
    EXPECT_EQ(samples[0], 1.0);
    EXPECT_EQ(samples[1], 2.0);
    EXPECT_EQ(samples[2], 3.0);
    EXPECT_EQ(samples[3], 4.0);
    EXPECT_EQ(samples[4], 5.0);
}

TEST(biquad_batch, process_separate_buffers)
{
    BiQuad<double> filter;
    filter.coeffs.set_bypass();

    std::vector<double> const input = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> output(5);

    process(filter, std::span<double const>(input), std::span<double>(output));

    // Bypass should copy values unchanged
    EXPECT_EQ(output[0], 1.0);
    EXPECT_EQ(output[1], 2.0);
    EXPECT_EQ(output[2], 3.0);
    EXPECT_EQ(output[3], 4.0);
    EXPECT_EQ(output[4], 5.0);
}

TEST(biquad_batch, process_output_smaller_than_input)
{
    BiQuad<double> filter;
    filter.coeffs.set_bypass();

    std::vector<double> const input = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> output(3);  // Smaller than input

    process(filter, std::span<double const>(input), std::span<double>(output));

    // Should only process 3 samples
    EXPECT_EQ(output[0], 1.0);
    EXPECT_EQ(output[1], 2.0);
    EXPECT_EQ(output[2], 3.0);
}

TEST(biquad_batch, process_input_smaller_than_output)
{
    BiQuad<double> filter;
    filter.coeffs.set_bypass();

    std::vector<double> const input = {1.0, 2.0, 3.0};
    std::vector<double> output(5, 0.0);  // Larger than input, pre-filled with zeros

    process(filter, std::span<double const>(input), std::span<double>(output));

    // Should only process 3 samples, rest unchanged
    EXPECT_EQ(output[0], 1.0);
    EXPECT_EQ(output[1], 2.0);
    EXPECT_EQ(output[2], 3.0);
    EXPECT_EQ(output[3], 0.0);  // Unchanged
    EXPECT_EQ(output[4], 0.0);  // Unchanged
}

TEST(biquad_batch, process_empty_span)
{
    BiQuad<double> filter;
    filter.coeffs.set_bypass();

    std::span<double> empty;
    process_in_place(filter, empty);  // Should not crash
}

TEST(biquad_batch, fixed_span_in_place)
{
    BiQuad<double> filter1;
    BiQuad<double> filter2;
    filter1.coeffs.set_bypass();
    filter2.coeffs.set_bypass();

    std::array<double, 64> samples1 = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::array<double, 64> samples2 = {1.0, 2.0, 3.0, 4.0, 5.0};

    // Process with dynamic span
    process_in_place(filter1, std::span<double>(samples1));

    // Process with fixed-size span
    process_in_place(filter2, std::span<double, 64>(samples2));

    // Results should be identical
    for (size_t i = 0; i < 64; ++i) {
        EXPECT_EQ(samples1[i], samples2[i]);
    }
}

TEST(biquad_batch, fixed_span_separate_buffers)
{
    BiQuad<double> filter1;
    BiQuad<double> filter2;
    filter1.coeffs.set_bypass();
    filter2.coeffs.set_bypass();

    std::array<double, 64> input = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::array<double, 64> output1{};
    std::array<double, 64> output2{};

    // Process with dynamic spans
    process(filter1, std::span<double const>(input), std::span<double>(output1));

    // Process with fixed-size spans
    process(filter2, std::span<double const, 64>(input), std::span<double, 64>(output2));

    // Results should be identical
    for (size_t i = 0; i < 64; ++i) {
        EXPECT_EQ(output1[i], output2[i]);
    }
}

TEST(biquad_batch, process_mix_in_place)
{
    BiQuad<double> filter;
    filter.coeffs.set_bypass();

    std::vector<double> samples = {1.0, 2.0, 3.0, 4.0, 5.0};
    process_mix_in_place(filter, std::span<double>(samples));

    // Bypass + mix should double values (original + processed = 2x original)
    EXPECT_EQ(samples[0], 2.0);
    EXPECT_EQ(samples[1], 4.0);
    EXPECT_EQ(samples[2], 6.0);
    EXPECT_EQ(samples[3], 8.0);
    EXPECT_EQ(samples[4], 10.0);
}

TEST(biquad_batch, process_accumulate)
{
    BiQuad<double> filter;
    filter.coeffs.set_bypass();

    std::vector<double> const input = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> output = {10.0, 20.0, 30.0, 40.0, 50.0};

    process_accumulate(filter, std::span<double const>(input), std::span<double>(output));

    // Should add processed values to existing output
    EXPECT_EQ(output[0], 11.0);
    EXPECT_EQ(output[1], 22.0);
    EXPECT_EQ(output[2], 33.0);
    EXPECT_EQ(output[3], 44.0);
    EXPECT_EQ(output[4], 55.0);
}

// Z-Domain / Frequency Response Tests

// Helper to compute z^-1 = e^(-jω) for a given frequency
static std::complex<double> z_inv_for_freq(double freq, double sample_rate)
{
    double omega = 2.0 * std::numbers::pi * freq / sample_rate;
    return std::complex<double>(std::cos(-omega), std::sin(-omega));
}

TEST(biquad_z_domain, bypass_unity_response)
{
    BiQuad<double> filter;
    filter.coeffs.set_bypass();

    constexpr double sample_rate = 48000.0;

    // Test at various frequencies - bypass should have unity gain
    for (double freq : {100.0, 1000.0, 5000.0, 10000.0, 20000.0}) {
        auto z1 = z_inv_for_freq(freq, sample_rate);
        auto response = filter.coeffs.process_z_domain(z1);
        double magnitude = std::abs(response);

        // Bypass should have unity gain (magnitude ≈ 1.0)
        EXPECT_TRUE(approx_equal(magnitude, 1.0, 1e-10));
    }
}

TEST(biquad_z_domain, bypass_zero_phase)
{
    BiQuad<double> filter;
    filter.coeffs.set_bypass();

    constexpr double sample_rate = 48000.0;

    // Test at various frequencies - bypass should have zero phase shift
    for (double freq : {100.0, 1000.0, 5000.0, 10000.0}) {
        auto z1 = z_inv_for_freq(freq, sample_rate);
        auto response = filter.coeffs.process_z_domain(z1);
        double phase = std::arg(response);

        // Bypass should have zero phase shift
        EXPECT_TRUE(approx_equal(phase, 0.0, 1e-10));
    }
}

TEST(biquad_z_domain, lowpass_cutoff_minus_3db)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double cutoff = 1000.0;
    constexpr double q = std::numbers::sqrt2 / 2.0;  // Butterworth Q = 0.7071

    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = cutoff, .q = q});

    // At cutoff frequency, Butterworth filter should have -3 dB response
    auto z1 = z_inv_for_freq(cutoff, sample_rate);
    auto response = filter.coeffs.process_z_domain(z1);
    double magnitude = std::abs(response);
    double magnitude_db = 20.0 * std::log10(magnitude);

    // -3 dB means magnitude ≈ 0.7071 (1/sqrt(2))
    EXPECT_TRUE(approx_equal(magnitude_db, -3.0, 0.1));
}

TEST(biquad_z_domain, lowpass_dc_unity)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double cutoff = 1000.0;
    constexpr double q = 0.707;

    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = cutoff, .q = q});

    // At DC (0 Hz), lowpass should have unity gain
    auto z1 = z_inv_for_freq(0.0, sample_rate);  // z^-1 = e^0 = 1
    auto response = filter.coeffs.process_z_domain(z1);
    double magnitude = std::abs(response);

    EXPECT_TRUE(approx_equal(magnitude, 1.0, 1e-6));
}

TEST(biquad_z_domain, lowpass_high_freq_attenuation)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double cutoff = 1000.0;
    constexpr double q = 0.707;

    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = cutoff, .q = q});

    // At 10x cutoff frequency, should have significant attenuation
    double high_freq = 10000.0;
    auto z1 = z_inv_for_freq(high_freq, sample_rate);
    auto response = filter.coeffs.process_z_domain(z1);
    double magnitude = std::abs(response);
    double magnitude_db = 20.0 * std::log10(magnitude);

    // 2nd order lowpass: ~40 dB attenuation at 10x cutoff
    EXPECT_TRUE(magnitude_db < -30.0);
}

TEST(biquad_z_domain, highpass_cutoff_minus_3db)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double cutoff = 1000.0;
    constexpr double q = std::numbers::sqrt2 / 2.0;

    filter.coeffs.calculate_highpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = cutoff, .q = q});

    // At cutoff frequency, Butterworth highpass should have -3 dB response
    auto z1 = z_inv_for_freq(cutoff, sample_rate);
    auto response = filter.coeffs.process_z_domain(z1);
    double magnitude = std::abs(response);
    double magnitude_db = 20.0 * std::log10(magnitude);

    EXPECT_TRUE(approx_equal(magnitude_db, -3.0, 0.1));
}

TEST(biquad_z_domain, highpass_dc_zero)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double cutoff = 1000.0;
    constexpr double q = 0.707;

    filter.coeffs.calculate_highpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = cutoff, .q = q});

    // At DC (0 Hz), highpass should have zero gain
    auto z1 = z_inv_for_freq(0.0, sample_rate);
    auto response = filter.coeffs.process_z_domain(z1);
    double magnitude = std::abs(response);

    EXPECT_TRUE(approx_equal(magnitude, 0.0, 1e-10));
}

TEST(biquad_z_domain, highpass_high_freq_unity)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double cutoff = 1000.0;
    constexpr double q = 0.707;

    filter.coeffs.calculate_highpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = cutoff, .q = q});

    // At high frequencies (well above cutoff), should approach unity
    double high_freq = 20000.0;
    auto z1 = z_inv_for_freq(high_freq, sample_rate);
    auto response = filter.coeffs.process_z_domain(z1);
    double magnitude = std::abs(response);

    // Should be close to unity gain
    EXPECT_TRUE(magnitude > 0.95);
}

TEST(biquad_z_domain, bandpass_center_freq_peak)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double center_freq = 1000.0;
    constexpr double q = 2.0;

    filter.coeffs.calculate_bandpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = center_freq, .q = q});

    // Get response at center frequency
    auto z_center = z_inv_for_freq(center_freq, sample_rate);
    auto response_center = filter.coeffs.process_z_domain(z_center);
    double mag_center = std::abs(response_center);

    // Get response at frequency well below center
    auto z_low = z_inv_for_freq(100.0, sample_rate);
    auto response_low = filter.coeffs.process_z_domain(z_low);
    double mag_low = std::abs(response_low);

    // Get response at frequency well above center
    auto z_high = z_inv_for_freq(10000.0, sample_rate);
    auto response_high = filter.coeffs.process_z_domain(z_high);
    double mag_high = std::abs(response_high);

    // Center frequency should have highest magnitude
    EXPECT_TRUE(mag_center > mag_low);
    EXPECT_TRUE(mag_center > mag_high);
}

TEST(biquad_z_domain, notch_center_freq_null)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double notch_freq = 1000.0;
    constexpr double q = 2.0;

    filter.coeffs.calculate_notch({.sample_rate_recip = 1.0 / sample_rate, .frequency = notch_freq, .q = q});

    // At notch frequency, should have deep null
    auto z1 = z_inv_for_freq(notch_freq, sample_rate);
    auto response = filter.coeffs.process_z_domain(z1);
    double magnitude = std::abs(response);
    double magnitude_db = 20.0 * std::log10(magnitude);

    // Notch should have very deep attenuation at center frequency
    EXPECT_TRUE(magnitude_db < -40.0);
}

TEST(biquad_z_domain, notch_passband_unity)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double notch_freq = 1000.0;
    constexpr double q = 2.0;

    filter.coeffs.calculate_notch({.sample_rate_recip = 1.0 / sample_rate, .frequency = notch_freq, .q = q});

    // At DC and Nyquist, should approach unity
    auto z_dc = z_inv_for_freq(0.0, sample_rate);
    auto response_dc = filter.coeffs.process_z_domain(z_dc);
    double mag_dc = std::abs(response_dc);

    EXPECT_TRUE(approx_equal(mag_dc, 1.0, 0.01));
}

TEST(biquad_z_domain, peak_boost_gain)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double center_freq = 1000.0;
    constexpr double q = 2.0;
    constexpr double gain_db = 6.0;

    filter.coeffs.calculate_peak({.sample_rate_recip = 1.0 / sample_rate, .frequency = center_freq, .q = q, .gain_db = gain_db});

    // At center frequency, should have approximately +6 dB gain
    auto z1 = z_inv_for_freq(center_freq, sample_rate);
    auto response = filter.coeffs.process_z_domain(z1);
    double magnitude = std::abs(response);
    double magnitude_db = 20.0 * std::log10(magnitude);

    EXPECT_TRUE(approx_equal(magnitude_db, gain_db, 0.5));
}

TEST(biquad_z_domain, peak_cut_gain)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double center_freq = 1000.0;
    constexpr double q = 2.0;
    constexpr double gain_db = -6.0;

    filter.coeffs.calculate_peak({.sample_rate_recip = 1.0 / sample_rate, .frequency = center_freq, .q = q, .gain_db = gain_db});

    // At center frequency, should have approximately -6 dB gain
    auto z1 = z_inv_for_freq(center_freq, sample_rate);
    auto response = filter.coeffs.process_z_domain(z1);
    double magnitude = std::abs(response);
    double magnitude_db = 20.0 * std::log10(magnitude);

    EXPECT_TRUE(approx_equal(magnitude_db, gain_db, 0.5));
}

TEST(biquad_z_domain, lowshelf_boost_dc_gain)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double shelf_freq = 200.0;
    constexpr double gain_db = 6.0;

    filter.coeffs.calculate_lowshelf({.sample_rate_recip = 1.0 / sample_rate, .frequency = shelf_freq, .gain_db = gain_db});

    // At DC, should have approximately +6 dB gain
    auto z_dc = z_inv_for_freq(0.0, sample_rate);
    auto response_dc = filter.coeffs.process_z_domain(z_dc);
    double mag_dc_db = 20.0 * std::log10(std::abs(response_dc));

    EXPECT_TRUE(approx_equal(mag_dc_db, gain_db, 0.5));

    // At high frequency, should approach unity
    auto z_high = z_inv_for_freq(10000.0, sample_rate);
    auto response_high = filter.coeffs.process_z_domain(z_high);
    double mag_high = std::abs(response_high);

    EXPECT_TRUE(approx_equal(mag_high, 1.0, 0.1));
}

TEST(biquad_z_domain, highshelf_boost_high_freq_gain)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double shelf_freq = 8000.0;
    constexpr double gain_db = 6.0;

    filter.coeffs.calculate_highshelf({.sample_rate_recip = 1.0 / sample_rate, .frequency = shelf_freq, .gain_db = gain_db});

    // At DC, should be near unity
    auto z_dc = z_inv_for_freq(0.0, sample_rate);
    auto response_dc = filter.coeffs.process_z_domain(z_dc);
    double mag_dc = std::abs(response_dc);

    EXPECT_TRUE(approx_equal(mag_dc, 1.0, 0.1));

    // At Nyquist, should have approximately +6 dB gain
    auto z_nyquist = z_inv_for_freq(sample_rate / 2.0 - 100, sample_rate);
    auto response_nyquist = filter.coeffs.process_z_domain(z_nyquist);
    double mag_nyquist_db = 20.0 * std::log10(std::abs(response_nyquist));

    EXPECT_TRUE(approx_equal(mag_nyquist_db, gain_db, 1.0));
}

TEST(biquad_z_domain, phase_response_lowpass)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double cutoff = 1000.0;
    constexpr double q = 0.707;

    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = cutoff, .q = q});

    // At DC, phase should be 0
    auto z_dc = z_inv_for_freq(0.0, sample_rate);
    auto response_dc = filter.coeffs.process_z_domain(z_dc);
    double phase_dc = std::arg(response_dc);
    EXPECT_TRUE(approx_equal(phase_dc, 0.0, 1e-6));

    // At cutoff, phase should be approximately -π/2 for 2nd order Butterworth
    auto z_cutoff = z_inv_for_freq(cutoff, sample_rate);
    auto response_cutoff = filter.coeffs.process_z_domain(z_cutoff);
    double phase_cutoff = std::arg(response_cutoff);
    EXPECT_TRUE(phase_cutoff < 0.0);  // Phase lag (negative)
}

// SIMD Tests

TEST(biquad_simd, simd_filter_construction)
{
    BiQuad<simd_float32x4> filter;
    // Should compile and not crash
    (void)filter;
}

TEST(biquad_simd, simd_set_bypass_per_channel)
{
    BiQuad<simd_float32x4> filter;

    // Set bypass for all channels
    for (size_t ch = 0; ch < 4; ++ch) {
        filter.coeffs.set_bypass(ch);
    }

    // Verify all channels have bypass coefficients
    EXPECT_EQ(filter.coeffs.a0[0], 1.0f);
    EXPECT_EQ(filter.coeffs.a0[1], 1.0f);
    EXPECT_EQ(filter.coeffs.a0[2], 1.0f);
    EXPECT_EQ(filter.coeffs.a0[3], 1.0f);
}

TEST(biquad_simd, simd_different_filters_per_channel)
{
    BiQuad<simd_float32x4> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double q = 0.707;

    // Set different cutoff frequencies for each channel
    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = 100.0, .q = q, .channel = 0});
    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = 500.0, .q = q, .channel = 1});
    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = 1000.0, .q = q, .channel = 2});
    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = 5000.0, .q = q, .channel = 3});

    // Each channel should have different a0 coefficients
    EXPECT_NE(filter.coeffs.a0[0], filter.coeffs.a0[1]);
    EXPECT_NE(filter.coeffs.a0[1], filter.coeffs.a0[2]);
    EXPECT_NE(filter.coeffs.a0[2], filter.coeffs.a0[3]);
}

TEST(biquad_simd, simd_process_sample)
{
    BiQuad<simd_float32x4> filter;

    // Set bypass for all channels
    for (size_t ch = 0; ch < 4; ++ch) {
        filter.coeffs.set_bypass(ch);
    }

    simd_float32x4 input{1.0f, 2.0f, 3.0f, 4.0f};
    simd_float32x4 output = filter(input);

    // Bypass should pass signal unchanged
    EXPECT_EQ(output[0], 1.0f);
    EXPECT_EQ(output[1], 2.0f);
    EXPECT_EQ(output[2], 3.0f);
    EXPECT_EQ(output[3], 4.0f);
}

// Edge Case Tests

TEST(biquad_edge, zero_input)
{
    BiQuad<double> filter;
    filter.coeffs.set_bypass();
    EXPECT_EQ(filter(0.0), 0.0);
}

TEST(biquad_edge, negative_input)
{
    BiQuad<double> filter;
    filter.coeffs.set_bypass();
    EXPECT_EQ(filter(-1.0), -1.0);
}

TEST(biquad_edge, state_accumulation)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double cutoff = 1000.0;
    constexpr double q = 0.707;

    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = cutoff, .q = q});

    // Process an impulse
    (void)filter(1.0);
    // State should be non-zero after processing
    // Since it's a lowpass with feedback, state will be affected
    (void)filter(0.0);
    (void)filter(0.0);

    // After processing more zeros, state should decay but not be exactly zero
    filter.reset();
    EXPECT_EQ(filter.state.z1, 0.0);
    EXPECT_EQ(filter.state.z2, 0.0);
}

TEST(biquad_edge, very_low_cutoff)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double cutoff = 10.0;  // Very low: 10 Hz
    constexpr double q = 0.707;

    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = cutoff, .q = q});

    // Coefficients should be valid
    EXPECT_TRUE(std::isfinite(filter.coeffs.a0));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b1));
}

TEST(biquad_edge, very_high_cutoff)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double cutoff = 20000.0;  // Near Nyquist
    constexpr double q = 0.707;

    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = cutoff, .q = q});

    // Coefficients should be valid
    EXPECT_TRUE(std::isfinite(filter.coeffs.a0));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b1));
}

TEST(biquad_edge, high_q_factor)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double cutoff = 1000.0;
    constexpr double q = 10.0;  // High Q for resonance

    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = cutoff, .q = q});

    // Coefficients should be valid even with high Q
    EXPECT_TRUE(std::isfinite(filter.coeffs.a0));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b1));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b2));
}

TEST(biquad_edge, low_q_factor)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double cutoff = 1000.0;
    constexpr double q = 0.1;  // Very low Q (overdamped)

    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / sample_rate, .frequency = cutoff, .q = q});

    // Coefficients should be valid
    EXPECT_TRUE(std::isfinite(filter.coeffs.a0));
    EXPECT_TRUE(std::isfinite(filter.coeffs.b1));
}

TEST(biquad_edge, peak_zero_gain)
{
    BiQuad<double> filter;
    constexpr double sample_rate = 48000.0;
    constexpr double cutoff = 1000.0;
    constexpr double q = 1.0;
    constexpr double gain_db = 0.0;  // Unity gain

    filter.coeffs.calculate_peak({.sample_rate_recip = 1.0 / sample_rate, .frequency = cutoff, .q = q, .gain_db = gain_db});

    // With 0 dB gain, peak filter should behave like bypass
    auto z1 = z_inv_for_freq(cutoff, sample_rate);
    auto response = filter.coeffs.process_z_domain(z1);
    double magnitude = std::abs(response);
    EXPECT_TRUE(approx_equal(magnitude, 1.0, 0.01));
}

TEST(biquad_edge, consecutive_resets)
{
    BiQuad<double> filter;
    filter.coeffs.set_bypass();
    filter.state.z1 = 1.0;
    filter.state.z2 = 2.0;

    filter.reset();
    EXPECT_EQ(filter.state.z1, 0.0);
    EXPECT_EQ(filter.state.z2, 0.0);

    // Reset again should have no effect
    filter.reset();
    EXPECT_EQ(filter.state.z1, 0.0);
    EXPECT_EQ(filter.state.z2, 0.0);
}

TEST(biquad_edge, different_sample_rates)
{
    // Same cutoff but different sample rates should give different coefficients
    BiQuad<double> filter_48k;
    BiQuad<double> filter_96k;
    constexpr double cutoff = 1000.0;
    constexpr double q = 0.707;

    filter_48k.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / 48000.0, .frequency = cutoff, .q = q});
    filter_96k.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / 96000.0, .frequency = cutoff, .q = q});

    // Coefficients should be different
    EXPECT_NE(filter_48k.coeffs.a0, filter_96k.coeffs.a0);
    EXPECT_NE(filter_48k.coeffs.b1, filter_96k.coeffs.b1);
}

// Main test runner function required bycreate_test_sourcelist =====
TEST_MAIN(statusbar_dsp, dsp_biquad_test)