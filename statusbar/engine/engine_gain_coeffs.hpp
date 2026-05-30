#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/dsp/dsp_vec_base.hpp"
#include "statusbar/engine/engine_segment.hpp"

namespace statusbar::engine {

// Single-channel-per-lane scalar amplitude coefficients for a Tier 0 gain
// element.
//
// Templated on the audio sample type T so the same element type can carry
// multiple parallel channels:
//
//   GainAmplitudeCoeffs<float>           — one channel of gain
//   GainAmplitudeCoeffs<simd_float32x4>  — four parallel channels of gain
//
// For SIMD T, the amplitude field holds N independent values (one per
// SIMD lane). Linear interpolation between two amplitudes IS the natural
// ramp — there's no stability region to worry about, no design math, no
// morph state. Any taper / perceptual curve (audio taper, S-curve, log)
// is the responsibility of upstream tiers; Tier 1 GainDesigner sees the
// already-curve-evaluated amplitude per leg.

template <typename T = float>
struct GainAmplitudeCoeffs
{
    T amplitude{};
};

template <typename T>
constexpr auto operator+(GainAmplitudeCoeffs<T> const& a, GainAmplitudeCoeffs<T> const& b) noexcept -> GainAmplitudeCoeffs<T>
{
    return {a.amplitude + b.amplitude};
}

template <typename T>
constexpr auto operator-(GainAmplitudeCoeffs<T> const& a, GainAmplitudeCoeffs<T> const& b) noexcept -> GainAmplitudeCoeffs<T>
{
    return {a.amplitude - b.amplitude};
}

template <typename T>
constexpr auto operator*(GainAmplitudeCoeffs<T> const& a, float s) noexcept -> GainAmplitudeCoeffs<T>
{
    // dsp::mul recurses through any SIMDVec nesting depth.
    return {statusbar::dsp::mul(a.amplitude, s)};
}

static_assert(SegmentableCoeffs<GainAmplitudeCoeffs<float>>);

}  // namespace statusbar::engine
