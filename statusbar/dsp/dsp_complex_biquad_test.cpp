// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Unit tests for statusbar.dsp:complex_biquad module

#include "statusbar/dsp/dsp.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <cmath>
#include <numbers>

using namespace statusbar::dsp;

// ============================================================================
// Type Traits Tests
// ============================================================================

TEST(cbiquad_traits, value_type)
{
    using FilterDouble = ComplexBiQuad<double>;
    static_assert(std::is_same_v<FilterDouble::value_type, double>);
    static_assert(std::is_same_v<FilterDouble::item_type, double>);
}

TEST(cbiquad_traits, vector_size_scalar)
{
    using FilterDouble = ComplexBiQuad<double>;
    EXPECT_EQ(FilterDouble::vector_size, 1U);
    EXPECT_EQ(FilterDouble::flattened_size, 1U);
}

TEST(cbiquad_traits, vector_size_simd)
{
    using FilterSimd = ComplexBiQuad<simd_float32x4>;
    EXPECT_EQ(FilterSimd::vector_size, 4U);
    EXPECT_EQ(FilterSimd::flattened_size, 4U);
}

// ============================================================================
// Construction Tests
// ============================================================================

TEST(cbiquad_construction, default)
{
    ComplexBiQuad<double> filter;
    (void)filter;
}

TEST(cbiquad_construction, copy)
{
    ComplexBiQuad<double> filter1;
    filter1.coeffs.set_bypass();
    ComplexBiQuad<double> filter2 = filter1;
    (void)filter2;
}

// ============================================================================
// Bypass Tests
// ============================================================================

TEST(cbiquad_bypass, double_passthrough)
{
    ComplexBiQuad<double> filter;
    filter.coeffs.set_bypass();

    for (int i = 0; i < 100; ++i) {
        double input = static_cast<double>(i) * 0.01;
        double output = filter(input);
        EXPECT_TRUE(approx_equal(output, input, 1e-12));
    }
}

TEST(cbiquad_bypass, float_passthrough)
{
    ComplexBiQuad<float> filter;
    filter.coeffs.set_bypass();

    for (int i = 0; i < 100; ++i) {
        float input = static_cast<float>(i) * 0.01f;
        float output = filter(input);
        EXPECT_TRUE(std::abs(output - input) < 1e-5f);
    }
}

// ============================================================================
// Match standard biquad output
// ============================================================================

static void compare_biquad_vs_complex(char const* label, double a0, double a1, double a2, double b1, double b2)
{
    BiQuad<double> bq;
    bq.coeffs.set(a0, a1, a2, b1, b2);

    ComplexBiQuad<double> cbq;
    cbq.coeffs.set_from_biquad_coeffs(a0, a1, a2, b1, b2);

    constexpr double sample_rate = 48000.0;
    constexpr double freq = 1000.0;
    constexpr double w = 2.0 * std::numbers::pi * freq / sample_rate;

    double max_err = 0.0;
    for (int i = 0; i < 4800; ++i) {
        double sample = std::sin(w * i);
        double out_bq = bq(sample);
        double out_cbq = cbq(sample);
        double err = std::abs(out_bq - out_cbq);
        max_err = std::max(max_err, err);
    }

    (void)label;
    EXPECT_TRUE(max_err < 1e-10);
}

TEST(cbiquad_match, lowpass)
{
    constexpr double sr = 48000.0;
    constexpr double freq = 1000.0;
    constexpr double q = 0.707;
    double const k = std::tan(std::numbers::pi * freq / sr);
    double const norm = 1.0 / (1.0 + (k / q) + (k * k));
    double const a0 = (k * k) * norm;
    double const a1 = 2.0 * a0;
    double const a2 = a0;
    double const b1 = 2.0 * ((k * k) - 1.0) * norm;
    double const b2 = (1.0 - (k / q) + (k * k)) * norm;
    compare_biquad_vs_complex("lowpass", a0, a1, a2, b1, b2);
}

