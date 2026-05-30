// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/audio/audio_error.hpp"

#include <string>
#include <system_error>

namespace statusbar::audio {

auto AudioErrorCategory::message(int const ev) const -> std::string
{
    switch (static_cast<AudioError>(ev)) {
        case AudioError::None:
            return "No error";
        case AudioError::DeviceNotFound:
            return "Audio device not found";
        case AudioError::InvalidConfig:
            return "Invalid audio configuration";
        case AudioError::DeviceBusy:
            return "Audio device is busy";
        case AudioError::HardwareError:
            return "Audio hardware error";
        case AudioError::CallbackError:
            return "Error in audio callback";
        case AudioError::BufferUnderrun:
            return "Audio buffer underrun";
        case AudioError::BufferOverrun:
            return "Audio buffer overrun";
        case AudioError::UnsupportedFormat:
            return "Sample format not supported";
        case AudioError::UnsupportedSampleRate:
            return "Sample rate not supported";
        case AudioError::StreamNotRunning:
            return "Audio stream is not running";
        case AudioError::AlreadyRunning:
            return "Audio stream is already running";
        case AudioError::InitializationFailed:
            return "Failed to initialize audio system";
        default:
            return "Unknown audio error";
    }
}

auto audio_error_category() -> std::error_category const&
{
    static AudioErrorCategory const instance;
    return instance;
}

auto make_error_code(AudioError const e) -> std::error_code
{
    return {static_cast<int>(e), audio_error_category()};
}

}  // namespace statusbar::audio
