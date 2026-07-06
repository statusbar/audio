#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Audio Module - Error Handling
// Error codes and error category for audio operations

#include <cstdint>
#include <string>
#include <system_error>

namespace statusbar::audio {

/// Audio error codes
enum class AudioError : uint32_t
{
    None = 0,                     ///< No error
    DeviceNotFound = 1,           ///< Requested device not found
    InvalidConfig = 2,            ///< Invalid configuration parameters
    DeviceBusy = 3,               ///< Device is busy or in use
    HardwareError = 4,            ///< Hardware or driver error
    CallbackError = 5,            ///< Error in user callback
    BufferUnderrun = 6,           ///< Audio buffer underrun
    BufferOverrun = 7,            ///< Audio buffer overrun
    UnsupportedFormat = 8,        ///< Sample format not supported
    UnsupportedSampleRate = 9,    ///< Sample rate not supported
    StreamNotRunning = 10,        ///< Operation requires running stream
    AlreadyRunning = 11,          ///< Stream is already running
    InitializationFailed = 12,    ///< Failed to initialize audio system
    RealtimePriorityDenied = 13,  ///< Could not obtain the requested realtime thread priority
};

/// Error category for audio errors
class AudioErrorCategory : public std::error_category
{
  public:
    [[nodiscard]] auto name() const noexcept -> char const* override { return "statusbar.audio"; }

    /// @param ev Error code value to convert to message string
    [[nodiscard]] auto message(int ev) const -> std::string override;
};

/// Get the audio error category singleton
[[nodiscard]] auto audio_error_category() -> std::error_category const&;

/// Create an error_code from AudioError
/// @param e Audio error code to convert
[[nodiscard]] auto make_error_code(AudioError e) -> std::error_code;

}  // namespace statusbar::audio

// Register AudioError for std::error_code
template <>
struct std::is_error_code_enum<statusbar::audio::AudioError> : std::true_type
{};
