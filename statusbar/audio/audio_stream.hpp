#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Audio Module - Stream Abstraction
// Cross-platform audio stream interface

#include "statusbar/audio/audio_device.hpp"
#include "statusbar/audio/audio_types.hpp"
#include "statusbar/status/status.hpp"

#include <memory>
#include <string_view>

namespace statusbar::audio {

/// Simple usage example:
///
/// Example:
/// ```cpp
/// auto cfg = AudioConfig{.sample_rate = SampleRate::Rate_48000, .channels = 2};
/// auto stream_res = OutputStream::create("default", cfg);
/// if (!stream_res) {
///     return stream_res.error();
/// }
/// auto stream = std::move(*stream_res);
/// // start with a moveable callback (captures move-only state safely);
/// // the callback type is AudioCallback<float> ==
/// // sg14::inplace_function<Status(AudioCallbackParamsFloat const&), 64>.
/// auto cb = [](AudioCallbackParamsFloat const& p) -> Status {
///     // fill all output channels with silence for demo
///     for (auto const& ch : p.output_buffers) {
///         for (auto& s : ch.sample) s = 0.0f;
///     }
///     return success();  // failure() would stop the stream
/// };
/// auto start_res = stream->start(std::move(cb));
/// if (!start_res) {
///     return start_res.error();
/// }
/// // ... use stream ...
/// stream->stop();  // idempotent - safe to call when already stopped
/// ```

/// Audio output stream
///
/// Thread Safety:
/// - start() and stop() are NOT thread-safe. The caller must serialize calls
///   to these methods (e.g., call from the same thread or use external locking).
/// - Once running, the audio callback executes on a separate audio thread.
/// - is_running(), config(), stream_time(), and device() are thread-safe for reads.
class OutputStream
{
  public:
    /// Create output stream for device
    /// @param device_uid Device UID, name, or "default" for system default
    /// @param config Audio configuration
    [[nodiscard]] static auto create(std::string_view device_uid, AudioConfig const& config)
        -> StatusValue<std::unique_ptr<OutputStream>>;

    /// Start audio streaming with callback
    /// @param callback Audio callback function
    /// @note Not thread-safe - do not call concurrently with stop()
    [[nodiscard]] virtual auto start(AudioCallbackFloat&& callback) -> Status = 0;

    /// Stop audio streaming (idempotent - safe to call when already stopped)
    /// @note Not thread-safe - do not call concurrently with start()
    virtual void stop() = 0;

    /// Check if stream is running
    [[nodiscard]] virtual auto is_running() const noexcept -> bool = 0;

    /// Get actual configuration (may differ from requested)
    [[nodiscard]] virtual auto config() const noexcept -> AudioConfig const& = 0;

    /// Get current stream time (seconds since start)
    [[nodiscard]] virtual auto stream_time() const noexcept -> double = 0;

    /// Get effective sample rate (actual hardware/negotiated rate)
    /// May differ from config().sample_rate due to hardware constraints or format negotiation
    [[nodiscard]] virtual auto effective_sample_rate() const noexcept -> double = 0;

    /// Get effective period frames (actual hardware/negotiated period size)
    /// May differ from config().buffer_frames due to hardware constraints
    [[nodiscard]] virtual auto effective_period_frames() const noexcept -> uint32_t = 0;

    /// Get effective buffer frames (actual hardware/negotiated buffer size)
    /// May differ from requested buffer size due to hardware constraints
    [[nodiscard]] virtual auto effective_buffer_frames() const noexcept -> uint32_t = 0;

    /// Get device info
    [[nodiscard]] virtual auto device() const noexcept -> DeviceInfo const& = 0;

    virtual ~OutputStream() noexcept = default;

  protected:
    OutputStream() = default;
    OutputStream(OutputStream const&) = delete;
    auto operator=(OutputStream const&) -> OutputStream& = delete;
    OutputStream(OutputStream&&) noexcept = default;
    auto operator=(OutputStream&&) noexcept -> OutputStream& = default;
};

/// Audio input stream
///
/// Thread Safety:
/// - start() and stop() are NOT thread-safe. The caller must serialize calls
///   to these methods (e.g., call from the same thread or use external locking).
/// - Once running, the audio callback executes on a separate audio thread.
/// - is_running(), config(), stream_time(), and device() are thread-safe for reads.
class InputStream
{
  public:
    /// Create input stream for device
    /// @param device_uid Device UID, name, or "default" for system default
    /// @param config Audio configuration
    [[nodiscard]] static auto create(std::string_view device_uid, AudioConfig const& config)
        -> StatusValue<std::unique_ptr<InputStream>>;

    /// Start audio streaming with callback
    /// @param callback Audio callback function
    /// @note Not thread-safe - do not call concurrently with stop()
    [[nodiscard]] virtual auto start(AudioCallbackFloat&& callback) -> Status = 0;

    /// Stop audio streaming (idempotent - safe to call when already stopped)
    /// @note Not thread-safe - do not call concurrently with start()
    virtual void stop() = 0;

    /// Check if stream is running
    [[nodiscard]] virtual auto is_running() const noexcept -> bool = 0;

    /// Get actual configuration (may differ from requested)
    [[nodiscard]] virtual auto config() const noexcept -> AudioConfig const& = 0;

    /// Get current stream time (seconds since start)
    [[nodiscard]] virtual auto stream_time() const noexcept -> double = 0;

    /// Get effective sample rate (actual hardware/negotiated rate)
    /// May differ from config().sample_rate due to hardware constraints or format negotiation
    [[nodiscard]] virtual auto effective_sample_rate() const noexcept -> double = 0;

    /// Get effective period frames (actual hardware/negotiated period size)
    /// May differ from config().buffer_frames due to hardware constraints
    [[nodiscard]] virtual auto effective_period_frames() const noexcept -> uint32_t = 0;

    /// Get effective buffer frames (actual hardware/negotiated buffer size)
    /// May differ from requested buffer size due to hardware constraints
    [[nodiscard]] virtual auto effective_buffer_frames() const noexcept -> uint32_t = 0;

    /// Get device info
    [[nodiscard]] virtual auto device() const noexcept -> DeviceInfo const& = 0;

    virtual ~InputStream() noexcept = default;

  protected:
    InputStream() = default;
    InputStream(InputStream const&) = delete;
    auto operator=(InputStream const&) -> InputStream& = delete;
    InputStream(InputStream&&) noexcept = default;
    auto operator=(InputStream&&) noexcept -> InputStream& = default;
};

}  // namespace statusbar::audio
