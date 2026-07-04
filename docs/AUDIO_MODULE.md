[← back to module index](README.md)

# audio

Cross-platform low-latency audio I/O: device enumeration, input and
output streams, sample-format conversion, lock-free ring and
presentation-time queues for bridging clock domains, BW64 file capture,
and a mock backend for tests. Native backends are CoreAudio HAL on
macOS and ALSA on Linux; everything above the backend is shared.

## Overview

The module header pulls in the common types, the device manager, the
abstract `OutputStream` / `InputStream` interfaces, the provider /
factory hooks, the SPSC ring and presentation queues, the BW64 writer,
the sample-format converters, and the timing / config helpers. The
platform-specific backend header (`audio_device_darwin.hpp` or
`audio_device_linux.hpp`) is selected by `__APPLE__` / `__linux__` and
supplies the concrete stream implementations behind PIMPL pointers.

Three layers cooperate:

1. **Platform backend.** On macOS, `OutputStreamDarwin` / `InputStreamDarwin`
   drive an `AudioUnit` (`kAudioUnitSubType_HALOutput`) through the
   Audio HAL; the callback fires on a CoreAudio real-time thread the
   system manages. On Linux, `OutputStreamLinux` / `InputStreamLinux`
   open an ALSA PCM device (`snd_pcm_*`) and run a dedicated thread
   whose priority follows `AudioConfig::thread_priority`. Both backends
   negotiate hardware parameters in `start()`, so the `effective_*`
   accessors are only meaningful after `start()` and may differ from what
   was requested (before `start()` they report defaults).
2. **Provider abstraction.** `IDeviceProvider` / `IStreamFactory` and the
   RAII `ProviderGuard` exist for dependency injection, but the current
   `OutputStream::create` / `InputStream::create` construct the platform
   backend directly rather than routing through the factory. Tests that
   need to run without hardware instantiate `MockOutputStream` /
   `MockInputStream` directly.
3. **In-callback helpers.** `AudioRing<T>` and `AudioPresentationQueue<T>`
   are SPSC, lock-free, and pre-sized via `std::pmr::memory_resource`
   so the audio thread never allocates. `AudioRing` is FIFO for
   bridging two clock domains; `AudioPresentationQueue` is time-indexed
   for AVB/TSN-style packet streams that arrive out of order with
   presentation timestamps.

Sample data is **non-interleaved float32 by default**
(`AudioConfig::non_interleaved = true`, `format = SampleFormat::Float32`).
The callback receives one `InputAudioBuffer<T>` / `OutputAudioBuffer<T>`
per channel, each wrapping a per-channel span; `audio_convert.hpp`
handles conversion to / from `Int16` / `Int32` when hardware needs it.
`AudioCallback<T>` is a `sg14::inplace_function<…, 64>` returning
`Status` — captures larger than 64 bytes won't fit; `failure()` stops
the stream.

## Key types

- `AudioConfig` — sample rate, channel count, sample format, period (`buffer_frames`), interleaving flag, and `ThreadPriority`. Built in `audio_types.hpp`; validated by helpers in `audio_config.hpp`.
- `SampleFormat` / `SampleRate` / `ThreadPriority` — strongly-typed enums for the supported formats (`Float32`, `Int16`, `Int32`), the four standard rates (44.1 / 48 / 96 / 192 kHz), and the three thread-priority levels (`Normal`, `Elevated`, `Realtime`).
- `DeviceInfo` — name, UID, channel counts, supported sample rates, default-input/output flags.
- `InputAudioBuffer<T>` / `OutputAudioBuffer<T>` / `AudioCallbackParams<T>` / `AudioCallback<T>` — per-channel span wrappers, the structured callback parameter pack (`stream_time` included), and the `inplace_function<Status(AudioCallbackParams<T> const&), 64>` callback type. `Float` aliases drop the template parameter.
- `DeviceManager` — static façade for `enumerate_devices()`, `default_input_device()`, `default_output_device()`, `find_device()`.
- `OutputStream` / `InputStream` — abstract stream interfaces with `create()` / `start()` / `stop()` / `is_running()` / `config()` / `stream_time()` / `device()` and `effective_sample_rate()` / `effective_period_frames()` / `effective_buffer_frames()`.
- `IDeviceProvider` / `IStreamFactory` / `ProviderGuard` — pluggable provider interfaces plus an RAII guard for temporarily swapping them.
- `AudioRing<T>` (+ `AudioRingBase`) — SPSC lock-free non-interleaved ring buffer with `push()` / `pop()` / `peek()` / `skip()` / `available_read()` / `available_write()`; storage comes from a `std::pmr::memory_resource`.
- `AudioPresentationQueue<T>` (+ `AudioPresentationQueueBase`) — SPSC lock-free time-indexed queue. `write()` deposits samples at a presentation time, `read()` consumes a window starting at `center_time()` and advances; missing slots read as zero.
- `Bw64Writer` — streaming float32 RIFF/WAVE writer with a BWF `bext` chunk; `open()` / `write_samples()` / `close()`.
- `AudioError` / `AudioErrorCategory` / `audio_error_category()` / `make_error_code()` — error enum registered as `std::is_error_code_enum`.
- `MockDeviceProvider` / `MockOutputStream` / `MockInputStream` / `MockStreamFactory` / `make_mock_device()` / `make_default_mock_provider()` — test doubles that simulate periodic callbacks on their own thread, capture output, optionally inject errors, and feed pre-recorded or generated input (e.g. `generate_sine_wave()`).

