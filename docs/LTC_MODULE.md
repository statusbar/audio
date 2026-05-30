[← back to module index](README.md)

# ltc

SMPTE Linear Timecode generation and playback — biphase-mark encoding,
drop-frame and non-drop-frame rates, and an audio-clock servo for keeping
generated samples in sync with an external reference clock.

## Overview

SMPTE 12M Linear Timecode packs HH:MM:SS:FF position into an 80-bit
frame emitted as a biphase-mark (Manchester) audio signal at 80 bits
per video frame. The module produces that signal as PCM `float`
samples at standard audio sample rates, suitable for writing to a file
or feeding an audio output stream.

The module is split into five small headers along the obvious seams:

- `ltc_timecode` — `Timecode` value type, `FrameRate` enum, and
  rate-aware arithmetic including drop-frame skip handling and
  HH:MM:SS:FF / HH:MM:SS;FF parse/format.
- `ltc_frame` — `LTCFrame`, the 80-bit packed wire form (BCD time
  fields, user bits, drop-frame and color flags, parity bit, sync
  word `0x3FFD`).
- `ltc_generator` — `Generator`, which biphase-mark-encodes one
  `LTCFrame` into exactly `samples_per_frame()` samples with
  configurable SMPTE 12M rise/fall time.
- `ltc_servo` — `ServoGenerator`, a continuous-stream wrapper around
  `Generator` that runs a small PI controller and resamples the
  per-frame buffer to compensate for drift between the audio clock and
  an external reference.
- `ltc_playback` — audio-callback adapters that drive either
  `ServoGenerator` (wall-clock-locked) or `Generator` (fixed-duration)
  from an `audio::AudioCallbackParamsFloat` callback.

All seven SMPTE rates are supported via the `FrameRate` enum: 23.976,
24, 25, 29.97 DF, 29.97 NDF, 30 DF, and 30 NDF. Drop-frame variants
follow the standard rule (skip frames 0 and 1 at the start of each
minute except 0, 10, 20, 30, 40, 50). The `Generator::SampleRate` enum
covers 44.1 kHz, 48 kHz, and 96 kHz. For SMPTE 12M background, the
per-bit layout of the 80-bit frame, and the rationale behind the servo
and rise/fall shaping, see the [LTC design doc](LTC_DESIGN.md).

## Key types

- `FrameRate` — enum of the seven SMPTE rates plus free helpers
  `frames_per_second`, `is_drop_frame`, `actual_frame_rate`,
  `frame_rate_name`, `parse_frame_rate`.
- `Timecode` — `{hours, minutes, seconds, frames, user_bits, rate, color_frame}` with `is_valid`,
  `increment_frame` / `decrement_frame`, `to_frame_count` /
  `from_frame_count`, `to_string` / `from_string`, `from_realtime`, and
  `operator<=>` (compares by frame count).
- `LTCFrame` — immutable 80-bit packed view of a `Timecode`; queried
  via `get_bit(pos)` or `data()` (10-byte array). Encodes BCD time
  fields, user bits, drop-frame and color flags, polarity-correction
  parity bit, and the `0x3FFD` sync word.
- `Generator::SampleRate` — `Rate_44100`, `Rate_48000`, `Rate_96000`.
- `Generator` — turns one `Timecode` into one buffer of biphase-mark
  samples; carries sample-rate, frame-rate, and rise-time
  configuration; exposes `samples_per_frame()` and `samples_per_bit()`.
- `ServoGenerator` — continuous-stream variant; owns a `Generator`,
  tracks the current `Timecode`, accepts `update_timing(actual,
  expected)` calls, and emits samples at a slightly adjusted rate via
  fractional-position interpolation.
- `ServoPlaybackState` / `process_servo_callback` and
  `FixedPlaybackState` / `process_fixed_callback` — state structs and
  callbacks that adapt the generator/servo to the `audio` module's
  `AudioCallbackParamsFloat` signature.
- `interpolate_sample(buffer, position)` — linear-interpolation helper
  used by the servo; returns `0.0f` for out-of-range positions.
- `parse_time_offset`, `get_local_time_info`, `get_realtime_timestamp`,
  `LocalTimeInfo` — wall-clock helpers used by realtime-clock playback.

## Quick example

```cpp
#include "statusbar/ltc/ltc.hpp"

#include <vector>

using namespace statusbar;

int main()
{
    // 48 kHz audio, 30 fps non-drop, default 40 us rise/fall time.
    ltc::Generator gen{
        ltc::Generator::SampleRate::Rate_48000,
        ltc::FrameRate::Rate_30_NDF};

    ltc::Timecode tc{
        .hours = 1, .minutes = 30, .seconds = 0, .frames = 15,
        .rate = ltc::FrameRate::Rate_30_NDF};

    // Render one video frame (1600 samples at 48 kHz / 30 fps).
    std::vector<float> samples;
    samples.reserve(gen.samples_per_frame());
    gen.generate_frame(tc, samples);

    // Advance to the next frame for the next call.
    tc.increment_frame();
    return samples.empty() ? 1 : 0;
}
```

