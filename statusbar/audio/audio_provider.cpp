// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Audio Module - Provider Implementation
// Global provider management and platform default implementations

#include "statusbar/audio/audio_provider.hpp"

#include "statusbar/audio/audio.hpp"
#include "statusbar/audio/audio_device.hpp"
#ifdef __APPLE__
#    include "statusbar/audio/audio_device_darwin.hpp"
#elif defined(__linux__)
#    include "statusbar/audio/audio_device_linux.hpp"
#endif
#include "statusbar/audio/audio_error.hpp"
#include "statusbar/audio/audio_stream.hpp"
#include "statusbar/audio/audio_types.hpp"
#include "statusbar/status/status.hpp"

#include <atomic>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace statusbar::audio {

// Platform Default Device Provider

/// Platform-specific device provider that wraps the existing DeviceManager implementation
class PlatformDeviceProvider : public IDeviceProvider
{
  public:
    auto enumerate_devices() -> std::vector<DeviceInfo> override { return DeviceManager::enumerate_devices(); }

    auto default_input_device() -> std::optional<DeviceInfo> override { return DeviceManager::default_input_device(); }

    auto default_output_device() -> std::optional<DeviceInfo> override { return DeviceManager::default_output_device(); }

    auto find_device(std::string_view identifier, bool is_input) -> std::optional<DeviceInfo> override
    {
        return DeviceManager::find_device(identifier, is_input);
    }
};

// Platform Default Stream Factory

/// Platform-specific stream factory
class PlatformStreamFactory : public IStreamFactory
{
  public:
    auto create_output_stream(DeviceInfo const& device, AudioConfig const& config)
        -> statusbar::StatusValue<std::unique_ptr<OutputStream>> override
    {
#if __cpp_exceptions
        try {
#endif
#ifdef __APPLE__
            return std::make_unique<OutputStreamDarwin>(device, config);
#elif defined(__linux__)
        return std::make_unique<OutputStreamLinux>(device, config);
#else
        (void)device;
        (void)config;
        return failure(AudioError::InitializationFailed);
#endif
#if __cpp_exceptions
        } catch (...) {
            return failure(AudioError::InitializationFailed);
        }
#endif
    }

    auto create_input_stream(DeviceInfo const& device, AudioConfig const& config)
        -> statusbar::StatusValue<std::unique_ptr<InputStream>> override
    {
#if __cpp_exceptions
        try {
#endif
#ifdef __APPLE__
            return std::make_unique<InputStreamDarwin>(device, config);
#elif defined(__linux__)
        return std::make_unique<InputStreamLinux>(device, config);
#else
        (void)device;
        (void)config;
        return failure(AudioError::InitializationFailed);
#endif
#if __cpp_exceptions
        } catch (...) {
            return failure(AudioError::InitializationFailed);
        }
#endif
    }
};

// Global Provider State

namespace {

// Mutex for thread-safe provider access
std::mutex g_provider_mutex;

// Custom providers (nullptr = use platform default)
std::unique_ptr<IDeviceProvider> g_custom_device_provider;
std::unique_ptr<IStreamFactory> g_custom_stream_factory;

// Platform default providers (lazily initialized)
std::unique_ptr<PlatformDeviceProvider> g_platform_device_provider;
std::unique_ptr<PlatformStreamFactory> g_platform_stream_factory;

auto get_platform_device_provider() -> PlatformDeviceProvider&
{
    if (!g_platform_device_provider) {
        g_platform_device_provider = std::make_unique<PlatformDeviceProvider>();
    }
    return *g_platform_device_provider;
}

auto get_platform_stream_factory() -> PlatformStreamFactory&
{
    if (!g_platform_stream_factory) {
        g_platform_stream_factory = std::make_unique<PlatformStreamFactory>();
    }
    return *g_platform_stream_factory;
}

}  // namespace

// Public Provider API

auto get_device_provider() -> IDeviceProvider&
{
    std::scoped_lock const lock(g_provider_mutex);
    if (g_custom_device_provider) {
        return *g_custom_device_provider;
    }
    return get_platform_device_provider();
}

auto set_device_provider(std::unique_ptr<IDeviceProvider> provider) -> void
{
    std::scoped_lock const lock(g_provider_mutex);
    g_custom_device_provider = std::move(provider);
}

auto get_stream_factory() -> IStreamFactory&
{
    std::scoped_lock const lock(g_provider_mutex);
    if (g_custom_stream_factory) {
        return *g_custom_stream_factory;
    }
    return get_platform_stream_factory();
}

auto set_stream_factory(std::unique_ptr<IStreamFactory> factory) -> void
{
    std::scoped_lock const lock(g_provider_mutex);
    g_custom_stream_factory = std::move(factory);
}

auto reset_providers() -> void
{
    std::scoped_lock const lock(g_provider_mutex);
    g_custom_device_provider.reset();
    g_custom_stream_factory.reset();
}

// ProviderGuard Implementation

ProviderGuard::ProviderGuard(std::unique_ptr<IDeviceProvider> device_provider, std::unique_ptr<IStreamFactory> stream_factory)
{
    std::scoped_lock const lock(g_provider_mutex);

    // Save current state
    had_custom_device_provider_ = (g_custom_device_provider != nullptr);
    had_custom_stream_factory_ = (g_custom_stream_factory != nullptr);

    if (had_custom_device_provider_) {
        previous_device_provider_ = std::move(g_custom_device_provider);
    }
    if (had_custom_stream_factory_) {
        previous_stream_factory_ = std::move(g_custom_stream_factory);
    }

    // Set new providers
    if (device_provider) {
        g_custom_device_provider = std::move(device_provider);
    }
    if (stream_factory) {
        g_custom_stream_factory = std::move(stream_factory);
    }
}

ProviderGuard::~ProviderGuard()
{
    std::scoped_lock const lock(g_provider_mutex);

    // Restore previous state
    if (had_custom_device_provider_) {
        g_custom_device_provider = std::move(previous_device_provider_);
    } else {
        g_custom_device_provider.reset();
    }

    if (had_custom_stream_factory_) {
        g_custom_stream_factory = std::move(previous_stream_factory_);
    } else {
        g_custom_stream_factory.reset();
    }
}

}  // namespace statusbar::audio