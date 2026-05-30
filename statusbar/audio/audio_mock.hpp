#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Audio Module - Mock Implementations for Testing
// Provides mock device provider and streams for hardware-independent testing

#include "statusbar/audio/audio_error.hpp"
#include "statusbar/audio/audio_provider.hpp"
#include "statusbar/audio/audio_stream.hpp"
#include "statusbar/audio/audio_types.hpp"
#include "statusbar/itc/itc_published.hpp"
#include "statusbar/status/status.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <limits>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace statusbar::audio {

// Mock Device Provider

/// Mock device provider for testing
/// Allows tests to configure a specific set of virtual devices
class MockDeviceProvider : public IDeviceProvider
{
  public:
    MockDeviceProvider() = default;

    /// Add a device to the mock device list
    /// @param device Device info to add
    auto add_device(DeviceInfo device) -> void { devices_.push_back(std::move(device)); }

    /// Clear all devices
    auto clear_devices() -> void { devices_.clear(); }

    /// Set which device UID is the default input
    /// @param uid Device UID to set as default input
    auto set_default_input(std::string_view const uid) -> void { default_input_uid_ = std::string(uid); }

    /// Set which device UID is the default output
    /// @param uid Device UID to set as default output
    auto set_default_output(std::string_view const uid) -> void { default_output_uid_ = std::string(uid); }

    /// Get number of devices
    [[nodiscard]] auto device_count() const { return devices_.size(); }

    // IDeviceProvider interface
    auto enumerate_devices() -> std::vector<DeviceInfo> override { return devices_; }

    auto default_input_device() -> std::optional<DeviceInfo> override;
    auto default_output_device() -> std::optional<DeviceInfo> override;

    /// @param identifier Device UID, name, or "default"
    /// @param is_input True to find input device, false for output
    auto find_device(std::string_view identifier, bool is_input) -> std::optional<DeviceInfo> override;

  private:
    std::vector<DeviceInfo> devices_;
    std::string default_input_uid_;
    std::string default_output_uid_;
};

// Mock Output Stream

/// Mock output stream for testing
/// Captures audio output and provides verification methods
class MockOutputStream : public OutputStream
{
  public:
    /// Configuration for mock behavior
    struct MockConfig
    {
        uint32_t callback_frames{512};                ///< Frames per callback
        size_t max_callbacks{0};                      ///< Max callbacks before auto-stop (0 = unlimited)
        bool simulate_latency{false};                 ///< Simulate real-time latency between callbacks
        AudioError error_on_start{AudioError::None};  ///< Inject error on start() (None = no error)
        int callback_return_value{0};                 ///< Value to inject from callback (for error testing)
        bool capture_output{true};                    ///< Whether to capture output samples
        size_t max_capture_frames{0};                 ///< Max frames to capture (0 = unlimited)
    };

    /// @param device Device info for this stream
    /// @param config Audio configuration
    MockOutputStream(DeviceInfo device, AudioConfig config)
        : MockOutputStream(std::move(device), config, MockConfig{})
    {}

    /// @param device Device info for this stream
    /// @param config Audio configuration
    /// @param mock_config Mock behavior configuration
    MockOutputStream(DeviceInfo device, AudioConfig config, MockConfig mock_config);

    ~MockOutputStream() override;

    // Non-copyable and non-movable (owns thread)
    MockOutputStream(MockOutputStream const&) = delete;
    auto operator=(MockOutputStream const&) -> MockOutputStream& = delete;
    MockOutputStream(MockOutputStream&&) = delete;
    auto operator=(MockOutputStream&&) -> MockOutputStream& = delete;

    // OutputStream interface
    /// @param callback Audio callback to invoke on each simulated period
    [[nodiscard]] auto start(AudioCallbackFloat&& callback) -> Status override;

    void stop() override;

    auto is_running() const noexcept -> bool override { return running_; }

    auto config() const noexcept -> AudioConfig const& override { return config_; }

    auto stream_time() const noexcept -> double override { return stream_time_.load(); }

    auto effective_sample_rate() const noexcept -> double override
    {
        return static_cast<double>(static_cast<uint32_t>(config_.sample_rate));
    }

    auto effective_period_frames() const noexcept -> uint32_t override
    {
        return mock_config_.callback_frames > 0 ? mock_config_.callback_frames : config_.buffer_frames;
    }

