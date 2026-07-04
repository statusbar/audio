[← back to module index](README.md)

# dsp

Real-time DSP primitives: biquad filters (standard and complex-factored),
smoothed gain, sinusoidal oscillator, level/dB conversions, filter design
math, and a header-only SIMD vector abstraction (`SIMDVec<T, N>`) with
compile-time backend specializations for NEON on arm64 and AVX on x86.

## Overview

The module splits cleanly into two layers. The first is a set of **scalar
processing primitives** — `BiQuad<T>` (transposed direct form II),
`ComplexBiQuad<T>` (two cascaded complex first-order stages), `Gain<T>`
with exponential smoothing, and `Oscillator<T>` (sine via two-pole
resonator) — each templated over the sample type. Every primitive is a
plain struct with `Coeffs` and `State` sub-structs and an
`operator()(T) noexcept` that processes one sample. They never allocate,
take no locks, and are intended to run on the audio thread.

The second layer is the **SIMD vector abstraction** under `dsp_vec_*`.
`SIMDVec<T, N>` is the primary template (a portable scalar fallback in
`dsp_vec_base.hpp`); each backend header specializes the template for one
(T, N) pair behind a `#if defined(__ARM_NEON)` or `#if defined(__AVX__)`
guard, so the compiler picks the right intrinsics implementation from the
build target's `-march`/`-mcpu` flags. There is no runtime dispatch — on
a non-NEON, non-AVX build all four type aliases (`simd_float32x4`,
`simd_float32x8`, `simd_float64x2`, `simd_float64x4`) resolve to the
scalar fallback. The scalar primitives are templated over `T`, so
substituting a SIMD type processes that many channels per call.

A third, smaller layer is **filter design**: `dsp_filter_design.hpp`
provides `design_lowpass`/`_highpass`/`_bandpass`/`_notch`/`_peak`/
`_lowshelf`/`_highshelf` as free functions returning `BiquadCoeffsF64`
(direct-form coefficients in double). `dsp_biquad_design.hpp` wraps these
with an enum-discriminated `BiquadDesignParams` and returns the
pole/zero-factored `ComplexFirstOrderCoeffs<float>` pair used by
`ComplexBiQuad`. Design math always runs in double for accuracy; results
are cast to whatever the runtime sample type is. Design calls are
typically made off the audio thread (they call `std::tan`, `std::pow`,
etc.); only the per-sample `operator()` is meant to live in the inner
loop.

## Key types

- `BiQuad<T>` — transposed direct form II biquad. Nested `Coeffs` exposes `set()` plus `calculate_{lowpass,highpass,bandpass,notch,peak,lowshelf,highshelf}(FilterParams<>)` and `process_z_domain(z1)` for offline frequency-response queries. `State` holds the two delay taps.
- `ComplexBiQuad<T>` — same surface as `BiQuad` but factored into two complex first-order sections; numerically more stable at extreme `sample_rate / cutoff` ratios. Coefficients populated via `set_from_biquad_coeffs(a0, a1, a2, b1, b2)` or the same `calculate_*` family.
- `ComplexFirstOrderCoeffs<T>` / `ComplexFirstOrderState<T>` — the two halves of a single complex first-order section (real/imag of `a0`, `a1`, `b1`).
- `Gain<T>` — single-pole smoothed scalar gain. `Coeffs::set_amplitude` / `set_amplitude_db` / `set_time_constant(sample_rate, time_in_seconds)` / `set_bypass` / `set_mute`. `State::snap_to_target(coeffs)` jumps without ramping.
- `Oscillator<T>` — two-pole sine resonator. `State::set_frequency(FrequencyParameters<>)` and `set_frequency_note(NoteParameters<>)` (12-TET, configurable A4 and cents detune). `operator()(input)` adds the oscillator into `input` scaled by `Coeffs::amplitude_`.
- `FilterParams<T>` — `sample_rate_recip`, `frequency`, `q` (default 0.707), `gain_db`, `channel`. Shared input to every `design_*` and `calculate_*` call.
- `BiquadCoeffsF64` — `{a0, a1, a2, b1, b2}` returned by the `design_*` free functions.
- `BiquadFilterType` (enum) + `BiquadDesignParams` + `design_biquad_pole_zero(params, sample_rate_recip)` — enum-dispatched API returning a `{stage1, stage2}` pair of `ComplexFirstOrderCoeffs<float>`.
- `SIMDVec<T, N>` — the SIMD vector. Aliases: `simd_float32x4` (NEON / SSE-AVX), `simd_float32x8` (AVX), `simd_float64x2` (NEON / AVX), `simd_float64x4` (AVX). Container interface (`size`, `operator[]`, iterators), arithmetic operators, math (`sqrt`, `sin`, `cos`, `exp`, `log`), reductions (`hsum`, `hmin`, `hmax`), and `splat` / `madd` / `lerp` / `clamp`.
- `simd_size<T>` / `simd_flattened_size<T>` / `simd_flattened_type<T>` / `is_simd<T>` — traits used by the primitives so the same code path serves scalar, 1-D, and nested SIMD sample types.
- `set_flattened_item` / `get_flattened_item` / `zero(v)` / `splat(v, x)` / `apply(r, f, args...)` — element-access and broadcast helpers that recurse into nested `SIMDVec`s.
- `process_in_place`, `process_mix_in_place`, `process`, `process_accumulate` in `dsp_process.hpp` — span-driven block loops that call any object with `operator()(T)` per sample.
- `db_to_amplitude` / `amplitude_to_db` (float and double overloads) in `dsp_levels.hpp`; `generate_sine`, `calculate_rms`, `count_zero_crossings`, `approx_equal`, constexpr `lround` in `dsp_math.hpp`.
- `statusbar::dsp::constants::*` — `pi`, `two_pi`, `sqrt_2`, `ln10`, `recip_48k`, `twenty_div_ln10`, etc., as `constexpr` function templates parameterized on `T`.

