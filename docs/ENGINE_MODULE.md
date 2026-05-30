[← back to module index](README.md)

# engine

A tiered real-time DSP data-flow framework: small, regular Elements
(gain, biquad, mixer, meter) driven by a sample-accurate inner loop,
fed parameter updates from a slower control tier via lock-free pipes,
with per-leg linear-ramp smoothing and freeze-on-miss when the
producer is late. The same shape repeats at every tier — only the
rate changes.

## Overview

The engine is a self-similar **Element / Segment / Scheduler** hierarchy.
Each `Element` is a regular type (no inheritance, no vtables) that
satisfies the `IsElement` concept: compile-time `level`,
`RealtimePolicy`, four associated types (`data_in`, `data_out`,
`ctrl_in`, `ctrl_out`), and a `process_cycle` member. Schedulers compose
Elements by static type — there are no virtual calls on the audio path.

Time is quantized into two compile-time units in `engine_constants.hpp`:
a **cycle** of `SR / 6000` samples (8 at 48 kHz, 16 at 96 kHz, 32 at
192 kHz; period ≈ 166.667 µs at every supported rate) and a **leg** of
32 cycles (~5.333 ms). The 6000 Hz divisor restricts sample rates to
AVB-family multiples — the 44.1 kHz family is intentionally unsupported
because it would make `samples_per_cycle` non-integer. Per-cycle and
per-leg counts are always powers of two so SIMD lanes divide them
exactly.

Parameter updates use a **coeffs + designer + element** trio that
repeats for biquad and gain:

- `*Coeffs` is the per-sample runtime representation Tier 0 consumes
  (pole/zero pair for biquad, scalar amplitude for gain). Every
  `Coeffs` type satisfies `SegmentableCoeffs` — pointwise `+`, `-`,
  `* scalar`.
- `*Designer` runs on Tier 1 (soft-RT, ~187.5 Hz leg rate). Once per
  leg it pulls the latest target from an `itc::LatestPipe`, computes a
  new endpoint coefficient (running expensive math like
  `dsp::design_biquad_pole_zero`), wraps the previous-endpoint→new-
  endpoint transition in a `Segment<Coeffs>`, and publishes it on an
  outbound `LatestPipe`.
- `*Element` runs on Tier 0 (hard-RT, audio rate). The element wraps
  `SmoothedElement<SR, T, Coeffs, Apply>`, which holds the active
  `Segment`, increments `current_coeffs += segment.delta` once per
  sample, applies the cheap `Apply` functor (multiply for gain;
  cascaded first-order complex section for biquad), and at each leg
  boundary tries to consume the next `Segment`. If none has been
  published it freezes at `active.end` (zero delta, hold) — the
  freeze-on-miss discipline.

The control-thread → audio-thread carrier is
`itc::LatestPipe<Segment<Coeffs>>`, an SPSC primitive backed by three
slots and a generation counter — a lock-free triple-buffer swap that is
wait-free on both sides and always overwrites stale data. There is no
queue, so a slow audio thread cannot back-pressure the control thread
and a slow control thread cannot stall the audio thread.

`GainMatrix<NIn, NOut, SR, T>` applies the same shape to a crosspoint
mixer: a single `MatrixCoeffs` rides one `Segment` per leg, the per-cell
delta accumulates in a flat row-major array, and a per-output inner
reduction produces the mixed channels. `Meter<SR, T>` is a passthrough
that taps a signal point — copies input to output unchanged while
accumulating per-sample `peak = max(peak, |x|)` and sum-of-squares, then
at each leg boundary publishes a `Measurement<T>` (peak + RMS) upward
via its `ctrl_out` `LatestPipe`.

The bundled scheduler is deliberately minimal: `drive_cycles()` runs a
single Element for N cycles. Richer topologies (graphs, mixed-rate
sub-trees) are composed by the caller on top of this primitive.

## Key types

- `IsElement` — concept; static `level`, `realtime_policy`, the four
  type aliases, `process_cycle`.
- `RealtimePolicy` — `HardRT` (a miss is a bug) vs `BestEffort`
  (downstream freezes on miss).
- `EngineConstants<SR>` — `samples_per_cycle`, `cycles_per_leg`,
  `samples_per_leg`, `recip_samples_per_leg_f/d`, `cycle_period_ns`.
  `Engine48k` / `Engine96k` / `Engine192k` aliases.
- `Segment<Coeffs>` — `{start, end, delta}` triple describing one leg's
  ramp; `make_segment()` and `hold_segment()` constructors.
- `SegmentableCoeffs` — concept requiring pointwise `+`, `-`, `* float`.
- `SmoothedElement<SR, T, Coeffs, Apply>` — generic Tier 0 wrapper
  implementing the per-sample accumulate / per-leg snap discipline.
