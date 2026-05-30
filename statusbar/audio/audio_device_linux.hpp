#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// @file audio_device_linux.hpp
/// @brief Linux ALSA platform backend for audio device enumeration and streaming.
///
/// Provides the Linux-specific implementations of DeviceManager, OutputStream, and
/// InputStream using ALSA (Advanced Linux Sound Architecture).
///
/// @par Platform requirements
/// - Linux only (guarded by `__linux__` preprocessor check in audio.hpp)
/// - Requires libasound2 (ALSA library) at link time
/// - Device identifiers use ALSA card/device naming (e.g., "hw:0", "plughw:0,0", "default")
/// - ALSA may negotiate different hardware parameters than requested (sample rate,
///   period size, buffer size); use the effective_* accessors to query actual values

#include "statusbar/audio/audio_device.hpp"
#include "statusbar/audio/audio_error.hpp"
#include "statusbar/audio/audio_stream.hpp"
#include "statusbar/audio/audio_types.hpp"
#include "statusbar/status/status.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace statusbar::audio {

/// Linux ALSA device manager implementation.
///
/// Provides platform-specific device enumeration using the ALSA library
/// (snd_device_name_hint / snd_card_* APIs). Devices are identified by
/// their ALSA hardware names (e.g., "hw:0", "plughw:0,0").
///
/// @note This class is used internally by DeviceManager and should not be
///       called directly by application code.
class DeviceManagerLinux
{
  public:
    /// Enumerate all available ALSA audio devices (inputs and outputs).
    /// @return Vector of DeviceInfo for each discovered ALSA device
    [[nodiscard]] static auto enumerate_devices_impl() -> std::vector<DeviceInfo>;

    /// Get the system default ALSA input device.
    /// @return DeviceInfo for the default input, or nullopt if none available
    [[nodiscard]] static auto default_input_device_impl() -> std::optional<DeviceInfo>;

    /// Get the system default ALSA output device.
    /// @return DeviceInfo for the default output, or nullopt if none available
    [[nodiscard]] static auto default_output_device_impl() -> std::optional<DeviceInfo>;

    /// Find an ALSA device by its hardware name or display name.
    /// @param identifier ALSA device name (e.g., "hw:0"), display name, or "default"
    /// @param is_input True to search input (capture) devices, false for output (playback)
    /// @return DeviceInfo if found, or nullopt if no matching device exists
    [[nodiscard]] static auto find_device_impl(std::string_view identifier, bool is_input) -> std::optional<DeviceInfo>;
};

/// Linux ALSA output stream implementation.
///
/// Opens an ALSA PCM playback device and runs a write loop on a dedicated thread.
/// ALSA hardware parameters (sample rate, period size, buffer size) are negotiated
/// during construction and may differ from the requested AudioConfig values.
///
/// Uses the PIMPL pattern to hide ALSA types (snd_pcm_t, etc.) from the public header.
///
/// @note Non-copyable and non-movable due to the PIMPL pointer and ALSA
///       resource ownership.
class OutputStreamLinux : public OutputStream
{
  public:
    /// Construct an ALSA output stream.
    /// @param device Target output device
    /// @param config Desired audio configuration (sample rate, channels, buffer size)
    OutputStreamLinux(DeviceInfo device, AudioConfig config);
    ~OutputStreamLinux() noexcept override;

    // Non-copyable, non-movable (PIMPL with unique_ptr)
    OutputStreamLinux(OutputStreamLinux const&) = delete;
    auto operator=(OutputStreamLinux const&) -> OutputStreamLinux& = delete;
    OutputStreamLinux(OutputStreamLinux&&) = delete;
    auto operator=(OutputStreamLinux&&) -> OutputStreamLinux& = delete;

    /// Start audio output with the given callback.
    /// @param callback Called on a dedicated audio thread to fill output buffers
    /// @return Success, or an error if the ALSA device fails to start
    [[nodiscard]] auto start(AudioCallbackFloat&& callback) -> Status override;

