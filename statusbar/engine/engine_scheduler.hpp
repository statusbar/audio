#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/engine/engine_element.hpp"

#include <cstddef>

namespace statusbar::engine {

// Drive a single Tier 0 Element through `cycle_count` cycles of audio.
//
// This is the simplest possible scheduler: one Element, no graph, no
// inter-element wiring. Useful for unit tests and standalone elements
// (single-channel send, single matrix crosspoint, etc.). Composes a graph
// scheduler when richer topologies are needed.
//
// Input and output are flat buffers of (cycle_count * samples_per_cycle)
// samples each. The per-cycle subdivision is derived from the Element's
// data_in extent, so the caller never has to know samples_per_cycle.
//
// HardRT contract: this loop must complete its `cycle_count` cycles within
// the audio deadline. The caller is responsible for sizing `cycle_count` to
// match the available time budget — typically one cycle per call from the
// audio callback, with `cycle_count = 1`.

template <IsElement E>
inline void drive_cycles(
    E& element,
    typename E::data_in::value_type const* input_begin,
    typename E::data_out::value_type* output_begin,
    size_t cycle_count) noexcept
{
    constexpr size_t samples_per_cycle = E::data_in::extent;
    for (size_t c = 0; c < cycle_count; ++c) {
        size_t const offset = c * samples_per_cycle;
        element.process_cycle(
            typename E::data_in{input_begin + offset, samples_per_cycle},
            typename E::data_out{output_begin + offset, samples_per_cycle});
    }
}

}  // namespace statusbar::engine
