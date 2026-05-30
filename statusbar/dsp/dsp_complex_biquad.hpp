#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/dsp/dsp_filter_design.hpp"
#include "statusbar/dsp/dsp_vec.hpp"

#include <complex>
#include <cstddef>
#include <type_traits>

namespace statusbar::dsp {

/// Coefficients for a single complex first-order section (transposed direct form II).
///
/// Transfer function: H(z) = (a0 + a1*z^-1) / (1 + b1*z^-1)
/// where a0, a1, b1 are complex. Real and imaginary parts stored separately.
template <typename T>
struct ComplexFirstOrderCoeffs
{
    T a0_re{}, a0_im{};  ///< Feedforward a0: real, imaginary
    T a1_re{}, a1_im{};  ///< Feedforward a1 (z^-1 term): real, imaginary
    T b1_re{}, b1_im{};  ///< Feedback b1 (z^-1 term): real, imaginary
};

/// State for a single complex first-order section.
template <typename T>
struct ComplexFirstOrderState
{
    T re, im;

    ComplexFirstOrderState() noexcept
    {
        zero(re);
        zero(im);
    }

    ComplexFirstOrderState(ComplexFirstOrderState const&) = default;
    ComplexFirstOrderState(ComplexFirstOrderState&&) = default;
    auto operator=(ComplexFirstOrderState const&) -> ComplexFirstOrderState& = default;
    auto operator=(ComplexFirstOrderState&&) -> ComplexFirstOrderState& = default;
    ~ComplexFirstOrderState() = default;

    void reset() noexcept
    {
        zero(re);
        zero(im);
    }
};

// ---------------------------------------------------------------------------
// Coefficient factorization helpers (f64 arithmetic)
// ---------------------------------------------------------------------------

namespace detail {

/// Result of factoring a biquad into two first-order stages.
struct TwoStageCoeffsF64
{
    // Stage 1
    std::complex<double> s1_a0;
    std::complex<double> s1_a1;
    std::complex<double> s1_b1;
    // Stage 2
    std::complex<double> s2_a0;
    std::complex<double> s2_a1;
    std::complex<double> s2_b1;
};

/// Factor roots of a monic quadratic z^-2 + c1*z^-1 + c2.
struct QuadRoots
{
    std::complex<double> r1, r2;
};

inline auto factor_quadratic(double const c1, double const c2) -> QuadRoots
{
    double const disc = (c1 * c1) - (4.0 * c2);
    if (disc < 0.0) {
        // Complex conjugate roots
        std::complex<double> const r{-c1 / 2.0, std::sqrt(-disc) / 2.0};
        return {.r1 = r, .r2 = std::conj(r)};
    }
    // Two real roots
    double const sd = std::sqrt(disc);
    return {.r1 = {(-c1 + sd) / 2.0}, .r2 = {(-c1 - sd) / 2.0}};
}

/// Factor a biquad H(z) = (a0 + a1*z^-1 + a2*z^-2) / (1 + b1*z^-1 + b2*z^-2)
/// into two first-order stages.
inline auto factor_biquad_to_two_stages(double const a0, double const a1, double const a2, double const b1, double const b2)
    -> TwoStageCoeffsF64
{
    // Factor denominator: 1 + b1*z^-1 + b2*z^-2
    auto const poles = factor_quadratic(b1, b2);

    // Factor numerator: a0*(1 + (a1/a0)*z^-1 + (a2/a0)*z^-2)
    QuadRoots zeros{};
    if (std::abs(a0) < 1e-30) {
        zeros = {};
    } else {
        double const na1 = a1 / a0;
        double const na2 = a2 / a0;
        zeros = factor_quadratic(na1, na2);
    }

    // Split gain: g = sqrt(a0), so g*g = a0
    //
    // Stage k: H_k(z) = g * (1 - z_k*z^-1) / (1 - p_k*z^-1)
    //        = (g - g*z_k*z^-1) / (1 + (-p_k)*z^-1)
    //
    // In TDFII: c_a0 = g, c_a1 = -g*z_k, c_b1 = -p_k
    double const g = std::sqrt(a0);

    return TwoStageCoeffsF64{
        .s1_a0 = {g, 0.0},
        .s1_a1 = -g * zeros.r1,
        .s1_b1 = -poles.r1,
        .s2_a0 = {g, 0.0},
        .s2_a1 = -g * zeros.r2,
        .s2_b1 = -poles.r2,
    };
}

}  // namespace detail

// ---------------------------------------------------------------------------
// ComplexBiQuad
// ---------------------------------------------------------------------------

/// Complex biquad filter: decomposes a standard biquad into two cascaded
/// first-order stages for improved numerical stability at extreme frequency
/// ratios (high sample rate + low cutoff).
///
/// Uses transposed direct form II per complex first-order section.
template <typename T>
struct ComplexBiQuad
{
    using value_type = T;
    using item_type = simd_flattened_type<T>::type;

    static constexpr size_t vector_size = simd_size<T>::value;
    static constexpr size_t flattened_size = simd_flattened_size<T>::value;

    struct Coeffs
    {
        ComplexFirstOrderCoeffs<T> stage1;
        ComplexFirstOrderCoeffs<T> stage2;