    auto effective_buffer_frames() const noexcept -> uint32_t override { return config_.buffer_frames; }

    auto device() const noexcept -> DeviceInfo const& override { return device_; }

    // Mock-specific methods

    /// Get number of callback invocations
    [[nodiscard]] auto callback_count() const { return callback_count_.load(); }

    /// Get total frames processed
    [[nodiscard]] auto total_frames() const { return total_frames_.load(); }

    /// Get captured output for a channel
    /// @param channel Channel index to retrieve
    [[nodiscard]] auto get_output_channel(uint32_t const channel) const -> std::vector<float> const&
    {
        static std::vector<float> const empty;
        if (channel < output_buffers_.size()) {
            return output_buffers_[channel];
        }
        return empty;
    }

    /// Get peak level of captured output for a channel
    /// @param channel Channel index to measure
    [[nodiscard]] auto get_peak_level(uint32_t channel) const -> float;

    /// Check if callback produced any non-zero output
    [[nodiscard]] auto has_output() const -> bool;

  private:
    auto simulation_loop() -> void;

    DeviceInfo device_;
    AudioConfig config_;
    MockConfig mock_config_;
    AudioCallbackFloat callback_;

    std::atomic<bool> running_;
    std::atomic<size_t> callback_count_;
    std::atomic<uint64_t> total_frames_;
    statusbar::itc::Published<double> stream_time_{};

    std::vector<std::vector<float>> output_buffers_;
    std::vector<float*> channel_ptrs_;

    // Scratch storage for building AudioCallbackParams
    std::vector<OutputAudioBufferFloat> callback_output_buffers_;

    std::thread simulation_thread_;

    void prepare_output_buffers(std::vector<float*>& out_ptrs, std::vector<float>& scratch, uint32_t frames);
    auto invoke_callback_and_check(std::vector<float*>& out_ptrs, uint32_t frames) -> bool;
    void capture_output(std::vector<float*> const& out_ptrs, uint32_t frames);
};

// Mock Input Stream

/// Mock input stream for testing
/// Provides pre-configured input data to the callback
class MockInputStream : public InputStream
{
  public:
    /// Configuration for mock behavior
    struct MockConfig
    {
        uint32_t callback_frames{512};                ///< Frames per callback
        size_t max_callbacks{0};                      ///< Max callbacks before auto-stop (0 = unlimited)
        bool simulate_latency{false};                 ///< Simulate real-time latency between callbacks
        AudioError error_on_start{AudioError::None};  ///< Inject error on start() (None = no error)
        bool loop_input{true};                        ///< Loop input data when exhausted
    };

    /// @param device Device info for this stream
    /// @param config Audio configuration
    MockInputStream(DeviceInfo device, AudioConfig config)
        : MockInputStream(std::move(device), config, MockConfig{})
    {}

    /// @param device Device info for this stream
    /// @param config Audio configuration
    /// @param mock_config Mock behavior configuration
    MockInputStream(DeviceInfo device, AudioConfig config, MockConfig mock_config);

    ~MockInputStream() override;

    // Non-copyable and non-movable (owns thread)
    MockInputStream(MockInputStream const&) = delete;
    auto operator=(MockInputStream const&) -> MockInputStream& = delete;
    MockInputStream(MockInputStream&&) = delete;
    auto operator=(MockInputStream&&) -> MockInputStream& = delete;

    /// Set input data for a channel
    /// @param channel Channel index to set data for
    /// @param data Sample data to provide as input
    auto set_input_data(uint32_t const channel, std::vector<float> data) -> void
    {
        if (channel < input_buffers_.size()) {
            input_buffers_[channel] = std::move(data);
        }
    }

    /// Generate sine wave input data
    /// @param frequency Sine wave frequency in Hz
    /// @param amplitude Peak amplitude of the sine wave
    /// @param frames Number of frames to generate
    auto generate_sine_wave(float frequency, float amplitude, size_t frames) -> void;

    // InputStream interface
    /// @param callback Audio callback to invoke on each simulated period
    [[nodiscard]] auto start(AudioCallbackFloat&& callback) -> Status override;

    void stop() override;

    auto is_running() const noexcept -> bool override { return running_; }

