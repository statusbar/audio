#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// @file audio_device_darwin.hpp
/// @brief macOS CoreAudio platform backend for audio device enumeration and streaming.
///
/// Provides the macOS-specific implementations of DeviceManager, OutputStream, and
/// InputStream using Apple's CoreAudio framework (AudioUnit / Audio HAL).
///
/// @par Platform requirements
/// - macOS only (guarded by `__APPLE__` preprocessor check in audio.hpp)
/// - Requires linking against CoreAudio.framework and AudioUnit.framework
/// - Device UIDs correspond to CoreAudio AudioDeviceID values

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

/// macOS CoreAudio device manager implementation.
///
/// Provides platform-specific device enumeration using the Audio HAL API
/// (AudioObjectGetPropertyData). Devices are identified by their CoreAudio
/// AudioDeviceID, exposed as string UIDs in DeviceInfo.
///
/// @note This class is used internally by DeviceManager and should not be
///       called directly by application code.
class DeviceManagerDarwin
{
  public:
    /// Enumerate all available CoreAudio devices (inputs and outputs).
    /// @return Vector of DeviceInfo for each discovered audio device
    static auto enumerate_devices_impl() -> std::vector<DeviceInfo>;

    /// Get the system default input device.
    /// @return DeviceInfo for the default input, or nullopt if none available
    static auto default_input_device_impl() -> std::optional<DeviceInfo>;

    /// Get the system default output device.
    /// @return DeviceInfo for the default output, or nullopt if none available
    static auto default_output_device_impl() -> std::optional<DeviceInfo>;

    /// Find a device by its CoreAudio UID or display name.
    /// @param identifier Device UID, display name, or "default" for system default
    /// @param is_input True to search input devices, false for output devices
    /// @return DeviceInfo if found, or nullopt if no matching device exists
    static auto find_device_impl(std::string_view identifier, bool is_input) -> std::optional<DeviceInfo>;
};

/// macOS CoreAudio output stream implementation.
///
/// Uses an AudioUnit (kAudioUnitSubType_HALOutput) for low-latency audio output.
/// The audio callback runs on a CoreAudio real-time thread managed by the system.
///
/// Uses the PIMPL pattern to hide CoreAudio framework types from the public header.
///
/// @note Non-copyable and non-movable due to the PIMPL pointer and CoreAudio
///       resource ownership.
class OutputStreamDarwin : public OutputStream
{
  public:
    /// Construct a CoreAudio output stream.
    /// @param device Target output device
    /// @param config Desired audio configuration (sample rate, channels, buffer size)
    OutputStreamDarwin(DeviceInfo device, AudioConfig config);
    ~OutputStreamDarwin() noexcept override;

    // Non-copyable, non-movable (pimpl with unique_ptr to incomplete type)
    OutputStreamDarwin(OutputStreamDarwin const&) = delete;
    auto operator=(OutputStreamDarwin const&) -> OutputStreamDarwin& = delete;
    OutputStreamDarwin(OutputStreamDarwin&&) = delete;
    auto operator=(OutputStreamDarwin&&) -> OutputStreamDarwin& = delete;

    /// Start audio output with the given callback.
    /// @param callback Called on the CoreAudio real-time thread to fill output buffers
    /// @return Success, or an error if the AudioUnit fails to start
    [[nodiscard]] auto start(AudioCallbackFloat&& callback) -> Status override;

    /// Stop audio output. Idempotent -- safe to call when already stopped.
    void stop() override;

    /// Check if the stream is currently running.
    [[nodiscard]] auto is_running() const noexcept -> bool override;

    /// Get the audio configuration for this stream.
    [[nodiscard]] auto config() const noexcept -> AudioConfig const& override;

    /// Get elapsed stream time in seconds since start.
    [[nodiscard]] auto stream_time() const noexcept -> double override;

    /// Get the actual sample rate negotiated with CoreAudio.
    /// May differ from the requested rate if the hardware does not support it.
    [[nodiscard]] auto effective_sample_rate() const noexcept -> double override;

    /// Get the actual period size (frames per callback) negotiated with CoreAudio.
    [[nodiscard]] auto effective_period_frames() const noexcept -> uint32_t override;

    /// Get the actual buffer size in frames negotiated with CoreAudio.
    [[nodiscard]] auto effective_buffer_frames() const noexcept -> uint32_t override;

    /// Get the device info for this stream.
    [[nodiscard]] auto device() const noexcept -> DeviceInfo const& override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// macOS CoreAudio input stream implementation.
///
/// Uses an AudioUnit (kAudioUnitSubType_HALOutput configured for input) for
/// low-latency audio capture. The audio callback runs on a CoreAudio real-time
/// thread managed by the system.
///
/// Uses the PIMPL pattern to hide CoreAudio framework types from the public header.
///
/// @note Non-copyable and non-movable due to the PIMPL pointer and CoreAudio
///       resource ownership.
class InputStreamDarwin : public InputStream
{
  public:
    /// Construct a CoreAudio input stream.
    /// @param device Target input device
    /// @param config Desired audio configuration (sample rate, channels, buffer size)
    InputStreamDarwin(DeviceInfo device, AudioConfig config);
    ~InputStreamDarwin() noexcept override;

    // Non-copyable, non-movable (pimpl with unique_ptr to incomplete type)
    InputStreamDarwin(InputStreamDarwin const&) = delete;
    auto operator=(InputStreamDarwin const&) -> InputStreamDarwin& = delete;
    InputStreamDarwin(InputStreamDarwin&&) = delete;
    auto operator=(InputStreamDarwin&&) -> InputStreamDarwin& = delete;

    /// Start audio capture with the given callback.
    /// @param callback Called on the CoreAudio real-time thread with captured input buffers
    /// @return Success, or an error if the AudioUnit fails to start
    [[nodiscard]] auto start(AudioCallbackFloat&& callback) -> Status override;

    /// Stop audio capture. Idempotent -- safe to call when already stopped.
    void stop() override;

    /// Check if the stream is currently running.
    [[nodiscard]] auto is_running() const noexcept -> bool override;

    /// Get the audio configuration for this stream.
    [[nodiscard]] auto config() const noexcept -> AudioConfig const& override;

    /// Get elapsed stream time in seconds since start.
    [[nodiscard]] auto stream_time() const noexcept -> double override;

    /// Get the actual sample rate negotiated with CoreAudio.
    /// May differ from the requested rate if the hardware does not support it.
    [[nodiscard]] auto effective_sample_rate() const noexcept -> double override;

    /// Get the actual period size (frames per callback) negotiated with CoreAudio.
    [[nodiscard]] auto effective_period_frames() const noexcept -> uint32_t override;

    /// Get the actual buffer size in frames negotiated with CoreAudio.
    [[nodiscard]] auto effective_buffer_frames() const noexcept -> uint32_t override;

    /// Get the device info for this stream.
    [[nodiscard]] auto device() const noexcept -> DeviceInfo const& override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace statusbar::audio