        /// Set coefficients for a single channel from f64 biquad coefficients.
        void set_from_biquad_coeffs(
            double const a0, double const a1, double const a2, double const b1, double const b2, size_t const channel = 0)
        {
            auto const s = detail::factor_biquad_to_two_stages(a0, a1, a2, b1, b2);

            set_flattened_item(stage1.a0_re, static_cast<item_type>(s.s1_a0.real()), channel);
            set_flattened_item(stage1.a0_im, static_cast<item_type>(s.s1_a0.imag()), channel);
            set_flattened_item(stage1.a1_re, static_cast<item_type>(s.s1_a1.real()), channel);
            set_flattened_item(stage1.a1_im, static_cast<item_type>(s.s1_a1.imag()), channel);
            set_flattened_item(stage1.b1_re, static_cast<item_type>(s.s1_b1.real()), channel);
            set_flattened_item(stage1.b1_im, static_cast<item_type>(s.s1_b1.imag()), channel);

            set_flattened_item(stage2.a0_re, static_cast<item_type>(s.s2_a0.real()), channel);
            set_flattened_item(stage2.a0_im, static_cast<item_type>(s.s2_a0.imag()), channel);
            set_flattened_item(stage2.a1_re, static_cast<item_type>(s.s2_a1.real()), channel);
            set_flattened_item(stage2.a1_im, static_cast<item_type>(s.s2_a1.imag()), channel);
            set_flattened_item(stage2.b1_re, static_cast<item_type>(s.s2_b1.real()), channel);
            set_flattened_item(stage2.b1_im, static_cast<item_type>(s.s2_b1.imag()), channel);
        }

        /// Set filter to bypass mode (unity gain, no filtering)
        void set_bypass(size_t const channel = 0) { set_from_biquad_coeffs(1.0, 0.0, 0.0, 0.0, 0.0, channel); }

        void calculate_lowpass(FilterParams<> const& params)
        {
            auto const c = design_lowpass(params);
            set_from_biquad_coeffs(c.a0, c.a1, c.a2, c.b1, c.b2, params.channel);
        }

        void calculate_highpass(FilterParams<> const& params)
        {
            auto const c = design_highpass(params);
            set_from_biquad_coeffs(c.a0, c.a1, c.a2, c.b1, c.b2, params.channel);
        }

        void calculate_bandpass(FilterParams<> const& params)
        {
            auto const c = design_bandpass(params);
            set_from_biquad_coeffs(c.a0, c.a1, c.a2, c.b1, c.b2, params.channel);
        }

        void calculate_notch(FilterParams<> const& params)
        {
            auto const c = design_notch(params);
            set_from_biquad_coeffs(c.a0, c.a1, c.a2, c.b1, c.b2, params.channel);
        }

        void calculate_peak(FilterParams<> const& params)
        {
            auto const c = design_peak(params);
            set_from_biquad_coeffs(c.a0, c.a1, c.a2, c.b1, c.b2, params.channel);
        }

        void calculate_lowshelf(FilterParams<> const& params)
        {
            auto const c = design_lowshelf(params);
            set_from_biquad_coeffs(c.a0, c.a1, c.a2, c.b1, c.b2, params.channel);
        }

        void calculate_highshelf(FilterParams<> const& params)
        {
            auto const c = design_highshelf(params);
            set_from_biquad_coeffs(c.a0, c.a1, c.a2, c.b1, c.b2, params.channel);
        }
    };

    struct State
    {
        ComplexFirstOrderState<T> stage1;
        ComplexFirstOrderState<T> stage2;

        State() noexcept = default;
        State(State const&) = default;
        State(State&&) = default;
        auto operator=(State const&) -> State& = default;
        auto operator=(State&&) -> State& = default;
        ~State() = default;

        void reset() noexcept
        {
            stage1.reset();
            stage2.reset();
        }
    };

    Coeffs coeffs;
    State state;

    ComplexBiQuad() = default;
    ComplexBiQuad(ComplexBiQuad const&) = default;
    auto operator=(ComplexBiQuad const&) -> ComplexBiQuad& = default;
    ComplexBiQuad(ComplexBiQuad&&) = default;
    auto operator=(ComplexBiQuad&&) -> ComplexBiQuad& = default;
    ~ComplexBiQuad() = default;

    /// Reset filter state to zero (clears delay lines)
    void reset() noexcept { state.reset(); }

    /// Process a single sample through both cascaded stages.
    [[nodiscard]] auto operator()(T const& input) noexcept -> T
    {
        auto const& c1 = coeffs.stage1;
        auto const& c2 = coeffs.stage2;

        // Stage 1: real input -> complex output
        // y = a0 * x + s  (complex a0, real x)
        T const y1_re = (c1.a0_re * input) + state.stage1.re;
        T const y1_im = (c1.a0_im * input) + state.stage1.im;

        // s_next = a1 * x - b1 * y  (complex multiply)
        state.stage1.re = (c1.a1_re * input) - (c1.b1_re * y1_re) + (c1.b1_im * y1_im);
        state.stage1.im = (c1.a1_im * input) - (c1.b1_re * y1_im) - (c1.b1_im * y1_re);

        // Stage 2: complex input (y1_re, y1_im) -> real output
        // out = a0_s2 * y1 + s2  (full complex multiply)
        T const out_re = (c2.a0_re * y1_re) - (c2.a0_im * y1_im) + state.stage2.re;
        T const out_im = (c2.a0_re * y1_im) + (c2.a0_im * y1_re) + state.stage2.im;

        // s2_next = a1_s2 * y1 - b1_s2 * out  (complex multiplies)
        state.stage2.re = (c2.a1_re * y1_re) - (c2.a1_im * y1_im) - (c2.b1_re * out_re) + (c2.b1_im * out_im);
        state.stage2.im = (c2.a1_re * y1_im) + (c2.a1_im * y1_re) - (c2.b1_re * out_im) - (c2.b1_im * out_re);

        (void)out_im;  // Should be ~0 for real input
        return out_re;
    }
};

}  // namespace statusbar::dsp