    auto config() const noexcept -> AudioConfig const& override { return config_; }

    auto stream_time() const noexcept -> double override { return stream_time_.load(); }

    auto effective_sample_rate() const noexcept -> double override
    {
        return static_cast<double>(static_cast<uint32_t>(config_.sample_rate));
    }

    auto effective_period_frames() const noexcept -> uint32_t override
    {
        return mock_config_.callback_frames > 0 ? mock_config_.callback_frames : config_.buffer_frames;
    }

    auto effective_buffer_frames() const noexcept -> uint32_t override { return config_.buffer_frames; }

    auto device() const noexcept -> DeviceInfo const& override { return device_; }

    // Mock-specific methods

    /// Get number of callback invocations
    [[nodiscard]] auto callback_count() const { return callback_count_.load(); }

    /// Get total frames processed
    [[nodiscard]] auto total_frames() const { return total_frames_.load(); }

  private:
    auto simulation_loop() -> void;

    DeviceInfo device_;
    AudioConfig config_;
    MockConfig mock_config_;
    AudioCallbackFloat callback_;

    std::atomic<bool> running_;
    std::atomic<size_t> callback_count_;
    std::atomic<uint64_t> total_frames_;
    statusbar::itc::Published<double> stream_time_{};

    std::vector<std::vector<float>> input_buffers_;
    size_t input_position_;

    // Scratch storage for building AudioCallbackParams
    std::vector<InputAudioBufferFloat> callback_input_buffers_;

    std::thread simulation_thread_;

    void fill_input_buffers(std::vector<float*>& in_ptrs, std::vector<float>& scratch, uint32_t frames);
    auto invoke_input_callback(std::vector<float*>& in_ptrs, uint32_t frames) -> bool;
};

// Mock Stream Factory

/// Mock stream factory that creates MockOutputStream and MockInputStream
class MockStreamFactory : public IStreamFactory
{
  public:
    /// Set default mock config for output streams
    /// @param config Mock configuration for created output streams
    auto set_output_mock_config(MockOutputStream::MockConfig const config) -> void { output_config_ = config; }

    /// Set default mock config for input streams
    /// @param config Mock configuration for created input streams
    auto set_input_mock_config(MockInputStream::MockConfig const config) -> void { input_config_ = config; }

    /// Set error to inject on output stream creation
    /// @param error Error code to return from create_output_stream
    auto inject_output_create_error(AudioError const error) -> void { output_create_error_ = error; }

    /// Set error to inject on input stream creation
    /// @param error Error code to return from create_input_stream
    auto inject_input_create_error(AudioError const error) -> void { input_create_error_ = error; }

    /// Get last created output stream (for verification)
    [[nodiscard]] auto last_output_stream() { return last_output_stream_; }

    /// Get last created input stream (for verification)
    [[nodiscard]] auto last_input_stream() { return last_input_stream_; }

    /// @param device Target device info
    /// @param config Audio configuration for the stream
    [[nodiscard]] auto create_output_stream(DeviceInfo const& device, AudioConfig const& config)
        -> StatusValue<std::unique_ptr<OutputStream>> override;

    /// @param device Target device info
    /// @param config Audio configuration for the stream
    [[nodiscard]] auto create_input_stream(DeviceInfo const& device, AudioConfig const& config)
        -> StatusValue<std::unique_ptr<InputStream>> override;

  private:
    MockOutputStream::MockConfig output_config_;
    MockInputStream::MockConfig input_config_;
    AudioError output_create_error_{AudioError::None};
    AudioError input_create_error_{AudioError::None};
    MockOutputStream* last_output_stream_{nullptr};
    MockInputStream* last_input_stream_{nullptr};
};

// Helper Functions

/// Create a standard mock device for testing
/// @param name Human-readable device name
/// @param uid Unique device identifier
/// @param in_channels Number of input channels (default 2)
/// @param out_channels Number of output channels (default 2)
[[nodiscard]] auto make_mock_device(std::string name, std::string uid, uint32_t in_channels = 2, uint32_t out_channels = 2)
    -> DeviceInfo;

/// Create a mock provider with a default device already configured
[[nodiscard]] auto make_default_mock_provider() -> std::unique_ptr<MockDeviceProvider>;

}  // namespace statusbar::audio
