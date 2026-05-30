#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Audio Module - Provider Abstraction Layer
// Enables mock/virtual device implementations for testing
// The provider pattern allows swapping real hardware with test implementations

#include "statusbar/audio/audio_error.hpp"
#include "statusbar/audio/audio_types.hpp"
#include "statusbar/status/status.hpp"

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace statusbar::audio {

// Forward declarations
class OutputStream;
class InputStream;

/// Device provider interface
/// Abstracts device enumeration and lookup for testability
class IDeviceProvider
{
  public:
    virtual ~IDeviceProvider() = default;

    /// Get list of all available audio devices
    [[nodiscard]] virtual auto enumerate_devices() -> std::vector<DeviceInfo> = 0;

    /// Get default input device info
    [[nodiscard]] virtual auto default_input_device() -> std::optional<DeviceInfo> = 0;

    /// Get default output device info
    [[nodiscard]] virtual auto default_output_device() -> std::optional<DeviceInfo> = 0;

    /// Find device by UID or name
    /// @param identifier Device UID or name, or "default" for system default
    /// @param is_input True for input device, false for output device
    [[nodiscard]] virtual auto find_device(std::string_view identifier, bool is_input) -> std::optional<DeviceInfo> = 0;

  protected:
    IDeviceProvider() = default;
    IDeviceProvider(IDeviceProvider const&) = default;
    auto operator=(IDeviceProvider const&) -> IDeviceProvider& = default;
    IDeviceProvider(IDeviceProvider&&) = default;
    auto operator=(IDeviceProvider&&) -> IDeviceProvider& = default;
};

/// Stream factory interface
/// Abstracts stream creation for testability
class IStreamFactory
{
  public:
    virtual ~IStreamFactory() = default;

    /// Create an output stream for the given device
    /// @param device Target device info
    /// @param config Audio configuration for the stream
    [[nodiscard]] virtual auto create_output_stream(DeviceInfo const& device, AudioConfig const& config)
        -> statusbar::StatusValue<std::unique_ptr<OutputStream>> = 0;

    /// Create an input stream for the given device
    /// @param device Target device info
    /// @param config Audio configuration for the stream
    [[nodiscard]] virtual auto create_input_stream(DeviceInfo const& device, AudioConfig const& config)
        -> statusbar::StatusValue<std::unique_ptr<InputStream>> = 0;

  protected:
    IStreamFactory() = default;
    IStreamFactory(IStreamFactory const&) = default;
    auto operator=(IStreamFactory const&) -> IStreamFactory& = default;
    IStreamFactory(IStreamFactory&&) = default;
    auto operator=(IStreamFactory&&) -> IStreamFactory& = default;
};

/// Get the current device provider
/// Returns the platform default if no custom provider has been set
[[nodiscard]] auto get_device_provider() -> IDeviceProvider&;

/// Set a custom device provider
/// Pass nullptr to reset to the platform default
/// @param provider Custom device provider, or nullptr to reset
void set_device_provider(std::unique_ptr<IDeviceProvider> provider);

/// Get the current stream factory
/// Returns the platform default if no custom factory has been set
[[nodiscard]] auto get_stream_factory() -> IStreamFactory&;

/// Set a custom stream factory
/// Pass nullptr to reset to the platform default
/// @param factory Custom stream factory, or nullptr to reset
void set_stream_factory(std::unique_ptr<IStreamFactory> factory);

/// Reset both providers to platform defaults
void reset_providers();

/// RAII guard for temporarily swapping providers in tests
/// Automatically restores previous providers on destruction
class ProviderGuard
{
  public:
    /// Create guard with custom providers
    /// @param device_provider Custom device provider (nullptr to keep current)
    /// @param stream_factory Custom stream factory (nullptr to keep current)
    ProviderGuard(std::unique_ptr<IDeviceProvider> device_provider, std::unique_ptr<IStreamFactory> stream_factory);

    /// Restore previous providers
    ~ProviderGuard();

    // Non-copyable, non-movable
    ProviderGuard(ProviderGuard const&) = delete;
    auto operator=(ProviderGuard const&) -> ProviderGuard& = delete;
    ProviderGuard(ProviderGuard&&) = delete;
    auto operator=(ProviderGuard&&) -> ProviderGuard& = delete;

  private:
    std::unique_ptr<IDeviceProvider> previous_device_provider_;
    std::unique_ptr<IStreamFactory> previous_stream_factory_;
    bool had_custom_device_provider_{false};
    bool had_custom_stream_factory_{false};
};

}  // namespace statusbar::audio
