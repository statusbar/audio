#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/dsp/dsp_complex_biquad.hpp"
#include "statusbar/dsp/dsp_constants.hpp"
#include "statusbar/dsp/dsp_vec_base.hpp"
#include "statusbar/engine/engine_segment.hpp"

#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>

namespace statusbar::engine {

// Coefficient pair for a biquad in pole/zero (cascaded first-order) form
// — the runtime ramping representation Tier 0 consumes.
//
// Templated on the audio sample type T so a single element type can
// represent multiple parallel channels:
//
//   BiquadComplexCoeffs<float>             — one channel of biquad
//   BiquadComplexCoeffs<simd_float32x4>    — four parallel channels
//   BiquadComplexCoeffs<simd_float32x8>    — eight parallel channels
//
// For SIMD T, each coefficient field holds N independent values (one per
// SIMD lane / channel). Pointwise arithmetic operators map naturally:
// `a + b` adds lane-wise, `a * s` (s a scalar) broadcasts and multiplies.
//
// Pole/zero form is preferred over standard biquad TDF II coefficients
// for ramping because:
//   - The stable region (open unit disk) is convex, so a linear chord
//     between two stable poles stays stable along its entire length.
//   - Mid-chord intermediate filters look like "real filters at
//     intermediate parameters" rather than the somewhat unintuitive
//     responses you get when interpolating TDF II coefficients.

template <typename T = float>
struct BiquadComplexCoeffs
{
    statusbar::dsp::ComplexFirstOrderCoeffs<T> stage1{};
    statusbar::dsp::ComplexFirstOrderCoeffs<T> stage2{};

