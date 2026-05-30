// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/dsp/dsp_biquad_design.hpp"

#include "statusbar/dsp/dsp_complex_biquad.hpp"
#include "statusbar/dsp/dsp_filter_design.hpp"

namespace statusbar::dsp {

namespace {

auto build_filter_params(BiquadDesignParams const& params, double sample_rate_recip) noexcept -> FilterParams<double>
{
    FilterParams<double> p{};
    p.sample_rate_recip = sample_rate_recip;
    p.frequency = params.frequency_hz;
    p.q = params.q;
    p.gain_db = params.gain_db;
    p.channel = 0;
    return p;
}

auto design_in_double(BiquadDesignParams const& params, double sample_rate_recip) noexcept -> BiquadCoeffsF64
{
    auto const p = build_filter_params(params, sample_rate_recip);
    switch (params.type) {
        case BiquadFilterType::Lowpass:
            return design_lowpass(p);
        case BiquadFilterType::Highpass:
            return design_highpass(p);
        case BiquadFilterType::Bandpass:
            return design_bandpass(p);
        case BiquadFilterType::Notch:
            return design_notch(p);
        case BiquadFilterType::Peak:
            return design_peak(p);
        case BiquadFilterType::LowShelf:
            return design_lowshelf(p);
        case BiquadFilterType::HighShelf:
            return design_highshelf(p);
    }
    // Unreachable for valid enum values; return identity (bypass).
    return BiquadCoeffsF64{.a0 = 1.0, .a1 = 0.0, .a2 = 0.0, .b1 = 0.0, .b2 = 0.0};
}

auto cast_stage_to_float(std::complex<double> const& a0, std::complex<double> const& a1, std::complex<double> const& b1) noexcept
    -> ComplexFirstOrderCoeffs<float>
{
    ComplexFirstOrderCoeffs<float> result{};
    result.a0_re = static_cast<float>(a0.real());
    result.a0_im = static_cast<float>(a0.imag());
    result.a1_re = static_cast<float>(a1.real());
    result.a1_im = static_cast<float>(a1.imag());
    result.b1_re = static_cast<float>(b1.real());
    result.b1_im = static_cast<float>(b1.imag());
    return result;
}

}  // namespace

auto design_biquad_pole_zero(BiquadDesignParams const& params, double sample_rate_recip) noexcept
    -> std::pair<ComplexFirstOrderCoeffs<float>, ComplexFirstOrderCoeffs<float>>
{
    auto const bc = design_in_double(params, sample_rate_recip);
    auto const ts = detail::factor_biquad_to_two_stages(bc.a0, bc.a1, bc.a2, bc.b1, bc.b2);
    return std::pair{
        cast_stage_to_float(ts.s1_a0, ts.s1_a1, ts.s1_b1),
        cast_stage_to_float(ts.s2_a0, ts.s2_a1, ts.s2_b1),
    };
}

}  // namespace statusbar::dsp
