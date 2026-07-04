#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/dsp/dsp_vec.hpp"
#include "statusbar/engine/engine_biquad_coeffs.hpp"
#include "statusbar/engine/engine_smoothed_element.hpp"

#include <cstddef>

namespace statusbar::engine {

// BiquadApply<T> — Apply functor for a Tier 0 biquad element.
//
// Templated on the audio sample type T. For T = float this processes one
// channel of biquad. For T = simd_float32x4 it processes four
// independent channels in parallel — each SIMD lane carries one
// channel's coefficients and state, and the math runs lane-wise so all
// channels advance simultaneously through one operator() call.
//
// Per-sample inner loop (all arithmetic is lane-wise on SIMD T):
//   stage 1: real input (T) → complex (re, im) intermediate (T, T)
//   stage 2: complex intermediate → real output (T)
//
// Owns four T values of delay-line state (one per stage's complex
// component). State is zeroed via dsp::zero() in the constructor so it
// initializes correctly for both scalar and SIMD T.
//
// Header-only / inline because this is the audio-rate hot path.

template <typename T = float>
class BiquadApply
{
  public:
    BiquadApply() noexcept { reset_state(); }

    [[nodiscard]] auto operator()(BiquadComplexCoeffs<T> const& c, T input) noexcept -> T
    {
        // Shared kernel — see dsp::process_complex_biquad_sample. The engine and
        // dsp::ComplexBiQuad differ only in how they store coeffs/state, not in
        // the biquad math, so the math lives in one place.
        return statusbar::dsp::process_complex_biquad_sample(c.stage1, c.stage2, state1_, state2_, input);
    }

    void reset_state() noexcept
    {
        state1_.reset();
        state2_.reset();
    }

  private:
    statusbar::dsp::ComplexFirstOrderState<T> state1_;
    statusbar::dsp::ComplexFirstOrderState<T> state2_;
};

// Convenience alias: a Tier 0 biquad element parameterized by sample
// rate and audio sample type. T defaults to float (one channel); set
// T = simd_float32x4 for four parallel channels in one element, etc.
template <size_t SampleRateHz, typename T = float>
using BiquadElement = SmoothedElement<SampleRateHz, T, BiquadComplexCoeffs<T>, BiquadApply<T>>;

}  // namespace statusbar::engine
