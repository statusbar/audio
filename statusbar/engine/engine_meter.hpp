#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/dsp/dsp_vec_base.hpp"
#include "statusbar/engine/engine_constants.hpp"
#include "statusbar/engine/engine_element.hpp"
#include "statusbar/itc/itc_message_pipe.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace statusbar::engine {

// Measurement<T> — per-leg metering snapshot.
//
// peak: maximum |sample| observed across the leg.
// rms : root-mean-square of samples observed across the leg.
//
// For SIMD T, both fields are T-typed: each SIMD lane carries its
// channel's independent measurement, readable via dsp::get_flattened_item.
template <typename T>
struct Measurement
{
    T peak{};
    T rms{};
};

// Meter<SR, T> — Tier 0 passthrough element that emits per-leg peak
// and RMS measurements upward via its ctrl_out pipe.
//
// Per-cycle behavior:
//   - Copies input to output unchanged (zero gain effect on the audio).
//   - Updates peak: peak = max(peak, |sample|) lane-wise.
//   - Accumulates sum-of-squares for RMS.
//
// Per leg boundary:
//   - Computes RMS = sqrt(sum_of_squares * recip_samples_per_leg).
//   - Publishes Measurement<T> to ctrl_out via itc::LatestPipe (latest-wins
//     semantics — UI/Tier 2 polls at its own pace; missed reads just
//     mean the GUI saw an older value).
//   - Resets accumulators for the next leg.
//
// The "meter as passthrough Element" pattern keeps composition simple:
// insert a Meter anywhere in a manual chain of Elements to tap that
// signal point without disturbing the audio.
//
// Telemetry consumer can be left unconnected; the meter then just
// updates its internal `last_measurement()` snapshot for query.
//
// SIMD support: works for scalar T and one-level SIMD T (e.g.
// simd_float32x4 → 4 independent per-lane meters). Nested SIMD is not
// supported here because the friend `sqrt` and `abs` on SIMDVec call
// std::sqrt / std::abs directly on lanes, which fails for nested
// SIMDVec lanes; see SIMDVec base for the underlying constraint.

template <size_t SampleRateHz, typename T = float>
class Meter
{
    // Reject nested SIMD T (e.g., SIMDVec<SIMDVec<float,2>,4>) at instantiation
    // time so the user gets a readable error here instead of a chain of
    // overload-resolution failures from the friend `sqrt` / `abs` on SIMDVec
    // trying to call std::sqrt / std::abs on inner SIMDVec lanes.
    static_assert(
        statusbar::dsp::simd_size<T>::value == statusbar::dsp::simd_flattened_size<T>::value,
        "Meter does not support nested SIMD T — use one-level SIMDVec or a scalar type");

  public:
    using engine_consts = EngineConstants<SampleRateHz>;
    static constexpr size_t samples_per_cycle = engine_consts::samples_per_cycle;

    using value_type = T;
    using measurement = Measurement<T>;

    using data_in = std::span<T const, samples_per_cycle>;
    using data_out = std::span<T, samples_per_cycle>;
    using ctrl_in = void;
    using ctrl_out = itc::LatestPipe<measurement>;

    static constexpr size_t level = 0;
    static constexpr RealtimePolicy realtime_policy = RealtimePolicy::HardRT;

    Meter() noexcept { reset_accumulators(); }

    Meter(Meter const&) = delete;
    auto operator=(Meter const&) -> Meter& = delete;

    // Connect the upward telemetry pipe. Optional — if no pipe is
    // connected, the meter still updates last_measurement() each leg
    // for direct query but does no publishing.
    void connect_telemetry(ctrl_out& pipe) noexcept { telemetry_pipe_ = &pipe; }

    void process_cycle(data_in input, data_out output) noexcept
    {
        using std::abs;
        using std::max;
        for (size_t n = 0; n < samples_per_cycle; ++n) {
            T const x = input[n];
            output[n] = x;  // passthrough
            T const abs_x = abs(x);
            peak_accum_ = max(peak_accum_, abs_x);
            sumsq_accum_ = sumsq_accum_ + (x * x);
        }
        if (++cycle_in_leg_ >= engine_consts::cycles_per_leg) {
            publish_and_reset();
        }
    }

    // Most recent published measurement. Updated at each leg boundary
    // (whether or not a telemetry pipe is connected).
    [[nodiscard]] auto last_measurement() const noexcept -> measurement const& { return last_measurement_; }

  private:
    void publish_and_reset() noexcept
    {
        using std::sqrt;
        measurement m;
        m.peak = peak_accum_;
        m.rms = sqrt(statusbar::dsp::mul(sumsq_accum_, engine_consts::recip_samples_per_leg_f));
        last_measurement_ = m;
        if (telemetry_pipe_ != nullptr && telemetry_pipe_->can_publish()) {
            telemetry_pipe_->publish(m);
        }
        reset_accumulators();
    }

    void reset_accumulators() noexcept
    {
        statusbar::dsp::zero(peak_accum_);
        statusbar::dsp::zero(sumsq_accum_);
        cycle_in_leg_ = 0;
    }

    T peak_accum_{};
    T sumsq_accum_{};
    size_t cycle_in_leg_{0};
    measurement last_measurement_{};
    ctrl_out* telemetry_pipe_{nullptr};
};

}  // namespace statusbar::engine
