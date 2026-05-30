#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include <concepts>
#include <cstddef>
#include <cstdint>

namespace statusbar::engine {

// Real-time policy for an Element.
//
// HardRT     — must complete every cycle within budget. A miss is a bug, not
//              a tolerated event. Tier 1 audio elements live here.
// BestEffort — may occasionally miss a cycle. Downstream consumer holds the
//              previous value (freeze-on-miss). Tier 2+ elements live here:
//              the worst case for a missed Tier 2 cycle is that one element's
//              coefficients freeze for one leg (~5 ms) — inaudible.
enum class RealtimePolicy : uint8_t
{
    HardRT,
    BestEffort
};

// IsElement concept — minimal contract for anything the engine's schedulers
// can drive.
//
// Each Element is a regular type (no inheritance, no vtables) exposing:
//
//   - constexpr static size_t          level
//         Discriminator within the tier hierarchy. Lower = faster rate,
//         closer to audio. Audio-rate elements are level 0.
//
//   - constexpr static RealtimePolicy  realtime_policy
//
//   - typename data_in    — the per-cycle data pulled from upstream
//                           (e.g., a span of input samples). May be void
//                           for sources.
//   - typename data_out   — the per-cycle data pushed downstream
//                           (e.g., a span of output samples). May be void
//                           for sinks.
//   - typename ctrl_in    — the control message type consumed (typically a
//                           Segment<Coeffs> or a higher-level command).
//                           May be void if the element has no control input.
//   - typename ctrl_out   — telemetry pushed up to the next tier
//                           (e.g., meter readings). May be void.
//
//   - void process_cycle(data_in const&, data_out&) noexcept
//         The hot path. Called once per cycle. Must be wait-free for HardRT.
//
// Schedulers compose Elements by static type and dispatch through this
// concept; there are no runtime-virtual calls on the audio path.

template <typename E>
concept IsElement = requires {
    { E::level } -> std::convertible_to<size_t>;
    { E::realtime_policy } -> std::convertible_to<RealtimePolicy>;
    typename E::data_in;
    typename E::data_out;
    typename E::ctrl_in;
    typename E::ctrl_out;
};

}  // namespace statusbar::engine
