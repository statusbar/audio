#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <numbers>
#include <span>

namespace statusbar::dsp {

//
// Constexpr Rounding Functions
//
// std::lround is not constexpr in some standard library implementations (e.g., macOS SDK).
// These provide constexpr-compatible rounding with correct behavior for negative numbers.

/// Constexpr round to nearest integer (double), ties away from zero (like std::lround)
/// @param x Value to round
/// @return Rounded value as int64_t
[[nodiscard]] constexpr auto lround(double x) noexcept -> int64_t
{
    // For positive: floor(x + 0.5), for negative: ceil(x - 0.5)
    // This matches std::lround behavior (round half away from zero)
    return static_cast<int64_t>(x >= 0.0 ? x + 0.5 : x - 0.5);
}

/// Constexpr round to nearest integer (float), ties away from zero (like std::lround)
/// @param x Value to round
/// @return Rounded value as int64_t
[[nodiscard]] constexpr auto lround(float x) noexcept -> int64_t
{
    return static_cast<int64_t>(x >= 0.0F ? x + 0.5F : x - 0.5F);
}

// Compile-time verification of lround correctness
static_assert(lround(0.0) == 0, "lround(0.0) should be 0");
static_assert(lround(1.0) == 1, "lround(1.0) should be 1");
static_assert(lround(-1.0) == -1, "lround(-1.0) should be -1");
static_assert(lround(1.4) == 1, "lround(1.4) should round down to 1");
static_assert(lround(1.5) == 2, "lround(1.5) should round up to 2 (ties away from zero)");
static_assert(lround(1.6) == 2, "lround(1.6) should round up to 2");
static_assert(lround(-1.4) == -1, "lround(-1.4) should round toward zero to -1");
static_assert(lround(-1.5) == -2, "lround(-1.5) should round away from zero to -2");
static_assert(lround(-1.6) == -2, "lround(-1.6) should round away from zero to -2");
static_assert(lround(2.5) == 3, "lround(2.5) should round to 3");
static_assert(lround(-2.5) == -3, "lround(-2.5) should round to -3");

//
// Floating Point Comparison
//

/// Check approximate floating-point equality (double)
[[nodiscard]] constexpr auto approx_equal(double a, double b, double epsilon = 1e-6) noexcept -> bool
{
    return std::abs(a - b) < epsilon;
}

/// Check approximate floating-point equality (float)
[[nodiscard]] constexpr auto approx_equal(float a, float b, float epsilon = 1e-6F) noexcept -> bool
{
    return std::abs(a - b) < epsilon;
}

/// Generate a sine wave signal into provided output span
inline void generate_sine(std::span<double> output, double freq, double sample_rate) noexcept
{
    double const phase_inc = 2.0 * std::numbers::pi * freq / sample_rate;
    for (size_t i = 0; i < output.size(); ++i) {
        output[i] = std::sin(static_cast<double>(i) * phase_inc);
    }
}

/// Generate a sine wave signal into provided output span (float version)
inline void generate_sine(std::span<float> output, float freq, float sample_rate) noexcept
{
    float const phase_inc = 2.0F * std::numbers::pi_v<float> * freq / sample_rate;
    for (size_t i = 0; i < output.size(); ++i) {
        output[i] = std::sin(static_cast<float>(i) * phase_inc);
    }
}

/// Calculate RMS (Root Mean Square) of a signal
[[nodiscard]] auto calculate_rms(std::span<double const> signal) noexcept -> double;

/// Calculate RMS (Root Mean Square) of a signal (float version)
[[nodiscard]] auto calculate_rms(std::span<float const> signal) noexcept -> float;

/// Count zero crossings in a signal (useful for frequency measurement)
[[nodiscard]] auto count_zero_crossings(std::span<double const> signal) noexcept -> size_t;

/// Count zero crossings in a signal (float version)
[[nodiscard]] auto count_zero_crossings(std::span<float const> signal) noexcept -> size_t;

}  // namespace statusbar::dsp
