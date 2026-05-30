#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/engine/engine_constants.hpp"
#include "statusbar/engine/engine_element.hpp"
#include "statusbar/engine/engine_segment.hpp"
#include "statusbar/itc/itc_message_pipe.hpp"

#include <concepts>
#include <cstddef>
#include <span>
#include <utility>

namespace statusbar::engine {

// SmoothApply concept — a callable that maps (Coeffs, T) → T.
//
// The apply functor is the per-sample math: given the current coefficient
// state and an input sample, produce an output sample. It must be cheap and
// branch-free for the audio inner loop. Stateless implementations (a single
// multiply for gain, a biquad evaluation for an EQ stage) are typical;
// stateful ones (carrying filter delay lines as members) are permitted —
// the concept takes `Apply&` not `Apply const&` so functors with mutable
// per-sample state are allowed.
template <typename Apply, typename Coeffs, typename T>
concept SmoothApply = requires(Apply& apply, Coeffs const& c, T input) {
    { apply(c, input) } -> std::convertible_to<T>;
};

// Tier 0 Element that wraps a (Coeffs, Apply) pair into the engine's
// segment-driven smoothing discipline.
//
// Per-cycle behavior:
//   - Inner loop: for each sample, current_coeffs += active.delta;
//                                    output = apply(current_coeffs, input).
//   - At end of every cycles_per_leg cycle: snap to the next segment's
//     start (or freeze at active.end if no new segment is available),
//     load the new active segment.
//
// The element is sample-rate-templated so the per-cycle and per-leg counts
// are compile-time constants. The std::span extents in data_in / data_out
// encode samples_per_cycle so the scheduler can deduce the chunk size from
// the Element type.

template <size_t SampleRateHz, typename T, SegmentableCoeffs Coeffs, typename Apply>
    requires SmoothApply<Apply, Coeffs, T>
struct SmoothedElement
{
    using engine_consts = EngineConstants<SampleRateHz>;
    using value_type = T;
    using coeffs_type = Coeffs;
    using apply_type = Apply;

    static constexpr size_t level = 0;
    static constexpr RealtimePolicy realtime_policy = RealtimePolicy::HardRT;

    using data_in = std::span<T const, engine_consts::samples_per_cycle>;
    using data_out = std::span<T, engine_consts::samples_per_cycle>;
    using ctrl_in = itc::LatestPipe<Segment<Coeffs>>;
    using ctrl_out = void;

    explicit SmoothedElement(Apply apply = Apply{}) noexcept
        : apply_{std::move(apply)}
        , active_{hold_segment(Coeffs{})}
    {}

    // Connect the Tier 1 → Tier 0 segment pipe. May be left null for tests
    // that prime segments manually via prime_segment().
    void connect(ctrl_in& pipe) noexcept { pipe_ = &pipe; }

    // Audio-thread hot path. Processes one cycle of samples_per_cycle samples,
    // detecting a leg boundary at the end if cycles_per_leg cycles have
    // elapsed since the last boundary.
    void process_cycle(data_in input, data_out output) noexcept
    {
        for (size_t n = 0; n < engine_consts::samples_per_cycle; ++n) {
            current_ = current_ + active_.delta;
            output[n] = apply_(current_, input[n]);
        }
        if (++cycle_in_leg_ >= engine_consts::cycles_per_leg) {
            advance_leg();
        }
    }

    // Manually seed the active segment. Useful for tests and for the very
    // first leg before the producer has had a chance to publish.
    void prime_segment(Segment<Coeffs> const& seg) noexcept
    {
        active_ = seg;
        current_ = seg.start;
        cycle_in_leg_ = 0;
    }

    [[nodiscard]] auto current_coeffs() const noexcept -> Coeffs const& { return current_; }
    [[nodiscard]] auto active_segment() const noexcept -> Segment<Coeffs> const& { return active_; }

  private:
    void advance_leg() noexcept
    {
        if (pipe_ != nullptr) {
            if (auto next = pipe_->try_consume()) {
                // Snap to the new segment's start. By Tier 1's discipline
                // this equals active_.end; a mismatch indicates Tier 1 is
                // requesting an intentional discontinuity (e.g., preset
                // recall).
                active_ = *next;
                current_ = next->start;
                cycle_in_leg_ = 0;
                return;
            }
        }
        // Freeze on miss: hold at previous endpoint with zero delta.
        current_ = active_.end;
        active_ = hold_segment(current_);
        cycle_in_leg_ = 0;
    }

    Apply apply_;
    Coeffs current_{};
    Segment<Coeffs> active_;
    size_t cycle_in_leg_{0};
    ctrl_in* pipe_{nullptr};
};

}  // namespace statusbar::engine
