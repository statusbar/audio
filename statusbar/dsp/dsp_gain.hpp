#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/dsp/dsp_constants.hpp"
#include "statusbar/dsp/dsp_vec.hpp"

#include <cmath>
#include <cstddef>
#include <span>
#include <type_traits>

namespace statusbar::dsp {

/// Smoothed gain control with exponential smoothing
///
/// Provides amplitude control with configurable smoothing time to avoid
/// clicks and pops when changing gain values. The gain ramps exponentially
/// from current to target amplitude over the specified time constant.
///
/// @tparam T Value type (double, float, or SIMD vector type)
template <typename T>
struct Gain
{
    using value_type = T;
    using item_type = simd_flattened_type<T>::type;

    static constexpr size_t vector_size = simd_size<T>::value;
    static constexpr size_t flattened_size = simd_flattened_size<T>::value;

    struct Coeffs
    {
        T amplitude{};
        T time_constant{};
        T one_minus_time_constant{};

        /// Set the target amplitude (linear gain)
        ///
        /// @param v Amplitude value (1.0 = unity gain, 0.0 = silence)
        /// @param channel Channel index for multi-channel (SIMD) filters (default: 0)
        void set_amplitude(item_type v, size_t channel = 0) { set_flattened_item(amplitude, v, channel); }

        /// Set the target amplitude in decibels
        ///
        /// @param db Gain in decibels (0 dB = unity, -inf dB = silence)
        /// @param channel Channel index for multi-channel (SIMD) filters (default: 0)
        void set_amplitude_db(double db, size_t channel = 0)
        {
            item_type v = static_cast<item_type>(std::pow(10.0, db / 20.0));
            set_flattened_item(amplitude, v, channel);
        }

        /// Set smoothing time constant
        ///
        /// Controls how quickly the gain ramps to the target value.
        /// Longer times provide smoother transitions but slower response.
        ///
        /// @param sample_rate Sample rate in Hz
        /// @param time_in_seconds Smoothing time (typical: 0.01 to 0.1 seconds)
        /// @param channel Channel index for multi-channel (SIMD) filters (default: 0)
        void set_time_constant(double sample_rate, double time_in_seconds, size_t channel = 0)
        {
            double const samples = time_in_seconds * sample_rate;
            item_type v = static_cast<item_type>(1.0 / samples);
            set_flattened_item(time_constant, v, channel);
            item_type one_val = constants::one<item_type>();
            set_flattened_item(one_minus_time_constant, one_val - v, channel);
        }

        /// Set to unity gain (bypass)
        ///
        /// @param channel Channel index for multi-channel (SIMD) filters (default: 0)
        void set_bypass(size_t channel = 0) { set_amplitude(constants::one<item_type>(), channel); }

        /// Set to silence (mute)
        ///
        /// @param channel Channel index for multi-channel (SIMD) filters (default: 0)
        void set_mute(size_t channel = 0)
        {
            item_type z;
            zero(z);
            set_amplitude(z, channel);
        }

        /// Evaluate the gain's transfer function H(z) in the z-domain
        ///
        /// For a simple gain, H(z) = amplitude (constant, frequency-independent).
        /// This is provided for API consistency with other DSP modules.
        ///
        /// @tparam ComplexType Complex number type (e.g., std::complex<double>)
        /// @param z1 The z^-1 value (unused for gain)
        /// @param channel Channel index for multi-channel (SIMD) filters
        /// @return Complex frequency response (real = amplitude, imag = 0)
        template <typename ComplexType>
        auto process_z_domain(ComplexType z1, size_t channel = 0) const -> ComplexType
        {
            (void)z1;  // Gain is frequency-independent
            return ComplexType(static_cast<ComplexType::value_type>(get_flattened_item(amplitude, channel)), 0);
        }
    };

    struct State
    {
        T current_amplitude;

        State() noexcept { zero(current_amplitude); }

        State(State const& other) = default;
        State(State&&) = default;
        auto operator=(State const& other) -> State& = default;
        auto operator=(State&&) -> State& = default;
        ~State() = default;

        /// Reset state to zero (gain will ramp from zero)
        void reset() noexcept { zero(current_amplitude); }

        /// Set current amplitude to match target (instant change, no ramping)
        /// @param coeffs Coefficients whose target amplitude to snap to
        void snap_to_target(Coeffs const& coeffs) noexcept { current_amplitude = coeffs.amplitude; }
    };

    Coeffs coeffs;
    State state;

    Gain() = default;
    Gain(Gain const&) = default;
    auto operator=(Gain const&) -> Gain& = default;
    Gain(Gain&&) = default;
    auto operator=(Gain&&) -> Gain& = default;
    ~Gain() = default;

    /// Reset filter state to zero
    void reset() noexcept { state.reset(); }

    /// Process a single sample with smoothed gain
    ///
    /// The current amplitude exponentially approaches the target amplitude
    /// according to the time constant, then the input is scaled by the
    /// current amplitude.
    ///
    /// @param input_value Input sample
    /// @return Gained output sample
    [[nodiscard]] auto operator()(T const& input_value) noexcept -> T
    {
        // Exponential smoothing: current = target * tc + current * (1 - tc)
        state.current_amplitude =
            (coeffs.amplitude * coeffs.time_constant) + (state.current_amplitude * coeffs.one_minus_time_constant);

        return input_value * state.current_amplitude;
    }
};

}  // namespace statusbar::dsp