## Quick example

```cpp
#include "statusbar/dsp/dsp.hpp"

#include <array>

using namespace statusbar;

int main()
{
    // Design a 1 kHz peaking EQ with +6 dB gain, Q = 1.0 at 48 kHz.
    dsp::BiQuad<float> eq;
    eq.coeffs.calculate_peak(dsp::FilterParams<>{
        .sample_rate_recip = 1.0 / 48000.0,
        .frequency = 1000.0,
        .q = 1.0,
        .gain_db = 6.0,
    });
    eq.reset();

    // Run a block of samples through it.
    std::array<float, 256> block{};  // imagine these came from a buffer
    dsp::process_in_place(eq, std::span<float>{block});

    return 0;
}
```

## Headers

- `statusbar/dsp/dsp.hpp` — module header; consumers `#include` this.
- `statusbar/dsp/dsp_constants.hpp` — `constexpr` math constants (`pi`, `two_pi`, `sqrt_2`, `ln10`, `recip_48k`, …) as function templates.
- `statusbar/dsp/dsp_math.hpp` — `lround`, `approx_equal`, `generate_sine`, `calculate_rms`, `count_zero_crossings`.
- `statusbar/dsp/dsp_levels.hpp` — `db_to_amplitude`, `amplitude_to_db`. Future home for peak / LUFS metering (see header comment).
- `statusbar/dsp/dsp_filter_design.hpp` — `FilterParams<T>`, `BiquadCoeffsF64`, and the `design_*` free functions per filter type.
- `statusbar/dsp/dsp_biquad_design.hpp` — `BiquadFilterType` enum, `BiquadDesignParams`, `design_biquad_pole_zero()` returning the pole/zero-factored coefficient pair.
- `statusbar/dsp/dsp_biquad.hpp` — `BiQuad<T>` (transposed direct form II).
- `statusbar/dsp/dsp_complex_biquad.hpp` — `ComplexBiQuad<T>`, `ComplexFirstOrderCoeffs<T>`, `ComplexFirstOrderState<T>`, and the internal `factor_biquad_to_two_stages` helper.
- `statusbar/dsp/dsp_gain.hpp` — `Gain<T>` smoothed gain element.
- `statusbar/dsp/dsp_oscillator.hpp` — `Oscillator<T>`, `FrequencyParameters<T>`, `NoteParameters<T>`, and the 12-TET note tables.
- `statusbar/dsp/dsp_process.hpp` — `process`, `process_in_place`, `process_mix_in_place`, `process_accumulate` block helpers.
- `statusbar/dsp/dsp_vec.hpp` — header for the SIMD layer; pulls in the base and every backend header.
- `statusbar/dsp/dsp_vec_base.hpp` — primary `SIMDVec<T, N>` template (scalar fallback), trait machinery, free-function arithmetic and reductions.
- `statusbar/dsp/dsp_vec_neon32x4.hpp` / `dsp_vec_neon64x2.hpp` — ARM NEON specializations (active only under `__ARM_NEON`).
- `statusbar/dsp/dsp_vec_avx32x4.hpp` / `dsp_vec_avx32x8.hpp` / `dsp_vec_avx64x2.hpp` / `dsp_vec_avx64x4.hpp` — Intel AVX specializations (active only under `__AVX__`).

