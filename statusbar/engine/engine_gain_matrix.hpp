#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/engine/engine_constants.hpp"
#include "statusbar/engine/engine_element.hpp"
#include "statusbar/engine/engine_segment.hpp"
#include "statusbar/itc/itc_message_pipe.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace statusbar::engine {

// MatrixCoeffs<NIn, NOut, T> — coefficient set for an NIn × NOut gain
// matrix, stored row-major as a flat array of NIn*NOut amplitudes.
//
// Indexing convention: a[out * NIn + in]. The output dimension is the
// "outer" (slow) axis so all coefficients contributing to a single
// output sit contiguously — better for the per-output reduction in the
// inner loop.
//
// Satisfies SegmentableCoeffs (pointwise +, -, * scalar) so a whole
// matrix can ride inside a single Segment, with per-leg whole-matrix
// publishes via itc::LatestPipe<Segment<MatrixCoeffs>>.

template <size_t NIn, size_t NOut, typename T = float>
struct MatrixCoeffs
{
    static constexpr size_t in_count = NIn;
    static constexpr size_t out_count = NOut;
    static constexpr size_t cell_count = NIn * NOut;

    std::array<T, cell_count> a{};

    constexpr auto operator+(MatrixCoeffs const& o) const noexcept -> MatrixCoeffs
    {
        MatrixCoeffs r;
        for (size_t k = 0; k < cell_count; ++k) {
            r.a[k] = a[k] + o.a[k];
        }
        return r;
    }
    constexpr auto operator-(MatrixCoeffs const& o) const noexcept -> MatrixCoeffs
    {
        MatrixCoeffs r;
        for (size_t k = 0; k < cell_count; ++k) {
            r.a[k] = a[k] - o.a[k];
        }
        return r;
    }
    constexpr auto operator*(T s) const noexcept -> MatrixCoeffs
    {
        MatrixCoeffs r;
        for (size_t k = 0; k < cell_count; ++k) {
            r.a[k] = a[k] * s;
        }
        return r;
    }

    [[nodiscard]] constexpr auto at(size_t in_idx, size_t out_idx) const noexcept -> T { return a[(out_idx * NIn) + in_idx]; }

    constexpr void set(size_t in_idx, size_t out_idx, T value) noexcept { a[(out_idx * NIn) + in_idx] = value; }
};

// GainMatrix<NIn, NOut, SR, T> — Tier 0 element implementing a fixed-size
// crosspoint mixer.
//
// Per cycle:
//   For each sample n in samples_per_cycle:
//     1. Per-cell delta accumulation (NIn*NOut adds; vectorizable).
//     2. Per-output reduction:
//          out[o][n] = sum over i of in[i][n] * current.a[o*NIn + i]
//
// Per leg boundary:
//   Try to consume a fresh whole-matrix Segment from the itc::LatestPipe and
//   adopt it; freeze on miss (same discipline as SmoothedElement).
//
// Inputs and outputs are arrays of static-extent spans, one per channel.
// Caller provides per-channel storage; matrix doesn't own audio buffers.

template <size_t NIn, size_t NOut, size_t SampleRateHz, typename T = float>
class GainMatrix
{
  public:
    using engine_consts = EngineConstants<SampleRateHz>;
    static constexpr size_t samples_per_cycle = engine_consts::samples_per_cycle;
    static constexpr size_t in_count = NIn;
    static constexpr size_t out_count = NOut;

    using value_type = T;
    using coeffs_type = MatrixCoeffs<NIn, NOut, T>;
    using channel_in = std::span<T const, samples_per_cycle>;
    using channel_out = std::span<T, samples_per_cycle>;
    using data_in = std::array<channel_in, NIn>;
    using data_out = std::array<channel_out, NOut>;
    using ctrl_in = itc::LatestPipe<Segment<coeffs_type>>;
    using ctrl_out = void;

    static constexpr size_t level = 0;
    static constexpr RealtimePolicy realtime_policy = RealtimePolicy::HardRT;

    GainMatrix() noexcept
        : active_{hold_segment(coeffs_type{})}
    {}
    GainMatrix(GainMatrix const&) = delete;
    auto operator=(GainMatrix const&) -> GainMatrix& = delete;

    void connect(ctrl_in& pipe) noexcept { pipe_ = &pipe; }

    // Manually seed the active segment. Useful for the first leg before
    // Tier 1 has had a chance to publish.
    void prime_segment(Segment<coeffs_type> const& seg) noexcept
    {
        active_ = seg;
        current_ = seg.start;
        cycle_in_leg_ = 0;
    }

    void process_cycle(data_in input, data_out output) noexcept
    {
        for (size_t n = 0; n < samples_per_cycle; ++n) {
            // Per-cell delta accumulation. Flat array, branchless,
            // vectorizable. The compiler can lift the delta load and
            // SIMD-add NIn*NOut cells per inner pass.
            for (size_t k = 0; k < coeffs_type::cell_count; ++k) {
                current_.a[k] += active_.delta.a[k];
            }
            // Per-output reduction. Coefficients for output o are
            // contiguous at a[o*NIn .. o*NIn + NIn].
            for (size_t o = 0; o < NOut; ++o) {
                T sum = T{0};
                for (size_t i = 0; i < NIn; ++i) {
                    sum += input[i][n] * current_.a[(o * NIn) + i];
                }
                output[o][n] = sum;
            }
        }
        if (++cycle_in_leg_ >= engine_consts::cycles_per_leg) {
            advance_leg();
        }
    }

    [[nodiscard]] auto current_coeffs() const noexcept -> coeffs_type const& { return current_; }

    [[nodiscard]] auto active_segment() const noexcept -> Segment<coeffs_type> const& { return active_; }

  private:
    void advance_leg() noexcept
    {
        if (pipe_ != nullptr) {
            if (auto next = pipe_->try_consume()) {
                active_ = *next;
                current_ = next->start;
                cycle_in_leg_ = 0;
                return;
            }
        }
        current_ = active_.end;
        active_ = hold_segment(current_);
        cycle_in_leg_ = 0;
    }

    coeffs_type current_{};
    Segment<coeffs_type> active_;
    size_t cycle_in_leg_{0};
    ctrl_in* pipe_{nullptr};
};

}  // namespace statusbar::engine