## Headers

- `statusbar/ltc/ltc.hpp` — module header; consumers `#include` this. Also exposes a `version` constant.
- `statusbar/ltc/ltc_timecode.hpp` — `FrameRate` enum, `Timecode` struct, rate predicates, parse/format helpers.
- `statusbar/ltc/ltc_frame.hpp` — `LTCFrame`, the 80-bit packed encoding of a `Timecode`.
- `statusbar/ltc/ltc_generator.hpp` — `Generator` (one frame → audio samples) and its `SampleRate` enum.
- `statusbar/ltc/ltc_servo.hpp` — `ServoGenerator` (continuous samples with drift compensation) and the `interpolate_sample` helper.
- `statusbar/ltc/ltc_playback.hpp` — `ServoPlaybackState` / `FixedPlaybackState`, their atomic base classes, `process_servo_callback` / `process_fixed_callback`, and wall-clock helpers (`parse_time_offset`, `get_local_time_info`, `get_realtime_timestamp`, `LocalTimeInfo`).

## Dependencies

- **Statusbar modules:** [`audio`](AUDIO_MODULE.md) (`audio::AudioCallbackParamsFloat`, `audio::AudioError` — only via `ltc_playback.hpp`), `core::status` (`Status` / `success` / `failure` returned by the playback callbacks).
- **System / external:** `<array>`, `<atomic>`, `<chrono>`, `<cmath>`, `<compare>`, `<cstdint>`, `<ctime>`, `<expected>`, `<format>`, `<optional>`, `<span>`, `<string>`, `<string_view>`, `<system_error>`, `<vector>` from the C++23 standard library. `localtime_r` (POSIX) / `localtime_s` (Windows) for wall-clock conversion.

## Notes & caveats

- Sample output is always 32-bit `float` in `[-1.0, +1.0]`. No
  integer-PCM mode; convert at the call site if you need one.
- `Generator::generate_frame` clears and refills the caller's
  `std::vector<float>` each call — pre-`reserve(samples_per_frame())`
  and reuse the same vector so the allocation stays put. The
  `push_back` path makes the first call non-RT-safe.
- `samples_per_frame()` is `lround(sample_rate / actual_frame_rate)`
  and varies subtly between e.g. 29.97 and 30 fps. The generator pads
  or truncates per-frame output to that exact count; fractional bit
  timing is absorbed inside the frame.
- Rise/fall time is clamped to the SMPTE 12M range of 25–250 µs at
  construction; out-of-range arguments are silently clamped.
- Drop-frame rules are encoded in `Timecode::is_drop_frame_skip` and
  applied by `increment_frame` / `decrement_frame` / `to_frame_count` /
  `from_frame_count`. `Timecode::from_string` accepts both `:` and `;`
  as the final separator (the `;` convention indicates drop-frame in
  display form) but validates **format only** — call `is_valid()`
  afterwards to enforce range and drop-skip rules.
- The servo corrects drift by adjusting the **fractional read position**
  through the per-frame sample buffer (linear interpolation via
  `interpolate_sample`), **not** by inserting or dropping whole samples.
  The `phase_adjustment` factor is clamped to `[0.999, 1.001]` (≈ ±0.1 %,
  ≈ ±1000 ppm) — fine for normal audio-vs-wall-clock drift, not for
  large step corrections. `update_timing` is a fixed-gain PI controller
  (`kp = 0.01`, `ki = 0.001`); the integral term accumulates without
  leak, so `reset()` if the timing source becomes unreliable.
- `process_servo_callback` re-pins the timecode to the wall clock at
  most once per second and refills the output buffer from
  `ServoGenerator::get_samples` every callback. It returns
  `failure(AudioError::StreamNotRunning)` once `set_running(false)` is
  called — the audio layer's stop signal.
- `process_fixed_callback` zero-fills the rest of the current callback
  when it runs out of frames and returns
  `failure(AudioError::StreamNotRunning)`; check `is_finished()` to
  distinguish clean completion from a real error.
- `ServoPlaybackState` and `FixedPlaybackState` hold **non-owning**
  pointers to their generator — the state must not outlive it.

## Further reading

- [LTC design doc](LTC_DESIGN.md) — SMPTE 12M background, full 80-bit
  bit-by-bit layout, rise/fall-time rationale, command-line tool usage,
  and the testing strategy.
- [`audio`](AUDIO_MODULE.md) — `AudioCallbackParamsFloat`, `AudioError`,
  and the audio-stream callback contract that `ltc_playback` plugs into.