- `BiquadComplexCoeffs<T>` — pole/zero coefficient pair (two cascaded
  first-order complex sections).
- `BiquadApply<T>` / `BiquadElement<SR, T>` — Tier 0 biquad alias for
  `SmoothedElement<SR, T, BiquadComplexCoeffs<T>, BiquadApply<T>>`.
- `BiquadChainCoeffs<N, T>` / `BiquadChain<N, SR, T>` — N cascaded
  biquads as one element with one `Segment` covering all stages.
- `BiquadDesigner<T>` — Tier 1 producer; per-channel
  `dsp::BiquadDesignParams` inputs, 3-leg morph (1-leg snap on
  filter-type change), publishes biquad segments.
- `GainAmplitudeCoeffs<T>` / `GainApply<T>` / `GainElement<SR, T>` —
  Tier 0 gain.
- `GainDesigner<T>` — Tier 1 1-leg-ramp producer; per-channel
  `LatestPipe<float>` inputs, one combined gain segment out.
- `MatrixCoeffs<NIn, NOut, T>` / `GainMatrix<NIn, NOut, SR, T>` —
  Tier 0 fixed-size crosspoint mixer.
- `Measurement<T>` / `Meter<SR, T>` — Tier 0 passthrough emitting
  per-leg peak + RMS.
- `BodePoint`, `evaluate_z_domain`, `bode_point_at_frequency` —
  off-thread frequency-response evaluation in double precision.
- `drive_cycles(element, in, out, cycle_count)` — single-element
  scheduler.

## Quick example

```cpp
#include "statusbar/engine/engine.hpp"

#include <array>

using namespace statusbar::engine;
using namespace statusbar::itc;

int main()
{
    constexpr size_t SR = 48000;
    using Consts = EngineConstants<SR>;

    GainElement<SR>   gain;
    BiquadElement<SR> eq;
    Meter<SR>         meter;

    // Tier 1 → Tier 0 segment pipes + upward telemetry.
    LatestPipe<Segment<GainAmplitudeCoeffs<float>>> gain_pipe;
    LatestPipe<Segment<BiquadComplexCoeffs<float>>> eq_pipe;
    LatestPipe<Measurement<float>>                  meter_pipe;
    gain.connect(gain_pipe);
    eq.connect(eq_pipe);
    meter.connect_telemetry(meter_pipe);

    // Seed initial state so the first leg isn't silent.
    gain.prime_segment(hold_segment(GainAmplitudeCoeffs<float>{1.0F}));
    eq.prime_segment(hold_segment(BiquadComplexCoeffs<float>{}));

    // Run one leg of audio: gain → eq → meter.
    std::array<float, Consts::samples_per_leg> in{}, mid1{}, mid2{}, out{};
    drive_cycles(gain,  in.data(),   mid1.data(), Consts::cycles_per_leg);
    drive_cycles(eq,    mid1.data(), mid2.data(), Consts::cycles_per_leg);
    drive_cycles(meter, mid2.data(), out.data(),  Consts::cycles_per_leg);

    auto const m = meter.last_measurement();
    return (m.peak >= 0.0F) ? 0 : 1;
}
```

## Headers

- `statusbar/engine/engine.hpp` — module header; consumers `#include` this.
- `engine_constants.hpp` — `EngineConstants<SR>` and `Engine48k` / `Engine96k` / `Engine192k`.
- `engine_element.hpp` — `RealtimePolicy`, `IsElement` concept.
- `engine_segment.hpp` — `Segment<Coeffs>`, `SegmentableCoeffs`, `make_segment`, `hold_segment`.
- `engine_smoothed_element.hpp` — `SmoothApply` concept and `SmoothedElement<SR, T, Coeffs, Apply>`.
- `engine_scheduler.hpp` — `drive_cycles()`.
- `engine_biquad_coeffs.hpp` — `BiquadComplexCoeffs<T>` plus `BodePoint` / `evaluate_z_domain` / `bode_point_at_frequency`.
- `engine_biquad_element.hpp` — `BiquadApply<T>` and the `BiquadElement<SR, T>` alias.
- `engine_biquad_chain.hpp` — `BiquadChainCoeffs<N, T>`, `BiquadChainApply<N, T>`, the `BiquadChain<N, SR, T>` alias, and chain Bode evaluators.
- `engine_biquad_designer.hpp` — `BiquadDesigner<T>` Tier 1 producer (3-leg morph, 1-leg snap on type change).
- `engine_gain_coeffs.hpp` — `GainAmplitudeCoeffs<T>` and operators.
- `engine_gain_element.hpp` — `GainApply<T>` and the `GainElement<SR, T>` alias.
- `engine_gain_designer.hpp` — `GainDesigner<T>` Tier 1 producer.
- `engine_gain_matrix.hpp` — `MatrixCoeffs<NIn, NOut, T>` and `GainMatrix<NIn, NOut, SR, T>`.
- `engine_meter.hpp` — `Measurement<T>` and `Meter<SR, T>` passthrough.

