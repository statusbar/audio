#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/dsp/dsp_constants.hpp"

#include <cmath>
#include <cstddef>

namespace statusbar::dsp {

/// Parameters for biquad filter coefficient calculation
///
/// Uses designated initializers for self-documenting call sites:
/// @code
/// filter.coeffs.calculate_lowpass({
///     .sample_rate_recip = 1.0 / 48000.0,
///     .frequency = 1000.0,
///     .q = 0.707
/// });
/// @endcode
template <typename T = double>
struct FilterParams
{
    T sample_rate_recip;  ///< Reciprocal of sample rate (1.0 / sample_rate)
    T frequency;          ///< Filter frequency in Hz
    T q{0.707};           ///< Quality factor (0.707 for Butterworth, higher for resonance)
    T gain_db{0.0};       ///< Gain in dB (only for peak/shelf filters)
    size_t channel{0};    ///< Channel index for multi-channel (SIMD) filters
};

/// Standard biquad coefficients in double precision.
///
/// Transfer function: H(z) = (a0 + a1*z^-1 + a2*z^-2) / (1 + b1*z^-1 + b2*z^-2)
struct BiquadCoeffsF64
{
    double a0, a1, a2, b1, b2;
};

// ---------------------------------------------------------------------------
// Filter design functions
// ---------------------------------------------------------------------------
// Each function computes the five standard biquad coefficients from FilterParams.
// These are pure functions with no dependency on the filter implementation.

/// Minimum Q value for filter design functions.
/// Values below this are clamped to prevent division by zero and numerical
/// instability. 0.01 is extremely overdamped but produces valid coefficients
/// for all filter types.
constexpr double min_q = 0.01;

/// Clamp Q to the minimum usable value
constexpr auto clamp_q(double q) noexcept -> double
{
    return q < min_q ? min_q : q;
}

/// Bilinear-transform prewarp: k = tan(pi * f/fs), with the normalized
/// frequency f/fs clamped to the open interval (0, 0.5). Without the clamp,
/// f at/above Nyquist drives tan() to infinity and f = 0 puts double poles on
/// the unit circle — both yield inf/NaN coefficients.
inline auto prewarp_k(FilterParams<> const& params) noexcept -> double
{
    constexpr double min_f_norm = 1e-6;
    constexpr double max_f_norm = 0.5 - 1e-6;
    double f_norm = params.frequency * params.sample_rate_recip;
    f_norm = f_norm < min_f_norm ? min_f_norm : (f_norm > max_f_norm ? max_f_norm : f_norm);
    return std::tan(constants::pi() * f_norm);
}

/// 2nd-order Butterworth lowpass.
/// Passes frequencies below cutoff, -3 dB at cutoff, 12 dB/octave rolloff.
inline auto design_lowpass(FilterParams<> const& params) -> BiquadCoeffsF64
{
    double const q = clamp_q(params.q);
    double const k = prewarp_k(params);
    double const norm = 1.0 / (1.0 + (k / q) + (k * k));
    double const a0 = (k * k) * norm;
    return {.a0 = a0, .a1 = 2.0 * a0, .a2 = a0, .b1 = 2.0 * ((k * k) - 1.0) * norm, .b2 = (1.0 - (k / q) + (k * k)) * norm};
}

/// 2nd-order Butterworth highpass.
/// Attenuates below cutoff, -3 dB at cutoff, 12 dB/octave rolloff.
inline auto design_highpass(FilterParams<> const& params) -> BiquadCoeffsF64
{
    double const q = clamp_q(params.q);
    double const k = prewarp_k(params);
    double const norm = 1.0 / (1.0 + (k / q) + (k * k));
    double const a0 = norm;
    return {.a0 = a0, .a1 = -2.0 * a0, .a2 = a0, .b1 = 2.0 * ((k * k) - 1.0) * norm, .b2 = (1.0 - (k / q) + (k * k)) * norm};
}

/// 2nd-order bandpass. Bandwidth = center_freq / Q.
inline auto design_bandpass(FilterParams<> const& params) -> BiquadCoeffsF64
{
    double const q = clamp_q(params.q);
    double const k = prewarp_k(params);
    double const norm = 1.0 / (1.0 + (k / q) + (k * k));
    double const a0 = (k / q) * norm;
    return {.a0 = a0, .a1 = 0.0, .a2 = -a0, .b1 = 2.0 * ((k * k) - 1.0) * norm, .b2 = (1.0 - (k / q) + (k * k)) * norm};
}

/// 2nd-order notch (band-reject). Unity gain at DC and Nyquist.
inline auto design_notch(FilterParams<> const& params) -> BiquadCoeffsF64
{
    double const q = clamp_q(params.q);
    double const k = prewarp_k(params);
    double const norm = 1.0 / (1.0 + (k / q) + (k * k));
    double const a0 = (1.0 + (k * k)) * norm;
    double const a1 = 2.0 * ((k * k) - 1.0) * norm;
    return {.a0 = a0, .a1 = a1, .a2 = a0, .b1 = a1, .b2 = (1.0 - (k / q) + (k * k)) * norm};
}

/// 2nd-order parametric peak/dip (bell) EQ.
/// Boosts or cuts around center frequency. Unity gain at DC and Nyquist.
inline auto design_peak(FilterParams<> const& params) -> BiquadCoeffsF64
{
    double const q = clamp_q(params.q);
    double const k = prewarp_k(params);
    double const v = std::pow(10.0, std::abs(params.gain_db) / 20.0);

    if (params.gain_db >= 0) {
        double const norm = 1.0 / (1.0 + (1.0 / q * k) + (k * k));
        double const a1 = 2.0 * ((k * k) - 1.0) * norm;
        return {
            .a0 = (1.0 + (v / q * k) + (k * k)) * norm,
            .a1 = a1,
            .a2 = (1.0 - (v / q * k) + (k * k)) * norm,
            .b1 = a1,
            .b2 = (1.0 - (1.0 / q * k) + (k * k)) * norm};
    }
    double const norm = 1.0 / (1.0 + (v / q * k) + (k * k));
    double const a1 = 2.0 * ((k * k) - 1.0) * norm;
    return {
        .a0 = (1.0 + (1.0 / q * k) + (k * k)) * norm,
        .a1 = a1,
        .a2 = (1.0 - (1.0 / q * k) + (k * k)) * norm,
        .b1 = a1,
        .b2 = (1.0 - (v / q * k) + (k * k)) * norm};
}

/// 2nd-order low shelf. Boosts/cuts below shelf frequency, unity above.
/// @note Q parameter is ignored (fixed sqrt(2) slope).
inline auto design_lowshelf(FilterParams<> const& params) -> BiquadCoeffsF64
{
    double const k = prewarp_k(params);
    double const v = std::pow(10.0, std::abs(params.gain_db) / 20.0);
    auto const sqrt2 = constants::sqrt_2<double>();

    if (params.gain_db >= 0) {
        double const norm = 1.0 / (1.0 + (sqrt2 * k) + (k * k));
        return {
            .a0 = (1.0 + (sqrt2 * v * k) + (v * k * k)) * norm,
            .a1 = 2.0 * ((v * k * k) - 1.0) * norm,
            .a2 = (1.0 - (sqrt2 * v * k) + (v * k * k)) * norm,
            .b1 = 2.0 * ((k * k) - 1.0) * norm,
            .b2 = (1.0 - (sqrt2 * k) + (k * k)) * norm};
    }
    double const norm = 1.0 / (1.0 + (sqrt2 * v * k) + (v * k * k));
    return {
        .a0 = (1.0 + (sqrt2 * k) + (k * k)) * norm,
        .a1 = 2.0 * ((k * k) - 1.0) * norm,
        .a2 = (1.0 - (sqrt2 * k) + (k * k)) * norm,
        .b1 = 2.0 * ((v * k * k) - 1.0) * norm,
        .b2 = (1.0 - (sqrt2 * v * k) + (v * k * k)) * norm};
}

/// 2nd-order high shelf. Boosts/cuts above shelf frequency, unity below.
/// @note Q parameter is ignored (fixed sqrt(2) slope).
inline auto design_highshelf(FilterParams<> const& params) -> BiquadCoeffsF64
{
    double const k = prewarp_k(params);
    double const v = std::pow(10.0, std::abs(params.gain_db) / 20.0);
    auto const sqrt2 = constants::sqrt_2<double>();

    if (params.gain_db >= 0) {
        double const norm = 1.0 / (1.0 + (sqrt2 * k) + (k * k));
        return {
            .a0 = (v + (sqrt2 * v * k) + (k * k)) * norm,
            .a1 = 2.0 * ((k * k) - v) * norm,
            .a2 = (v - (sqrt2 * v * k) + (k * k)) * norm,
            .b1 = 2.0 * ((k * k) - 1) * norm,
            .b2 = (1.0 - (sqrt2 * k) + (k * k)) * norm};
    }
    double const norm = 1.0 / (v + (sqrt2 * v * k) + (k * k));
    return {
        .a0 = (1.0 + (sqrt2 * k) + (k * k)) * norm,
        .a1 = 2.0 * ((k * k) - 1) * norm,
        .a2 = (1.0 - (sqrt2 * k) + (k * k)) * norm,
        .b1 = 2.0 * ((k * k) - v) * norm,
        .b2 = (v - (sqrt2 * v * k) + (k * k)) * norm};
}

}  // namespace statusbar::dsp