    // Identity (bypass) coefficients: H(z) = 1. Each first-order stage gets
    // a0_re = 1 with every other term 0, so the cascade passes signal through
    // unchanged. Default-constructed coeffs are all-zero — i.e. silence — so
    // prime an element with identity() when it should start transparent.
    [[nodiscard]] static auto identity() noexcept -> BiquadComplexCoeffs
    {
        BiquadComplexCoeffs r{};
        r.stage1.a0_re = statusbar::dsp::constants::one<T>();
        r.stage2.a0_re = statusbar::dsp::constants::one<T>();
        return r;
    }
};

template <typename T>
constexpr auto operator+(BiquadComplexCoeffs<T> const& a, BiquadComplexCoeffs<T> const& b) noexcept -> BiquadComplexCoeffs<T>
{
    BiquadComplexCoeffs<T> r{};
    r.stage1.a0_re = a.stage1.a0_re + b.stage1.a0_re;
    r.stage1.a0_im = a.stage1.a0_im + b.stage1.a0_im;
    r.stage1.a1_re = a.stage1.a1_re + b.stage1.a1_re;
    r.stage1.a1_im = a.stage1.a1_im + b.stage1.a1_im;
    r.stage1.b1_re = a.stage1.b1_re + b.stage1.b1_re;
    r.stage1.b1_im = a.stage1.b1_im + b.stage1.b1_im;
    r.stage2.a0_re = a.stage2.a0_re + b.stage2.a0_re;
    r.stage2.a0_im = a.stage2.a0_im + b.stage2.a0_im;
    r.stage2.a1_re = a.stage2.a1_re + b.stage2.a1_re;
    r.stage2.a1_im = a.stage2.a1_im + b.stage2.a1_im;
    r.stage2.b1_re = a.stage2.b1_re + b.stage2.b1_re;
    r.stage2.b1_im = a.stage2.b1_im + b.stage2.b1_im;
    return r;
}

template <typename T>
constexpr auto operator-(BiquadComplexCoeffs<T> const& a, BiquadComplexCoeffs<T> const& b) noexcept -> BiquadComplexCoeffs<T>
{
    BiquadComplexCoeffs<T> r{};
    r.stage1.a0_re = a.stage1.a0_re - b.stage1.a0_re;
    r.stage1.a0_im = a.stage1.a0_im - b.stage1.a0_im;
    r.stage1.a1_re = a.stage1.a1_re - b.stage1.a1_re;
    r.stage1.a1_im = a.stage1.a1_im - b.stage1.a1_im;
    r.stage1.b1_re = a.stage1.b1_re - b.stage1.b1_re;
    r.stage1.b1_im = a.stage1.b1_im - b.stage1.b1_im;
    r.stage2.a0_re = a.stage2.a0_re - b.stage2.a0_re;
    r.stage2.a0_im = a.stage2.a0_im - b.stage2.a0_im;
    r.stage2.a1_re = a.stage2.a1_re - b.stage2.a1_re;
    r.stage2.a1_im = a.stage2.a1_im - b.stage2.a1_im;
    r.stage2.b1_re = a.stage2.b1_re - b.stage2.b1_re;
    r.stage2.b1_im = a.stage2.b1_im - b.stage2.b1_im;
    return r;
}

template <typename T>
constexpr auto operator*(BiquadComplexCoeffs<T> const& a, float s) noexcept -> BiquadComplexCoeffs<T>
{
    // dsp::mul recurses through any SIMDVec nesting depth so this works
    // for T = float, simd_float32x4, SIMDVec<simd_float32x4, 2>, etc.
    using statusbar::dsp::mul;
    BiquadComplexCoeffs<T> r{};
    r.stage1.a0_re = mul(a.stage1.a0_re, s);
    r.stage1.a0_im = mul(a.stage1.a0_im, s);
    r.stage1.a1_re = mul(a.stage1.a1_re, s);
    r.stage1.a1_im = mul(a.stage1.a1_im, s);
    r.stage1.b1_re = mul(a.stage1.b1_re, s);
    r.stage1.b1_im = mul(a.stage1.b1_im, s);
    r.stage2.a0_re = mul(a.stage2.a0_re, s);
    r.stage2.a0_im = mul(a.stage2.a0_im, s);
    r.stage2.a1_re = mul(a.stage2.a1_re, s);
    r.stage2.a1_im = mul(a.stage2.a1_im, s);
    r.stage2.b1_re = mul(a.stage2.b1_re, s);
    r.stage2.b1_im = mul(a.stage2.b1_im, s);
    return r;
}

static_assert(SegmentableCoeffs<BiquadComplexCoeffs<float>>);

// ─────────────────────────────────────────────────────────────────────────────
// Frequency-response evaluation (Bode plot points)
//
// These free functions let a GUI sample the filter's transfer function at
// arbitrary z-domain points, or convert from audible frequency to the
// corresponding z⁻¹ on the unit circle. Evaluation is done in double
// precision regardless of T so plots aren't quantized to the runtime
// float coefficients' precision.
//
// For SIMD T, the `channel` parameter selects which lane's coefficients
// to evaluate — a SIMD biquad with 4 independent per-channel filters
// gives 4 independent Bode responses.
// ─────────────────────────────────────────────────────────────────────────────

// Polar-form frequency response point: linear magnitude + phase (radians).
// Convert magnitude to dB via dsp::amplitude_to_db if needed.
struct BodePoint
{
    double magnitude{};  // linear gain |H(z)|
    double phase{};      // arg(H(z)) in radians, in (-π, π]
};

// z⁻¹ = e⁻ʲω at the given audio frequency. ω = 2π · f / fs.
[[nodiscard]] inline auto z_inv_at_frequency(double freq_hz, double sample_rate_recip) noexcept -> std::complex<double>
{
    double const omega = 2.0 * std::numbers::pi * freq_hz * sample_rate_recip;
    return std::complex<double>{std::cos(omega), -std::sin(omega)};
}

// Evaluate one complex first-order section at the given z⁻¹.
// H_stage(z) = (a0 + a1·z⁻¹) / (1 + b1·z⁻¹)
template <typename T>
[[nodiscard]] inline auto evaluate_z_domain(
    statusbar::dsp::ComplexFirstOrderCoeffs<T> const& stage, std::complex<double> const& z_inv, size_t channel = 0) noexcept
    -> std::complex<double>
{
    using statusbar::dsp::get_flattened_item;
    std::complex<double> const a0{
        static_cast<double>(get_flattened_item(stage.a0_re, channel)),
        static_cast<double>(get_flattened_item(stage.a0_im, channel))};
    std::complex<double> const a1{
        static_cast<double>(get_flattened_item(stage.a1_re, channel)),
        static_cast<double>(get_flattened_item(stage.a1_im, channel))};
    std::complex<double> const b1{
        static_cast<double>(get_flattened_item(stage.b1_re, channel)),
        static_cast<double>(get_flattened_item(stage.b1_im, channel))};
    std::complex<double> const numer = a0 + (a1 * z_inv);
    std::complex<double> const denom = std::complex<double>{1.0, 0.0} + (b1 * z_inv);
    return numer / denom;
}

// Evaluate a biquad (cascaded pair of first-order stages) at z⁻¹.
// H_biquad(z) = H_stage1(z) · H_stage2(z)
template <typename T>
[[nodiscard]] inline auto evaluate_z_domain(
    BiquadComplexCoeffs<T> const& coeffs, std::complex<double> const& z_inv, size_t channel = 0) noexcept -> std::complex<double>
{
    auto const h1 = evaluate_z_domain(coeffs.stage1, z_inv, channel);
    auto const h2 = evaluate_z_domain(coeffs.stage2, z_inv, channel);
    return h1 * h2;
}

// Convenience: evaluate at an audible frequency, return polar (magnitude
// + phase) form.
template <typename T>
[[nodiscard]] inline auto bode_point_at_frequency(
    BiquadComplexCoeffs<T> const& coeffs, double freq_hz, double sample_rate_recip, size_t channel = 0) noexcept -> BodePoint
{
    auto const H = evaluate_z_domain(coeffs, z_inv_at_frequency(freq_hz, sample_rate_recip), channel);
    return BodePoint{.magnitude = std::abs(H), .phase = std::arg(H)};
}

}  // namespace statusbar::engine