TEST(cbiquad_match, negative_a0)
{
    // Sign-inverted numerator (negative leading coefficient): the two-stage
    // factorization carries the sign in one stage (g = sqrt(|a0|)), where a
    // plain sqrt(a0) would be NaN. Output must still match the reference biquad.
    constexpr double sr = 48000.0;
    constexpr double freq = 1000.0;
    constexpr double q = 0.707;
    double const k = std::tan(std::numbers::pi * freq / sr);
    double const norm = 1.0 / (1.0 + (k / q) + (k * k));
    double const a0 = -(k * k) * norm;
    double const a1 = 2.0 * a0;
    double const a2 = a0;
    double const b1 = 2.0 * ((k * k) - 1.0) * norm;
    double const b2 = (1.0 - (k / q) + (k * k)) * norm;
    compare_biquad_vs_complex("negative_a0", a0, a1, a2, b1, b2);
}

TEST(cbiquad_match, highpass)
{
    constexpr double sr = 48000.0;
    constexpr double freq = 1000.0;
    constexpr double q = 0.707;
    double const k = std::tan(std::numbers::pi * freq / sr);
    double const norm = 1.0 / (1.0 + (k / q) + (k * k));
    double const a0 = norm;
    double const a1 = -2.0 * a0;
    double const a2 = a0;
    double const b1 = 2.0 * ((k * k) - 1.0) * norm;
    double const b2 = (1.0 - (k / q) + (k * k)) * norm;
    compare_biquad_vs_complex("highpass", a0, a1, a2, b1, b2);
}

TEST(cbiquad_match, bandpass)
{
    constexpr double sr = 48000.0;
    constexpr double freq = 1000.0;
    constexpr double q = 2.0;
    double const k = std::tan(std::numbers::pi * freq / sr);
    double const norm = 1.0 / (1.0 + (k / q) + (k * k));
    double const a0 = (k / q) * norm;
    double const a1 = 0.0;
    double const a2 = -a0;
    double const b1 = 2.0 * ((k * k) - 1.0) * norm;
    double const b2 = (1.0 - (k / q) + (k * k)) * norm;
    compare_biquad_vs_complex("bandpass", a0, a1, a2, b1, b2);
}

TEST(cbiquad_match, notch)
{
    constexpr double sr = 48000.0;
    constexpr double freq = 1000.0;
    constexpr double q = 2.0;
    double const k = std::tan(std::numbers::pi * freq / sr);
    double const norm = 1.0 / (1.0 + (k / q) + (k * k));
    double const a0 = (1.0 + (k * k)) * norm;
    double const a1 = 2.0 * ((k * k) - 1.0) * norm;
    double const a2 = a0;
    double const b1 = a1;
    double const b2 = (1.0 - (k / q) + (k * k)) * norm;
    compare_biquad_vs_complex("notch", a0, a1, a2, b1, b2);
}

TEST(cbiquad_match, peak_boost)
{
    constexpr double sr = 48000.0;
    constexpr double freq = 1000.0;
    constexpr double q = 2.0;
    constexpr double gain_db = 6.0;
    double const k = std::tan(std::numbers::pi * freq / sr);
    double const v = std::pow(10.0, gain_db / 20.0);
    double const norm = 1.0 / (1.0 + (1.0 / q * k) + (k * k));
    double const a0 = (1.0 + (v / q * k) + (k * k)) * norm;
    double const a1 = 2.0 * ((k * k) - 1.0) * norm;
    double const a2 = (1.0 - (v / q * k) + (k * k)) * norm;
    double const b1 = a1;
    double const b2 = (1.0 - (1.0 / q * k) + (k * k)) * norm;
    compare_biquad_vs_complex("peak +6dB", a0, a1, a2, b1, b2);
}

TEST(cbiquad_match, lowshelf_boost)
{
    constexpr double sr = 48000.0;
    constexpr double freq = 200.0;
    constexpr double gain_db = 6.0;
    double const k = std::tan(std::numbers::pi * freq / sr);
    double const v = std::pow(10.0, gain_db / 20.0);
    auto const sqrt2 = std::numbers::sqrt2;
    double const norm = 1.0 / (1.0 + (sqrt2 * k) + (k * k));
    double const a0 = (1.0 + (sqrt2 * v * k) + (v * k * k)) * norm;
    double const a1 = 2.0 * ((v * k * k) - 1.0) * norm;
    double const a2 = (1.0 - (sqrt2 * v * k) + (v * k * k)) * norm;
    double const b1 = 2.0 * ((k * k) - 1.0) * norm;
    double const b2 = (1.0 - (sqrt2 * k) + (k * k)) * norm;
    compare_biquad_vs_complex("lowshelf +6dB", a0, a1, a2, b1, b2);
}