## Quick example

```cpp
#include "statusbar/audio/audio.hpp"

#include <atomic>
#include <chrono>
#include <numbers>
#include <print>
#include <thread>

using namespace statusbar;
using namespace statusbar::audio;

int main()
{
    AudioConfig const cfg{
        .sample_rate = SampleRate::Rate_48000,
        .channels = 2,
        .format = SampleFormat::Float32,
        .buffer_frames = 256,
    };

    auto stream_res = OutputStream::create("default", cfg);
    if (!stream_res) {
        std::println(stderr, "create failed: {}", stream_res.error().message());
        return 1;
    }
    auto stream = std::move(*stream_res);

    // Non-interleaved float32; one OutputAudioBuffer per channel.
    double phase = 0.0;
    double const step = 2.0 * std::numbers::pi * 440.0 / 48000.0;
    auto cb = [&phase, step](AudioCallbackParamsFloat const& p) -> Status {
        auto const frames = p.output_buffers[0].sample.size();
        for (size_t i = 0; i < frames; ++i) {
            float const s = static_cast<float>(0.2 * std::sin(phase));
            phase += step;
            for (auto const& ch : p.output_buffers) {
                ch.sample[i] = s;
            }
        }
        return success();  // failure() would stop the stream
    };

    if (auto st = stream->start(std::move(cb)); !st) {
        std::println(stderr, "start failed: {}", st.error().message());
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::seconds{2});
    stream->stop();
}
```

## Headers

- `statusbar/audio/audio.hpp` — module header; consumers `#include` this.
- `statusbar/audio/audio_types.hpp` — `SampleFormat`, `SampleRate`, `ThreadPriority`, `AudioConfig`, `DeviceInfo`, `InputAudioBuffer<T>` / `OutputAudioBuffer<T>`, `AudioCallbackParams<T>`, `AudioCallback<T>`.
- `statusbar/audio/audio_error.hpp` — `AudioError` enum, category, `make_error_code()`.
- `statusbar/audio/audio_config.hpp` — `validate_*` / `clamp_buffer_frames()` / `bytes_per_sample()` / `bytes_per_frame()` plus min/max channel and buffer constants.
- `statusbar/audio/audio_convert.hpp` — `clamp_sample()` plus scalar, span, and pointer converters between float and `s16` / `s32`.
- `statusbar/audio/audio_timing.hpp` — frames-to-time / latency / period helpers and `sample_rates::*` constants.
- `statusbar/audio/audio_device.hpp` — `DeviceManager` static façade for device enumeration / lookup.
- `statusbar/audio/audio_stream.hpp` — `OutputStream` / `InputStream` abstract base classes and `create()` factory entry points.
- `statusbar/audio/audio_provider.hpp` — `IDeviceProvider`, `IStreamFactory`, `get_*` / `set_*` provider hooks, `reset_providers()`, `ProviderGuard`.
- `statusbar/audio/audio_ring.hpp` — `AudioRingBase`, `AudioRing<T>` SPSC FIFO ring buffer.
- `statusbar/audio/audio_presentation_queue.hpp` — `AudioPresentationQueueBase`, `AudioPresentationQueue<T>` time-indexed SPSC queue.
- `statusbar/audio/audio_bw64_writer.hpp` — `Bw64Writer` (BWF / RIFF WAVE writer with `bext` metadata).
- `statusbar/audio/audio_mock.hpp` — `MockDeviceProvider`, `MockOutputStream`, `MockInputStream`, `MockStreamFactory`, `make_mock_device()`, `make_default_mock_provider()`.
- `statusbar/audio/audio_device_darwin.hpp` — macOS backend: `DeviceManagerDarwin`, `OutputStreamDarwin`, `InputStreamDarwin` (included only when `__APPLE__`).
- `statusbar/audio/audio_device_linux.hpp` — Linux backend: `DeviceManagerLinux`, `OutputStreamLinux`, `InputStreamLinux` (included only when `__linux__`).

