#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include <concepts>

namespace statusbar::engine {

// A Segment describes how a coefficient set transitions over one leg.
//
// The triple (start, end, delta) is intentionally redundant. delta is
// computable as (end - start) * recip_samples_per_leg, but storing it
// denormalized lets Tier 1's inner loop pick its evaluation strategy:
//
//   - Accumulation: current = current + delta   (cheap, one add per sample,
//                                                accumulates round-off across
//                                                the leg)
//   - Formula:      coeffs  = start + delta * n  (one FMA per sample, no
//                                                drift across the leg)
//
// Both are mathematically equivalent up to floating-point round-off, and the
// snap discipline at leg boundary (current = end) caps that round-off at
// one leg's worth — sub-LSB even at 24-bit audio.
//
// Self-validation: at the start of a leg, Tier 1 may assert current ≈ start.
// A mismatch indicates a missed publish or a previous-leg miscount.

template <typename Coeffs>
struct Segment
{
    Coeffs start;  // coefficient at sample 0 of this leg
    Coeffs end;    // coefficient at sample samples_per_leg of this leg
    Coeffs delta;  // (end - start) * recip_samples_per_leg, denormalized
};

// Required arithmetic for any Coeffs type carried by Segment.
// The Coeffs type is treated as a vector space over a scalar — pointwise add
// and subtract, scale by float.
template <typename C>
concept SegmentableCoeffs = requires(C const& a, C const& b, float s) {
    { a + b } -> std::same_as<C>;
    { a - b } -> std::same_as<C>;
    { a* s } -> std::same_as<C>;
};

// Construct a segment from start + end + recip_N. Tier 2 calls this once
// per leg per element.
template <SegmentableCoeffs Coeffs, typename Scalar>
[[nodiscard]] constexpr auto make_segment(Coeffs const& start, Coeffs const& end, Scalar recip_samples_per_leg) noexcept
    -> Segment<Coeffs>
{
    return Segment<Coeffs>{
        .start = start,
        .end = end,
        .delta = (end - start) * recip_samples_per_leg,
    };
}

// Hold-in-place segment: produced when an element should freeze at its current
// endpoint (e.g., when Tier 2 misses its publishing window). delta = 0, both
// endpoints equal value.
//
// Requires Coeffs to be default-constructible to a zero/identity state, which
// holds for the aggregate POD Coeffs types used by dsp filters.
template <SegmentableCoeffs Coeffs>
    requires std::default_initializable<Coeffs>
[[nodiscard]] constexpr auto hold_segment(Coeffs const& value) noexcept -> Segment<Coeffs>
{
    return Segment<Coeffs>{.start = value, .end = value, .delta = Coeffs{}};
}

}  // namespace statusbar::engine
