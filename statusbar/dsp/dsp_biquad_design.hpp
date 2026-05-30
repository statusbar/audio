#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

//
// Friendly enum-dispatched biquad design API.
//
// The lower-level filter design templates in dsp_filter_design.hpp expose
// one design_*(params) function per filter type. This header wraps them
// with an enum-discriminated union of design parameters so callers can
// hold a single "what filter do I want?" struct and dispatch later.
//
// Pairs with the pole/zero (complex first-order) factoring in
// dsp_complex_biquad.hpp to produce the runtime coefficient form used
// for parameter-ramping (linear chord between two stable poles stays
// inside the unit disk by convexity).
//
// Design math runs in double; the returned coefficients are float so the
// audio inner loop runs in float without an extra conversion step.
//

#include "statusbar/dsp/dsp_complex_biquad.hpp"

#include <cstdint>
#include <utility>

namespace statusbar::dsp {

enum class BiquadFilterType : uint8_t
{
    Lowpass,
    Highpass,
    Bandpass,
    Notch,
    Peak,
    LowShelf,
    HighShelf,
};

struct BiquadDesignParams
{
    BiquadFilterType type{BiquadFilterType::Lowpass};
    double frequency_hz{1000.0};
    double q{0.707};
    double gain_db{0.0};  // applies to Peak / LowShelf / HighShelf only
};

// Compute the float pole/zero (cascaded first-order) biquad coefficients
// for a given design at a given sample rate. Runs the design math in
// double, factors via factor_biquad_to_two_stages, casts to float for
// runtime use.
//
// Takes the sample-rate reciprocal (1.0 / sample_rate_hz) directly to
// avoid a division here — matches FilterParams::sample_rate_recip and
// lets long-running callers pre-compute the reciprocal once.
//
// .first  is the first-stage coefficients
// .second is the second-stage coefficients
[[nodiscard]] auto design_biquad_pole_zero(BiquadDesignParams const& params, double sample_rate_recip) noexcept
    -> std::pair<ComplexFirstOrderCoeffs<float>, ComplexFirstOrderCoeffs<float>>;

}  // namespace statusbar::dsp