## Dependencies

- **Statusbar modules:** `status` from `statusbar-core` (`Status` / `StatusValue<T>` thread through `create()` / `start()` / validation / BW64 / convert), `itc` from `statusbar-core` (`itc::Published<double>` exposes `stream_time` from the mock streams), `sg14` from `statusbar-core` (`inplace_function` backs `AudioCallback<T>`); `dsp` from `statusbar-dsp` (`dsp::lround` in `audio_timing.hpp`, `dsp::zero` in `audio_presentation_queue.hpp`).
- **System / external:**
  - macOS: `AudioUnit.framework`, `AudioToolbox.framework`, `CoreAudio.framework`, `CoreFoundation.framework`.
  - Linux: `libasound2` (ALSA).
  - C++23 standard library: `<atomic>`, `<thread>`, `<span>`, `<memory_resource>`, `<chrono>`, `<system_error>`, `<expected>`, `<cstdio>`, `<numbers>`.

## Notes & caveats

- **Callback is real-time.** On both backends the audio callback runs on a thread that must not block, allocate, take locks, log to stdio, or call APIs that page-fault. `AudioCallback<T>` caps capture state at 64 bytes (`inplace_function<…, 64>`); spilling allocates and will fail to compile-into the slot.
- **Non-interleaved float32 by default.** `AudioConfig::non_interleaved` is `true` and `format` is `SampleFormat::Float32`; the callback receives one `InputAudioBuffer<T>` / `OutputAudioBuffer<T>` per channel, each holding a per-channel `std::span`. Other formats require conversion via `audio_convert.hpp` before / after the callback.
- **`start()` / `stop()` are not thread-safe with each other.** Serialize from one thread (typically the owner). All read-only accessors are safe concurrently. `stop()` is idempotent.
- **`effective_*` may differ from `config()`.** Both ALSA and CoreAudio negotiate the actual rate, period, and buffer size with hardware. Use `effective_sample_rate()` / `effective_period_frames()` / `effective_buffer_frames()` after `start()` if any downstream code depends on the real values (they return placeholder defaults before `start()` negotiates).
- **Thread priority is best-effort.** `ThreadPriority::Elevated` tries SCHED_FIFO and silently falls back to normal if the process lacks `CAP_SYS_NICE`; `ThreadPriority::Realtime` requests SCHED_FIFO on the audio thread after `start()` returns; on Linux, if it cannot obtain RT priority the thread stops the stream rather than failing the original `start()` call. On macOS the CoreAudio thread is system-managed.
- **`AudioRing` and `AudioPresentationQueue` are strictly SPSC.** One producer thread for `push()` / `write()`, one consumer for `pop()` / `read()` / `advance()`; `available_*` / `center_time()` / `can_*` are safe from either side. Storage comes from a `std::pmr::memory_resource` — pass an arena-backed one to guarantee no heap activity in the callback. `AudioRing::push` of fewer channels than the ring holds fills the unspecified channels with silence (write_pos advances for all channels, so they would otherwise expose stale wrapped samples). `AudioPresentationQueue::read` zeros each slot as it consumes it, so late / missing samples appear as silence rather than as data from a previous wrap.
- **`AudioCallback` return value.** Returning `failure()` from the callback signals the stream to stop. The CoreAudio thread will not call again; the ALSA loop drops out after the current period.
- **Mocks run a real thread.** `MockOutputStream` / `MockInputStream` spawn a `std::thread` that calls the user callback every `callback_frames` worth of simulated time. Install them through `ProviderGuard` so the swap is undone on destruction.
- **`Bw64Writer` is RIFF, not RF64.** Files larger than 4 GiB are rejected; the RF64 `ds64` extension is not implemented. Samples are channel-interleaved float32 even though stream callbacks are non-interleaved — convert on the way to the writer. `clamp_sample()` maps NaN to `0.0F`; the float-to-int converters rely on it, since propagating NaN through `static_cast<int32_t>` is undefined behaviour.

## Further reading

- [LTC_DESIGN.md](LTC_DESIGN.md) — design notes for the linear-timecode reader/writer built on top of this module.
