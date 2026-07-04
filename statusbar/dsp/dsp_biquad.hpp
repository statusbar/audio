#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/dsp/dsp_filter_design.hpp"
#include "statusbar/dsp/dsp_vec.hpp"

#include <cstddef>
#include <span>
#include <type_traits>

namespace statusbar::dsp {

template <typename T>
struct BiQuad
{
    using value_type = T;
    using item_type = simd_flattened_type<T>::type;

    static constexpr size_t vector_size = simd_size<T>::value;
    static constexpr size_t flattened_size = simd_flattened_size<T>::value;

    struct Coeffs
    {
        T a0{}, a1{}, a2{}, b1{}, b2{};

        /// Set filter coefficients directly
        ///
        /// @param na0 Numerator coefficient a0 (feedforward)
        /// @param na1 Numerator coefficient a1 (feedforward, z^-1 term)
        /// @param na2 Numerator coefficient a2 (feedforward, z^-2 term)
        /// @param nb1 Denominator coefficient b1 (feedback, z^-1 term)
        /// @param nb2 Denominator coefficient b2 (feedback, z^-2 term)
        /// @param channel Channel index for multi-channel (SIMD) filters (default: 0)
        void set(double const na0, double const na1, double const na2, double const nb1, double const nb2, size_t const channel = 0)
        {
            set_flattened_item(a0, static_cast<item_type>(na0), channel);
            set_flattened_item(a1, static_cast<item_type>(na1), channel);
            set_flattened_item(a2, static_cast<item_type>(na2), channel);
            set_flattened_item(b1, static_cast<item_type>(nb1), channel);
            set_flattened_item(b2, static_cast<item_type>(nb2), channel);
        }

        /// Set filter to bypass mode (unity gain, no filtering)
        ///
        /// @param channel Channel index for multi-channel (SIMD) filters (default: 0)
        void set_bypass(size_t const channel = 0) { set(1.0, 0.0, 0.0, 0.0, 0.0, channel); }

        void calculate_lowpass(FilterParams<> const& params)
        {
            auto const c = design_lowpass(params);
            set(c.a0, c.a1, c.a2, c.b1, c.b2, params.channel);
        }

        void calculate_highpass(FilterParams<> const& params)
        {
            auto const c = design_highpass(params);
            set(c.a0, c.a1, c.a2, c.b1, c.b2, params.channel);
        }

        void calculate_bandpass(FilterParams<> const& params)
        {
            auto const c = design_bandpass(params);
            set(c.a0, c.a1, c.a2, c.b1, c.b2, params.channel);
        }

        void calculate_notch(FilterParams<> const& params)
        {
            auto const c = design_notch(params);
            set(c.a0, c.a1, c.a2, c.b1, c.b2, params.channel);
        }

        void calculate_peak(FilterParams<> const& params)
        {
            auto const c = design_peak(params);
            set(c.a0, c.a1, c.a2, c.b1, c.b2, params.channel);
        }

        void calculate_lowshelf(FilterParams<> const& params)
        {
            auto const c = design_lowshelf(params);
            set(c.a0, c.a1, c.a2, c.b1, c.b2, params.channel);
        }

        void calculate_highshelf(FilterParams<> const& params)
        {
            auto const c = design_highshelf(params);
            set(c.a0, c.a1, c.a2, c.b1, c.b2, params.channel);
        }

        /// Evaluate the filter's transfer function H(z) in the z-domain
        ///
        /// Computes H(z) = (a0 + a1*z^-1 + a2*z^-2) / (1 + b1*z^-1 + b2*z^-2)
        /// for a given z value. This allows calculation of the filter's
        /// frequency response and phase shift at any frequency.
        ///
        /// @tparam ComplexType Complex number type (e.g., std::complex<double>)
        /// @param z1 The z^-1 value; for frequency response use z^-1 = e^(-jω)
        ///           where ω = 2πf/fs (normalized angular frequency)
        /// @param channel Channel index for multi-channel (SIMD) filters
        /// @return Complex frequency response H(z) at the given z value
        ///
        /// The returned complex value encodes both magnitude and phase:
        /// - Magnitude response: std::abs(result) gives amplitude gain
        /// - Phase response: std::arg(result) gives phase shift in radians
        /// - Magnitude in dB: 20 * log10(std::abs(result))
        ///
        /// Example - calculate frequency response at 1 kHz with 48 kHz sample rate:
        /// @code
        /// BiQuad<double> filter;
        /// filter.coeffs.calculate_lowpass({
        ///     .sample_rate_recip = 1.0 / 48000.0,
        ///     .frequency = 1000.0,
        ///     .q = 0.707
        /// });
        ///
        /// double freq = 1000.0;
        /// double sample_rate = 48000.0;
        /// double omega = 2.0 * M_PI * freq / sample_rate;
        /// std::complex<double> z1(std::cos(-omega), std::sin(-omega)); // e^(-jω)
        ///
        /// auto response = filter.coeffs.process_z_domain(z1);
        /// double magnitude = std::abs(response);      // ~0.707 at cutoff
        /// double phase_rad = std::arg(response);      // phase shift
        /// double magnitude_db = 20.0 * std::log10(magnitude); // ~-3 dB
        /// @endcode
        template <typename ComplexType>
        auto process_z_domain(ComplexType const z1, size_t const channel = 0) -> ComplexType
        {
            ComplexType const one(1.0, 0.0);
            ComplexType const z2 = z1 * z1;

            // Flattened scalar (item_type), not T: for a SIMD T the vector's
            // scalar constructor is explicit, so `T c = get_flattened_item(...)`
            // fails to compile — the per-channel z-domain response is scalar.
            item_type const c_a0 = get_flattened_item(a0, channel);
            item_type const c_a1 = get_flattened_item(a1, channel);
            item_type const c_a2 = get_flattened_item(a2, channel);
            item_type const c_b1 = get_flattened_item(b1, channel);
            item_type const c_b2 = get_flattened_item(b2, channel);

            ComplexType const numerator = c_a0 + (c_a1 * z1) + (c_a2 * z2);
            ComplexType const denominator = one + (c_b1 * z1) + (c_b2 * z2);
            return numerator / denominator;
        }
    };

    struct State
    {
        T z1, z2;

        State() noexcept
        {
            zero(z1);
            zero(z2);
        }

        State(State const& other) = default;
        State(State&&) = default;
        auto operator=(State const& other) -> State& = default;
        auto operator=(State&&) -> State& = default;
        ~State() = default;

        void reset() noexcept
        {
            zero(z1);
            zero(z2);
        }
    };

    Coeffs coeffs;
    State state;

    BiQuad() = default;
    BiQuad(BiQuad const&) = default;
    auto operator=(BiQuad const&) -> BiQuad& = default;
    BiQuad(BiQuad&&) = default;
    auto operator=(BiQuad&&) -> BiQuad& = default;
    ~BiQuad() = default;

    /// Reset filter state to zero (clears delay lines)
    void reset() noexcept { state.reset(); }

    [[nodiscard]] auto operator()(T const& input_value) noexcept -> T
    {
        T output_value;

        output_value = (input_value * coeffs.a0) + state.z1;
        state.z1 = (input_value * coeffs.a1) + state.z2 - (coeffs.b1 * output_value);
        state.z2 = (input_value * coeffs.a2) - (coeffs.b2 * output_value);

        return output_value;
    }
};
}  // namespace statusbar::dsp
