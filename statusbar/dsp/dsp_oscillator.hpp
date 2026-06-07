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

template <typename T>
struct FrequencyParameters
{
    T sample_rate_recip;
    T frequency;
    T phase_in_radians;
};

template <typename T>
struct NoteParameters
{
    T sample_rate_recip;
    int octave{0};
    int note{0};
    T tuning_in_cents{0};
    T tuning_of_a{440.0};
    T phase_in_radians{0.0};
};

inline double oscillator_octave_multiplier_table[8] = {1.0 / 8.0, 1.0 / 4.0, 1.0 / 2.0, 1.0, 2.0, 4.0, 8.0, 16.0};

inline float oscillator_octave_multiplier_table_f[8] = {1.0F / 8.0F, 1.0F / 4.0F, 1.0F / 2.0F, 1.0F, 2.0F, 4.0F, 8.0F, 16.0F};

inline double oscillator_note_frequencies_a440[12] = {
    440.00,  // A
    466.16,  // A#
    493.92,  // B
    523.28,  // C
    554.40,  // C#
    587.36,  // D
    622.24,  // D#
    659.28,  // E
    698.48,  // F
    740.00,  // F#
    784.00,  // G
    830.64   // G#
};

inline float oscillator_note_frequencies_a440_f[12] = {
    440.00F,  // A
    466.16F,  // A#
    493.92F,  // B
    523.28F,  // C
    554.40F,  // C#
    587.36F,  // D
    622.24F,  // D#
    659.28F,  // E
    698.48F,  // F
    740.00F,  // F#
    784.00F,  // G
    830.64F   // G#
};

template <typename T>
struct Oscillator
{
    using value_type = T;
    using item_type = simd_flattened_type<T>::type;

    static constexpr size_t vector_size = simd_size<T>::value;
    static constexpr size_t flattened_size = simd_flattened_size<T>::value;

    struct Coeffs
    {
        T amplitude_;

        void set_amplitude(item_type const& v, size_t channel) noexcept { set_flattened_item(amplitude_, v, channel); }
    };

    struct State
    {
        T a_, z1_, z2_;

        State() noexcept
        {
            zero(z1_);
            zero(z2_);
        }

        ~State() = default;
        State(State const&) = default;
        auto operator=(State const&) -> State& = default;
        State(State&&) noexcept = default;
        auto operator=(State&&) noexcept -> State& = default;

        template <typename U = double>
        void set_frequency(FrequencyParameters<U> params, size_t channel = 0) noexcept
        {
            U w = constants::two_pi<U>() * params.frequency * params.sample_rate_recip;
            // Seed the 2-pole resonator with the two prior outputs of a UNIT
            // sine at the requested phase: z1 = sin(phase) [y(-1)], z2 =
            // sin(phase - w) [y(-2)]. Then o(n) = sin((n+1)w + phase) with
            // amplitude exactly 1 for every phase. The previous seed (z1=0,
            // z2=sin(w+phase)) gave amplitude |sin(w+phase)/sin(w)|, which is 1
            // only near phase 0 or pi and blows up (~sin(phase)/sin(w)) for
            // other phases at low w -> clipping on phase-offset channels.
            U temp1 = std::sin(params.phase_in_radians);
            U temp2 = std::sin(params.phase_in_radians - w);
            U tempa = constants::two<U>() * std::cos(w);
            auto const nz1 = static_cast<item_type>(temp1);
            auto const nz2 = static_cast<item_type>(temp2);
            auto const na = static_cast<item_type>(tempa);
            set_flattened_item(z1_, nz1, channel);
            set_flattened_item(z2_, nz2, channel);
            set_flattened_item(a_, na, channel);
        }

        template <typename U = double>
        void set_frequency_note(NoteParameters<U> params, size_t channel = 0) noexcept
        {
            U tuning_multiplier = std::pow(U(2.0), params.tuning_in_cents * (U(1.0) / U(1200.0)));
            U octave_multiplier;
            U base_freq;
            if constexpr (std::is_same_v<U, float>) {
                octave_multiplier = oscillator_octave_multiplier_table_f[params.octave];
                base_freq = oscillator_note_frequencies_a440_f[params.note];
            } else {
                octave_multiplier = static_cast<U>(oscillator_octave_multiplier_table[params.octave]);
                base_freq = static_cast<U>(oscillator_note_frequencies_a440[params.note]);
            }
            U a_tuning_multiplier = params.tuning_of_a * (U(1.0) / U(440.0));
            U freq = base_freq * tuning_multiplier * octave_multiplier * a_tuning_multiplier;
            set_frequency(FrequencyParameters<U>{params.sample_rate_recip, freq, params.phase_in_radians}, channel);
        }
    };

    Coeffs coeffs_;
    State state_;

    ~Oscillator() = default;
    Oscillator() = default;
    Oscillator(Oscillator const&) = default;
    auto operator=(Oscillator const&) -> Oscillator& = default;
    Oscillator(Oscillator&&) = default;
    auto operator=(Oscillator&&) -> Oscillator& = default;

    [[nodiscard]] auto operator()(T const& input_value) noexcept -> T
    {
        T output_value = (state_.a_ * state_.z1_) - state_.z2_;
        state_.z2_ = state_.z1_;
        state_.z1_ = output_value;

        return (output_value * coeffs_.amplitude_) + input_value;
    }
};

}  // namespace statusbar::dsp