    /// Stop audio output. Idempotent -- safe to call when already stopped.
    void stop() override;

    /// Check if the stream is currently running.
    [[nodiscard]] auto is_running() const noexcept -> bool override;

    /// Get the audio configuration for this stream.
    [[nodiscard]] auto config() const noexcept -> AudioConfig const& override;

    /// Get elapsed stream time in seconds since start.
    [[nodiscard]] auto stream_time() const noexcept -> double override;

    /// Get the device info for this stream.
    [[nodiscard]] auto device() const noexcept -> DeviceInfo const& override;

    /// Get the actual sample rate negotiated with ALSA hardware.
    /// May differ from the requested rate if the hardware does not support it exactly.
    [[nodiscard]] auto effective_sample_rate() const noexcept -> double override;

    /// Get the actual period size (frames per callback) negotiated with ALSA.
    /// May differ from the requested buffer_frames due to hardware constraints.
    [[nodiscard]] auto effective_period_frames() const noexcept -> uint32_t override;

    /// Get the actual total buffer size in frames negotiated with ALSA.
    /// Typically a multiple of effective_period_frames().
    [[nodiscard]] auto effective_buffer_frames() const noexcept -> uint32_t override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Linux ALSA input stream implementation.
///
/// Opens an ALSA PCM capture device and runs a read loop on a dedicated thread.
/// ALSA hardware parameters (sample rate, period size, buffer size) are negotiated
/// during construction and may differ from the requested AudioConfig values.
///
/// Uses the PIMPL pattern to hide ALSA types (snd_pcm_t, etc.) from the public header.
///
/// @note Non-copyable and non-movable due to the PIMPL pointer and ALSA
///       resource ownership.
class InputStreamLinux : public InputStream
{
  public:
    /// Construct an ALSA input stream.
    /// @param device Target input (capture) device
    /// @param config Desired audio configuration (sample rate, channels, buffer size)
    InputStreamLinux(DeviceInfo device, AudioConfig config);
    ~InputStreamLinux() noexcept override;

    // Non-copyable, non-movable (PIMPL with unique_ptr)
    InputStreamLinux(InputStreamLinux const&) = delete;
    auto operator=(InputStreamLinux const&) -> InputStreamLinux& = delete;
    InputStreamLinux(InputStreamLinux&&) = delete;
    auto operator=(InputStreamLinux&&) -> InputStreamLinux& = delete;

    /// Start audio capture with the given callback.
    /// @param callback Called on a dedicated audio thread with captured input buffers
    /// @return Success, or an error if the ALSA device fails to start
    [[nodiscard]] auto start(AudioCallbackFloat&& callback) -> Status override;

    /// Stop audio capture. Idempotent -- safe to call when already stopped.
    void stop() override;

    /// Check if the stream is currently running.
    [[nodiscard]] auto is_running() const noexcept -> bool override;

    /// Get the audio configuration for this stream.
    [[nodiscard]] auto config() const noexcept -> AudioConfig const& override;

    /// Get elapsed stream time in seconds since start.
    [[nodiscard]] auto stream_time() const noexcept -> double override;

    /// Get the device info for this stream.
    [[nodiscard]] auto device() const noexcept -> DeviceInfo const& override;

    /// Get the actual sample rate negotiated with ALSA hardware.
    /// May differ from the requested rate if the hardware does not support it exactly.
    [[nodiscard]] auto effective_sample_rate() const noexcept -> double override;

    /// Get the actual period size (frames per callback) negotiated with ALSA.
    /// May differ from the requested buffer_frames due to hardware constraints.
    [[nodiscard]] auto effective_period_frames() const noexcept -> uint32_t override;

    /// Get the actual total buffer size in frames negotiated with ALSA.
    /// Typically a multiple of effective_period_frames().
    [[nodiscard]] auto effective_buffer_frames() const noexcept -> uint32_t override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace statusbar::audio