## Dependencies

- **Statusbar modules:** none. The `dsp` module is mathematically and lexically self-contained; the audio package declares a `find_package(statusbar-core)` at the package level but the dsp headers and sources include nothing from `statusbar-core`.
- **System / external:**
  - `<cmath>`, `<numbers>`, `<complex>`, `<span>`, `<array>`, `<cstddef>`, `<cstdint>`, `<initializer_list>`, `<type_traits>`, `<utility>`, `<algorithm>` from the C++23 standard library.
  - `<arm_neon.h>` — included only from `dsp_vec_neon*.hpp` when `__ARM_NEON` (or `__ARM_NEON__`) is defined.
  - `<immintrin.h>` — included only from `dsp_vec_avx*.hpp` when `__AVX__` is defined.

## Notes & caveats

- **Real-time and allocation-free.** Every per-sample `operator()` and the `process*` helpers in `dsp_process.hpp` allocate no memory, take no locks, and use no `Status` / exceptions. They are safe to call from the audio callback.
- **Coefficient design is off-thread.** `calculate_*` and the `design_*` free functions call `std::tan`, `std::pow`, `std::sqrt` in double precision. Compute them on a control thread, then publish the resulting `Coeffs` to the audio thread (e.g. via a triple buffer).
- **SIMD backend selection is compile-time.** `SIMDVec<float, 4>` is specialized to NEON `float32x4_t` under `__ARM_NEON`, to AVX `__m128` under `__AVX__`, or to the scalar fallback otherwise. There is no runtime feature detection — set `-march`/`-mcpu` on the target. Backend headers always exist; their contents are wrapped in `#if defined(__ARM_NEON)` / `#if defined(__AVX__)` and become empty translation units on the other arch.
- **Backend coverage is partial.** NEON specializes only `SIMDVec<float, 4>` and `SIMDVec<double, 2>`; AVX adds `SIMDVec<float, 8>` and `SIMDVec<double, 4>`. Any other `(T, N)` uses the portable scalar fallback even on a NEON/AVX build.
- **No denormals control.** The dsp module does **not** set FTZ/DAZ on the FPU; that is the caller's responsibility (typically configured once at audio-thread startup).
- **Block size is the caller's choice.** `dsp_process.hpp` helpers loop over a `std::span` of any length. There is no block-size assumption and no SIMD-width alignment requirement — the SIMD width only matters when you instantiate a primitive over a SIMD `T`, in which case one "sample" already represents `N` lanes / channels.
- **Sample format.** All primitives are floating-point (typically `float` or `double`, or a SIMD vector thereof). Integer sample formats must be converted by the caller before processing.
- **Multi-channel via SIMD.** A `BiQuad<simd_float32x4>` is four independent biquads sharing coefficient layout. `Coeffs::set(..., channel)` uses `set_flattened_item` to write one lane at a time, which lets per-channel `FilterParams` populate the same `Coeffs` object across calls.
- **`Oscillator::operator()` accumulates.** It returns `output * amplitude + input`, mixing into the input sample rather than replacing it — useful with `process_in_place` to add tone into an existing buffer.
- **Adding a new vec backend.** Create a `dsp_vec_<arch><T><N>.hpp`, wrap its body in the appropriate `#if defined(<arch_macro>)`, specialize `SIMDVec<T, N>` against the architecture's intrinsic type, and add the header to `dsp_vec.hpp`. The trait machinery (`simd_size`, `simd_flattened_size`, etc.) picks the specialization up automatically.

## Further reading

- [`audio`](AUDIO_MODULE.md) — the buffer and sample-format types that feed `dsp_process.hpp`.
- [`engine`](ENGINE_MODULE.md) — wires dsp primitives into a runnable audio graph.
