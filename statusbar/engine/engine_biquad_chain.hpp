#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/dsp/dsp_vec_base.hpp"
#include "statusbar/engine/engine_biquad_coeffs.hpp"
#include "statusbar/engine/engine_biquad_element.hpp"
#include "statusbar/engine/engine_segment.hpp"
#include "statusbar/engine/engine_smoothed_element.hpp"

#include <array>
#include <cstddef>

namespace statusbar::engine {

// BiquadChainCoeffs<N, T> — coefficients for a chain of N cascaded
// biquads. One combined array of N BiquadComplexCoeffs<T> stages, each
// of which is itself a cascaded pair of first-order complex sections —
// so the chain is structurally 2N first-order stages.
//
// SegmentableCoeffs is satisfied via pointwise operators that iterate
// over the N stages and delegate to BiquadComplexCoeffs<T>'s operators.
// Compile-time fixed N means the loops fully unroll; the memory layout
// is one contiguous std::array, friendly to cache and to SIMD packing
// when T is a SIMDVec.

template <size_t N, typename T = float>
struct BiquadChainCoeffs
{
    static_assert(N > 0, "BiquadChainCoeffs requires N >= 1");
    static constexpr size_t stage_count = N;

    std::array<BiquadComplexCoeffs<T>, N> stages{};
};

template <size_t N, typename T>
constexpr auto operator+(BiquadChainCoeffs<N, T> const& a, BiquadChainCoeffs<N, T> const& b) noexcept -> BiquadChainCoeffs<N, T>
{
    BiquadChainCoeffs<N, T> r{};
    for (size_t i = 0; i < N; ++i) {
        r.stages[i] = a.stages[i] + b.stages[i];
    }
    return r;
}

template <size_t N, typename T>
constexpr auto operator-(BiquadChainCoeffs<N, T> const& a, BiquadChainCoeffs<N, T> const& b) noexcept -> BiquadChainCoeffs<N, T>
{
    BiquadChainCoeffs<N, T> r{};
    for (size_t i = 0; i < N; ++i) {
        r.stages[i] = a.stages[i] - b.stages[i];
    }
    return r;
}

template <size_t N, typename T>
constexpr auto operator*(BiquadChainCoeffs<N, T> const& a, float s) noexcept -> BiquadChainCoeffs<N, T>
{
    BiquadChainCoeffs<N, T> r{};
    for (size_t i = 0; i < N; ++i) {
        r.stages[i] = a.stages[i] * s;
    }
    return r;
}

static_assert(SegmentableCoeffs<BiquadChainCoeffs<1, float>>);
static_assert(SegmentableCoeffs<BiquadChainCoeffs<3, float>>);

// BiquadChainApply<N, T> — Apply functor for a Tier 0 chain of N biquads.
//
// Holds N independent BiquadApply<T> instances (one per stage in the
// chain), each carrying its own delay-line state. Per-sample math
// cascades input through every stage in order:
//
//   input → stage[0] → stage[1] → ... → stage[N-1] → output
//
// Each stage is the existing complex-biquad math (cascaded first-order
// pair). For SIMD T, all N stages run lane-wise so the chain is N
// independent biquad chains processing in parallel — one chain per
// SIMD lane.

template <size_t N, typename T = float>
class BiquadChainApply
{
  public:
    [[nodiscard]] auto operator()(BiquadChainCoeffs<N, T> const& c, T input) noexcept -> T
    {
        T x = input;
        for (size_t i = 0; i < N; ++i) {
            x = stages_[i](c.stages[i], x);
        }
        return x;
    }

    void reset_state() noexcept
    {
        for (auto& s : stages_) {
            s.reset_state();
        }
    }

  private:
    std::array<BiquadApply<T>, N> stages_{};
};

// Convenience alias: a Tier 0 chain of N biquads parameterized by sample
// rate and audio sample type. Wraps SmoothedElement so it picks up snap-
// at-leg-boundary, freeze-on-miss, and segment ramping for free.
//
//   BiquadChain<3, 48000, float>            — 3 biquads in series, mono
//   BiquadChain<3, 48000, simd_float32x4>   — 3 biquads, 4 parallel chains
template <size_t N, size_t SampleRateHz, typename T = float>
using BiquadChain = SmoothedElement<SampleRateHz, T, BiquadChainCoeffs<N, T>, BiquadChainApply<N, T>>;

// ─────────────────────────────────────────────────────────────────────────────
// Frequency-response evaluation for the chain.
//
// The chain's transfer function is the product of each stage's transfer
// function:
//
//   H_chain(z) = ∏ H_biquad[i](z)
//
// Same shape as evaluate_z_domain / bode_point_at_frequency for a single
// biquad, just multiplies through every stage. For a GUI plotting an
// EQ's overall response, this gives the combined curve in one call;
// per-stage curves can be obtained by walking `coeffs.stages[i]`
// individually with the single-biquad evaluators in
// engine_biquad_coeffs.hpp.
// ─────────────────────────────────────────────────────────────────────────────

template <size_t N, typename T>
[[nodiscard]] inline auto evaluate_z_domain(
    BiquadChainCoeffs<N, T> const& coeffs, std::complex<double> const& z_inv, size_t channel = 0) noexcept -> std::complex<double>
{
    std::complex<double> H{1.0, 0.0};
    for (size_t i = 0; i < N; ++i) {
        H *= evaluate_z_domain(coeffs.stages[i], z_inv, channel);
    }
    return H;
}

template <size_t N, typename T>
[[nodiscard]] inline auto bode_point_at_frequency(
    BiquadChainCoeffs<N, T> const& coeffs, double freq_hz, double sample_rate_recip, size_t channel = 0) noexcept -> BodePoint
{
    auto const H = evaluate_z_domain(coeffs, z_inv_at_frequency(freq_hz, sample_rate_recip), channel);
    return BodePoint{.magnitude = std::abs(H), .phase = std::arg(H)};
}

}  // namespace statusbar::engine
