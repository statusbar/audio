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
        auto const& c1 = c.stage1;
        auto const& c2 = c.stage2;

        // Stage 1: real input → complex output.
        T const y1_re = (c1.a0_re * input) + state1_re_;
        T const y1_im = (c1.a0_im * input) + state1_im_;
        state1_re_ = (c1.a1_re * input) - (c1.b1_re * y1_re) + (c1.b1_im * y1_im);
        state1_im_ = (c1.a1_im * input) - (c1.b1_re * y1_im) - (c1.b1_im * y1_re);

        // Stage 2: complex input → real output (out_im is ~0 for real input).
        T const out_re = (c2.a0_re * y1_re) - (c2.a0_im * y1_im) + state2_re_;
        T const out_im = (c2.a0_re * y1_im) + (c2.a0_im * y1_re) + state2_im_;
        state2_re_ = (c2.a1_re * y1_re) - (c2.a1_im * y1_im) - (c2.b1_re * out_re) + (c2.b1_im * out_im);
        state2_im_ = (c2.a1_re * y1_im) + (c2.a1_im * y1_re) - (c2.b1_re * out_im) - (c2.b1_im * out_re);

        (void)out_im;
        return out_re;
    }

    void reset_state() noexcept
    {
        statusbar::dsp::zero(state1_re_);
        statusbar::dsp::zero(state1_im_);
        statusbar::dsp::zero(state2_re_);
        statusbar::dsp::zero(state2_im_);
    }

  private:
    T state1_re_;
    T state1_im_;
    T state2_re_;
    T state2_im_;
};

// Convenience alias: a Tier 0 biquad element parameterized by sample
// rate and audio sample type. T defaults to float (one channel); set
// T = simd_float32x4 for four parallel channels in one element, etc.
template <size_t SampleRateHz, typename T = float>
using BiquadElement = SmoothedElement<SampleRateHz, T, BiquadComplexCoeffs<T>, BiquadApply<T>>;

}  // namespace statusbar::engine
