#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/engine/engine_gain_coeffs.hpp"
#include "statusbar/engine/engine_smoothed_element.hpp"

#include <cstddef>

namespace statusbar::engine {

// GainApply<T> — Apply functor for a Tier 0 gain element.
//
// Templated on the audio sample type T. For T = float this is one
// channel of gain. For T = simd_float32x4 it's four independent
// channels in parallel — each lane multiplies its own input by its own
// amplitude in a single SIMD instruction.
//
// Stateless: gain is just one multiply per sample. The engine's per-leg
// Segment ramping handles all smoothing externally.
//
// Header-only / inline because this is the audio-rate hot path.

template <typename T = float>
struct GainApply
{
    [[nodiscard]] constexpr auto operator()(GainAmplitudeCoeffs<T> const& c, T input) const noexcept -> T
    {
        return c.amplitude * input;
    }
};

// Convenience alias: a Tier 0 gain element parameterized by sample rate
// and audio sample type. T defaults to float (one channel); set
// T = simd_float32x4 for four parallel channels in one element, etc.
//
// Initial state: GainAmplitudeCoeffs<T> default-constructs to amplitude=0,
// so a freshly constructed GainElement outputs silence until either
// prime_segment() seeds a different starting amplitude or a Tier 1
// GainDesigner publishes its first Segment at the next leg boundary.
// This matches the engine's freeze-on-miss discipline (hold the previous
// value, which initially is zero) and is rarely surprising in practice
// because Tier 1 publishes a hold_segment as its very first step. Callers
// that wire up a GainElement without a producer should prime_segment()
// explicitly to avoid an audible mute-then-fade-in on startup.
template <size_t SampleRateHz, typename T = float>
using GainElement = SmoothedElement<SampleRateHz, T, GainAmplitudeCoeffs<T>, GainApply<T>>;

}  // namespace statusbar::engine