## Dependencies

- **Statusbar modules:** [`dsp`](DSP_MODULE.md) — `ComplexFirstOrderCoeffs`, `BiquadDesignParams`, `design_biquad_pole_zero`, `simd_flattened_size`, `set_flattened_item` / `get_flattened_item`, `mul`, `zero`, SIMD vector types. `core::itc` — `LatestPipe<T>` (the 2-slot `LatestWins` `MessagePipe`) carries every Tier 0 ↔ Tier 1 ↔ Tier 2 hop.
- **System / external:** `<array>`, `<span>`, `<atomic>` (via `itc`), `<cstddef>`, `<cstdint>`, `<concepts>`, `<complex>`, `<cmath>`, `<numbers>`, `<utility>`, `<algorithm>`.

## Notes & caveats

- **RT-safety on the audio path.** `process_cycle`, `drive_cycles`, `prime_segment`, and `LatestPipe::try_consume` are all wait-free and allocation-free. Tier 0 elements never touch the heap.
- **Parameter-update protocol.** Tier 1 → Tier 0 hops go through `itc::LatestPipe<Segment<Coeffs>>` — a 2-slot SPSC pipe backed internally by three slots and a generation counter; `publish()` is an atomic-swap of the producer's staging slot with the ready slot, and `try_consume()` swaps the ready slot with the consumer's slot. Stale publishes are overwritten in place; there is no queue and no back-pressure. The Tier 0 element samples the pipe **once per leg boundary** — within a leg the active segment does not change.
- **Smoothing is per-sample inside one leg.** `current_coeffs = current_coeffs + active.delta` runs every sample; at the boundary the element either snaps to the next segment's `start` (a deliberate discontinuity if `start != previous_end`) or freezes (`hold_segment(active.end)`, delta zero) when the producer was late.
- **Scheduler granularity.** `drive_cycles` is block-based at the cycle level (8 / 16 / 32 samples). Parameter changes apply at leg boundaries (256 / 512 / 1024 samples). The scheduler is not sample-accurate for parameter updates.
- **AVB-only sample rates.** The 6000 Hz divisor in `EngineConstants` rejects the 44.1 kHz family at `static_assert` time. Audio paths that need 44.1k must run in a separate engine instance.
- **SIMD via the sample type.** Every element is templated on `T`. Set `T = simd_float32x4` (or nested `SIMDVec`) to process 4 / 8 / N parallel channels in one element; coefficients are packed lane-wise. `Meter` rejects nested SIMD `T` at instantiation time.
- **Initial state.** `GainElement` default-constructs to amplitude zero — outputs silence until a `Segment` arrives or `prime_segment` is called. `BiquadDesigner` publishes a `hold_segment` on its first `step()` after `set_initial()`; until then it is silent.
- **Designer morph windows.** `BiquadDesigner` uses a 3-leg linear morph between two same-type parameter sets and a 1-leg snap on filter-type change (which would otherwise pass through unstable intermediate filters). `GainDesigner` is a 1-leg ramp — the minimum fade is the leg duration itself (~5.333 ms).
- **`BiquadChain` vs cascaded `BiquadElement`s.** `BiquadChain<N>` rides one `Segment` covering all N stages, so the chain morphs in lock-step with one publish per leg. Cascading separate `BiquadElement`s gives independent per-stage control at the cost of one pipe and one publish per stage.
- **Pole/zero, not TDF II.** Biquads are ramped in pole/zero form because the stable region is convex — linear interpolation between two stable filters stays stable along the entire chord, and intermediate filters look like "real filters at intermediate parameters."
- **Meter timing.** Peak is per-leg maximum (no decay across leg boundaries); RMS is sqrt(mean-of-squares) over exactly one leg. No built-in hold or release — apply those in the consumer.
- **Self-validation.** `Segment` stores `start`, `end`, and `delta` redundantly so Tier 1 can assert `current ≈ start` at a leg boundary to catch missed publishes or miscounts.

## Further reading

- [`dsp`](DSP_MODULE.md) — biquad design math, complex first-order primitives, SIMD vector types and helpers used by every Coeffs and Apply in this module.
- [`audio`](AUDIO_MODULE.md) — audio-thread plumbing and sample buffers that drive `process_cycle` from the host I/O callback.