TEST(cbiquad_match, highshelf_boost)
{
    constexpr double sr = 48000.0;
    constexpr double freq = 8000.0;
    constexpr double gain_db = 6.0;
    double const k = std::tan(std::numbers::pi * freq / sr);
    double const v = std::pow(10.0, gain_db / 20.0);
    auto const sqrt2 = std::numbers::sqrt2;
    double const norm = 1.0 / (1.0 + (sqrt2 * k) + (k * k));
    double const a0 = (v + (sqrt2 * v * k) + (k * k)) * norm;
    double const a1 = 2.0 * ((k * k) - v) * norm;
    double const a2 = (v - (sqrt2 * v * k) + (k * k)) * norm;
    double const b1 = 2.0 * ((k * k) - 1) * norm;
    double const b2 = (1.0 - (sqrt2 * k) + (k * k)) * norm;
    compare_biquad_vs_complex("highshelf +6dB", a0, a1, a2, b1, b2);
}

// ============================================================================
// Extreme frequency ratio stability
// ============================================================================

TEST(cbiquad_stability, extreme_ratio_192k_20hz)
{
    ComplexBiQuad<float> filter;
    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / 192000.0, .frequency = 20.0});

    constexpr double sample_rate = 192000.0;
    constexpr double freq = 15.0;
    constexpr double w = 2.0 * std::numbers::pi * freq / sample_rate;
    constexpr int num_samples = static_cast<int>(sample_rate * 10.0);

    float max_output = 0.0f;
    bool any_nan = false;
    bool any_inf = false;

    for (int i = 0; i < num_samples; ++i) {
        float input = static_cast<float>(std::sin(w * i));
        float output = filter(input);
        if (std::isnan(output)) {
            any_nan = true;
            break;
        }
        if (std::isinf(output)) {
            any_inf = true;
            break;
        }
        if (i > 192000) {
            max_output = std::max(max_output, std::abs(output));
        }
    }

    EXPECT_FALSE(any_nan);
    EXPECT_FALSE(any_inf);
    EXPECT_TRUE(max_output > 0.1f);
    EXPECT_TRUE(max_output < 2.0f);
}

TEST(cbiquad_stability, extreme_ratio_rejects_high_freq)
{
    ComplexBiQuad<float> filter;
    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / 192000.0, .frequency = 20.0});

    constexpr double sample_rate = 192000.0;
    constexpr double freq = 1000.0;
    constexpr double w = 2.0 * std::numbers::pi * freq / sample_rate;

    float max_output = 0.0f;
    for (int i = 0; i < 384000; ++i) {
        float input = static_cast<float>(std::sin(w * i));
        float output = filter(input);
        if (i > 192000) {
            max_output = std::max(max_output, std::abs(output));
        }
    }

    EXPECT_TRUE(max_output < 0.01f);
}

// ============================================================================
// All filter types work
// ============================================================================

TEST(cbiquad_filters, lowpass_attenuates_high)
{
    ComplexBiQuad<float> filter;
    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / 48000.0, .frequency = 1000.0});

    constexpr double w = 2.0 * std::numbers::pi * 12000.0 / 48000.0;
    float max_output = 0.0f;
    for (int i = 0; i < 4800; ++i) {
        float output = filter(static_cast<float>(std::sin(w * i)));
        if (i > 480) {
            max_output = std::max(max_output, std::abs(output));
        }
    }

    EXPECT_TRUE(max_output < 0.1f);
}

