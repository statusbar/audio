#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Audio Module - Device Enumeration
// Cross-platform device discovery and management

#include "statusbar/audio/audio_types.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace statusbar::audio {

/// Device manager for enumerating and querying audio devices
class DeviceManager
{
  public:
    /// Get list of all available audio devices
    [[nodiscard]] static auto enumerate_devices() -> std::vector<DeviceInfo>;

    /// Get default input device info
    [[nodiscard]] static auto default_input_device() -> std::optional<DeviceInfo>;

    /// Get default output device info
    [[nodiscard]] static auto default_output_device() -> std::optional<DeviceInfo>;

    /// Find device by UID or name
    /// @param identifier Device UID or name, or "default" for system default
    /// @param is_input True for input device, false for output device
    [[nodiscard]] static auto find_device(std::string_view identifier, bool is_input) -> std::optional<DeviceInfo>;
};

}  // namespace statusbar::audio
