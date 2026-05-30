#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include <cstddef>
#include <cstdint>

namespace statusbar::engine {

// Compile-time constants for the engine's tiered processing schedule.
//
// Two time units are central to the design:
//
//   Cycle:  one Tier 1 inner-loop tick. Always 8 samples wide at 48 kHz,
//           16 at 96 kHz, 32 at 192 kHz — i.e. samples_per_cycle = SR / 6000.
//           Cycle period is 1/6000 s ≈ 166.667 µs at every supported rate.
//           Every count is a power of two so SIMD lanes (4×, 8×, 16×) divide
//           cycle and leg sample counts exactly.
//
//   Leg:    the parameter-ramp interval. Always 32 cycles, regardless of
//           sample rate. Leg duration is 32 × 166.667 µs ≈ 5.333 ms. At 48 kHz
//           this is 256 samples; at 96 kHz, 512; at 192 kHz, 1024.
//
// The cycle period is decoupled from the AVB class A 125 µs presentation
// interval on purpose: the time-compensation buffer at the network boundary
// absorbs the difference. A single processing cycle always produces ≥ one
// PDU's worth of samples (8 ≥ 6 at 48 kHz, etc.), so the transmit FIFO never
// underruns and the receive FIFO sets the latency budget.
//
// The 6000 Hz divisor restricts SampleRate to AVB-family rates (48 / 96 / 192
// kHz and any other 6000-multiple). The 44.1 kHz family (44100 / 88200 /
// 176400) is intentionally NOT supported — those rates do not divide evenly
// into a 1/6000 s cycle, which would make samples_per_cycle non-integer and
// break the SIMD-alignment guarantee. Audio paths that need 44.1k must run
// in a separate engine instance at a different cycle period; this engine is
// AVB-first by design.

template <size_t SampleRateHz>
struct EngineConstants
{
    static constexpr size_t sample_rate_hz = SampleRateHz;
    static constexpr size_t samples_per_cycle = SampleRateHz / 6000;
    static constexpr size_t cycles_per_leg = 32;
    static constexpr size_t samples_per_leg = samples_per_cycle * cycles_per_leg;

    static constexpr float recip_samples_per_leg_f = 1.0F / static_cast<float>(samples_per_leg);
    static constexpr double recip_samples_per_leg_d = 1.0 / static_cast<double>(samples_per_leg);

    // Cycle period in nanoseconds, exact for any 6000-multiple sample rate.
    static constexpr uint64_t cycle_period_ns = 1'000'000'000ULL / 6000ULL;

    static_assert(
        SampleRateHz % 6000 == 0,
        "SampleRate must be a multiple of 6000 Hz so that one cycle "
        "is an integer number of samples (SR/6000)");
    static_assert(
        samples_per_cycle > 0 && (samples_per_cycle & (samples_per_cycle - 1)) == 0,
        "samples_per_cycle must be a power of two for SIMD alignment");
    static_assert(
        (samples_per_leg & (samples_per_leg - 1)) == 0,
        "samples_per_leg must be a power of two so the compile-time "
        "reciprocal is exactly representable in float");
};

using Engine48k = EngineConstants<48000>;
using Engine96k = EngineConstants<96000>;
using Engine192k = EngineConstants<192000>;

}  // namespace statusbar::engine