TEST(cbiquad_filters, highpass_attenuates_low)
{
    ComplexBiQuad<float> filter;
    filter.coeffs.calculate_highpass({.sample_rate_recip = 1.0 / 48000.0, .frequency = 5000.0});

    constexpr double w = 2.0 * std::numbers::pi * 100.0 / 48000.0;
    float max_output = 0.0f;
    for (int i = 0; i < 4800; ++i) {
        float output = filter(static_cast<float>(std::sin(w * i)));
        if (i > 480) {
            max_output = std::max(max_output, std::abs(output));
        }
    }

    EXPECT_TRUE(max_output < 0.05f);
}

TEST(cbiquad_filters, notch_rejects_center)
{
    ComplexBiQuad<float> filter;
    filter.coeffs.calculate_notch({.sample_rate_recip = 1.0 / 48000.0, .frequency = 1000.0, .q = 10.0});

    constexpr double w = 2.0 * std::numbers::pi * 1000.0 / 48000.0;
    float max_output = 0.0f;
    for (int i = 0; i < 9600; ++i) {
        float output = filter(static_cast<float>(std::sin(w * i)));
        if (i > 2400) {
            max_output = std::max(max_output, std::abs(output));
        }
    }

    EXPECT_TRUE(max_output < 0.05f);
}

TEST(cbiquad_filters, bandpass_passes_center)
{
    ComplexBiQuad<float> filter;
    filter.coeffs.calculate_bandpass({.sample_rate_recip = 1.0 / 48000.0, .frequency = 1000.0, .q = 2.0});

    constexpr double w = 2.0 * std::numbers::pi * 1000.0 / 48000.0;
    float max_output = 0.0f;
    for (int i = 0; i < 4800; ++i) {
        float output = filter(static_cast<float>(std::sin(w * i)));
        if (i > 480) {
            max_output = std::max(max_output, std::abs(output));
        }
    }

    EXPECT_TRUE(max_output > 0.5f);
}

TEST(cbiquad_filters, peak_boosts_center)
{
    ComplexBiQuad<float> filter;
    filter.coeffs.calculate_peak({.sample_rate_recip = 1.0 / 48000.0, .frequency = 1000.0, .q = 2.0, .gain_db = 6.0});

    constexpr double w = 2.0 * std::numbers::pi * 1000.0 / 48000.0;
    float max_output = 0.0f;
    for (int i = 0; i < 4800; ++i) {
        float output = filter(static_cast<float>(std::sin(w * i)));
        if (i > 480) {
            max_output = std::max(max_output, std::abs(output));
        }
    }

    EXPECT_TRUE(max_output > 1.5f);
}

TEST(cbiquad_filters, lowshelf_boosts_dc)
{
    ComplexBiQuad<float> filter;
    filter.coeffs.calculate_lowshelf({.sample_rate_recip = 1.0 / 48000.0, .frequency = 200.0, .gain_db = 6.0});

    constexpr double w = 2.0 * std::numbers::pi * 20.0 / 48000.0;
    float max_output = 0.0f;
    for (int i = 0; i < 48000; ++i) {
        float output = filter(static_cast<float>(std::sin(w * i)));
        if (i > 24000) {
            max_output = std::max(max_output, std::abs(output));
        }
    }

    EXPECT_TRUE(max_output > 1.5f);
}

TEST(cbiquad_filters, highshelf_boosts_high)
{
    ComplexBiQuad<float> filter;
    filter.coeffs.calculate_highshelf({.sample_rate_recip = 1.0 / 48000.0, .frequency = 8000.0, .gain_db = 6.0});

    constexpr double w = 2.0 * std::numbers::pi * 20000.0 / 48000.0;
    float max_output = 0.0f;
    for (int i = 0; i < 4800; ++i) {
        float output = filter(static_cast<float>(std::sin(w * i)));
        if (i > 480) {
            max_output = std::max(max_output, std::abs(output));
        }
    }

    EXPECT_TRUE(max_output > 1.5f);
}

// ============================================================================
// SIMD Tests
// ============================================================================

TEST(cbiquad_simd, construction)
{
    ComplexBiQuad<simd_float32x4> filter;
    (void)filter;
}

TEST(cbiquad_simd, bypass_per_channel)
{
    ComplexBiQuad<simd_float32x4> filter;
    for (size_t ch = 0; ch < 4; ++ch) {
        filter.coeffs.set_bypass(ch);
    }

    simd_float32x4 input{1.0f, 2.0f, 3.0f, 4.0f};
    simd_float32x4 output = filter(input);
    EXPECT_TRUE(approx_equal(output[0], 1.0f, 1e-5f));
    EXPECT_TRUE(approx_equal(output[1], 2.0f, 1e-5f));
    EXPECT_TRUE(approx_equal(output[2], 3.0f, 1e-5f));
    EXPECT_TRUE(approx_equal(output[3], 4.0f, 1e-5f));
}

TEST(cbiquad_simd, different_cutoffs_per_channel)
{
    ComplexBiQuad<simd_float32x4> filter;
    constexpr double sr = 48000.0;

    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / sr, .frequency = 500.0, .channel = 0});
    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / sr, .frequency = 2000.0, .channel = 1});
    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / sr, .frequency = 8000.0, .channel = 2});
    filter.coeffs.set_bypass(3);

    constexpr double w = 2.0 * std::numbers::pi * 12000.0 / sr;
    float max_ch[4] = {};

    for (int i = 0; i < 4800; ++i) {
        float sample = static_cast<float>(std::sin(w * i));
        simd_float32x4 input = simd_float32x4::splat(sample);
        simd_float32x4 output = filter(input);
        if (i > 480) {
            for (size_t ch = 0; ch < 4; ++ch) {
                max_ch[ch] = std::max(max_ch[ch], std::abs(output[ch]));
            }
        }
    }

    // Channel 0 (500 Hz LP): heavily attenuate 12 kHz
    EXPECT_TRUE(max_ch[0] < 0.01f);
    // Channel 1 (2 kHz LP): attenuate 12 kHz
    EXPECT_TRUE(max_ch[1] < 0.1f);
    // Channel 2 (8 kHz LP): attenuate somewhat
    EXPECT_TRUE(max_ch[2] < 0.5f);
    // Channel 3 (bypass): pass through
    EXPECT_TRUE(max_ch[3] > 0.9f);
}

// ============================================================================
// State reset
// ============================================================================

TEST(cbiquad_state, reset_clears_history)
{
    ComplexBiQuad<double> filter;
    filter.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / 48000.0, .frequency = 1000.0});

    for (int i = 0; i < 100; ++i) {
        (void)filter(static_cast<double>(i) * 0.1);
    }

    // State should be nonzero
    EXPECT_TRUE(std::abs(filter.state.stage1.re) > 1e-10);

    filter.reset();
    EXPECT_TRUE(approx_equal(filter.state.stage1.re, 0.0, 1e-15));
    EXPECT_TRUE(approx_equal(filter.state.stage1.im, 0.0, 1e-15));
    EXPECT_TRUE(approx_equal(filter.state.stage2.re, 0.0, 1e-15));
    EXPECT_TRUE(approx_equal(filter.state.stage2.im, 0.0, 1e-15));
}

// ============================================================================
// f32 complex vs f64 standard biquad
// ============================================================================

TEST(cbiquad_precision, f32_complex_vs_f64_standard)
{
    BiQuad<double> bq;
    bq.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / 48000.0, .frequency = 1000.0});

    ComplexBiQuad<float> cbq;
    cbq.coeffs.calculate_lowpass({.sample_rate_recip = 1.0 / 48000.0, .frequency = 1000.0});

    constexpr double w = 2.0 * std::numbers::pi * 500.0 / 48000.0;
    double max_err = 0.0;

    for (int i = 0; i < 4800; ++i) {
        double sample = std::sin(w * i);
        double out_f64 = bq(sample);
        float out_f32 = cbq(static_cast<float>(sample));
        double err = std::abs(out_f64 - static_cast<double>(out_f32));
        if (i > 100) {
            max_err = std::max(max_err, err);
        }
    }

    EXPECT_TRUE(max_err < 5e-3);
}

// Main test runner function required by create_test_sourcelist
TEST_MAIN(statusbar_dsp, dsp_complex_biquad_test)
