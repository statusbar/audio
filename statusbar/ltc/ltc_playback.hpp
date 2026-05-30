#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// LTC Audio Playback Support
// Provides state structs and callback functions for LTC audio playback
// Supports both servo-controlled realtime clock and fixed timecode modes

#include "statusbar/audio/audio.hpp"
#include "statusbar/ltc/ltc_generator.hpp"
#include "statusbar/ltc/ltc_servo.hpp"
#include "statusbar/ltc/ltc_timecode.hpp"
#include "statusbar/status/status.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace statusbar::ltc {

// Time Offset Utilities

/// Parse time offset in HH:MM:SS format
/// @param str Time offset string (e.g., "09:00:00")
/// @return Offset in seconds, or nullopt if invalid format
[[nodiscard]] auto parse_time_offset(std::string_view str) noexcept -> std::optional<int32_t>;

/// Get high-resolution timestamp in seconds since epoch
[[nodiscard]] inline auto get_realtime_timestamp() noexcept -> double
{
    using namespace std::chrono;
    auto now = system_clock::now();
    auto dur = now.time_since_epoch();
    return duration_cast<duration<double>>(dur).count();
}

/// Local time components for timecode conversion
struct LocalTimeInfo
{
    std::tm local_time;    ///< Local time (hours, minutes, seconds)
    int32_t millis;        ///< Milliseconds within the current second (0-999)
    double epoch_seconds;  ///< Time as seconds since epoch (for timing calculations)
};

/// Get local time information from current system clock
/// @return LocalTimeInfo containing local time, milliseconds, and epoch seconds
[[nodiscard]] auto get_local_time_info() noexcept -> LocalTimeInfo;

// Playback State Base Classes
// Base classes with out-of-line implementations to prevent inlining issues
// across module boundaries (LLVM Issue #172241)

/// Base class for ServoPlaybackState with atomic running flag
class ServoPlaybackStateBase
{
  public:
    ServoPlaybackStateBase() noexcept;

    /// Check if playback is running
    [[nodiscard]] auto is_running() const noexcept -> bool;
    /// Set running state (use to stop playback)
    /// @param value True to enable playback, false to stop
    auto set_running(bool value) noexcept -> void;

  protected:
    std::atomic<bool> running_;
};

/// Base class for FixedPlaybackState with atomic finished flag
class FixedPlaybackStateBase
{
  public:
    FixedPlaybackStateBase() noexcept;

    /// Check if playback is finished
    [[nodiscard]] auto is_finished() const noexcept -> bool;
    /// Set finished state
    /// @param value True to mark playback as finished
    auto set_finished(bool value) noexcept -> void;

  protected:
    std::atomic<bool> finished_;
};

// Servo Playback State and Callback

/// State for realtime clock servo playback
/// Used with process_servo_callback for audio generation synchronized to wall clock
struct ServoPlaybackState : public ServoPlaybackStateBase
{
    /// Constructor for servo playback state
    /// @param servo_gen Pointer to servo generator (must outlive this state)
    /// @param rate Audio sample rate in Hz
    /// @param fr SMPTE frame rate
    /// @param epoch Timestamp when playback started (seconds since epoch)
    /// @param offset Time-of-day offset in seconds (0 = no offset)
    ServoPlaybackState(ServoGenerator* servo_gen, uint32_t rate, FrameRate fr, double epoch, int32_t offset = 0) noexcept
        : servo(servo_gen)
        , start_epoch(epoch)
        , sample_rate(rate)
        , frame_rate(fr)
        , time_offset_seconds(offset)
    {}

    ServoGenerator* servo;                ///< Pointer to servo generator (must outlive this state)
    std::vector<float> sample_buffer;     ///< Scratch buffer for samples
    uint64_t total_samples_generated{0};  ///< Total samples generated so far
    double start_epoch;                   ///< Timestamp when playback started (seconds since epoch)
    double last_update_epoch{0.0};        ///< Last servo update timestamp (seconds since epoch)
    uint32_t sample_rate;                 ///< Audio sample rate in Hz
    FrameRate frame_rate;                 ///< SMPTE frame rate
    int32_t time_offset_seconds{0};       ///< Time-of-day offset in seconds (0 = no offset)
    // running flag moved to ServoPlaybackStateBase
};

/// Process audio callback for servo-controlled playback
/// @param state Playback state (must remain valid for duration of playback)
/// @param local_time Local time (from localtime_r/localtime_s)
/// @param millis Milliseconds within the current second (0-999)
/// @param current_epoch Current time as seconds since epoch (for servo timing)
/// @param params Audio callback parameters from the audio stream
/// @return success() to continue, failure() to stop stream
[[nodiscard]] auto process_servo_callback(
    ServoPlaybackState& state,
    std::tm const& local_time,
    int32_t millis,
    double current_epoch,
    audio::AudioCallbackParamsFloat const& params) -> Status;

// Fixed Timecode Playback State and Callback

/// State for fixed timecode playback
/// Used with process_fixed_callback for generating a fixed duration of LTC
struct FixedPlaybackState : public FixedPlaybackStateBase
{
    /// Constructor for fixed playback state
    /// @param gen Pointer to LTC generator (must outlive this state)
    /// @param tc Starting timecode for playback
    /// @param total Total number of frames to generate
    FixedPlaybackState(Generator* gen, Timecode tc, size_t total) noexcept
        : generator(gen)
        , current_tc(tc)
        , total_frames(total)
    {}

    Generator* generator;             ///< Pointer to LTC generator (must outlive this state)
    Timecode current_tc;              ///< Current timecode being generated
    std::vector<float> frame_buffer;  ///< Buffer for current frame samples
    size_t frame_position{0};         ///< Current position within frame buffer
    size_t frames_generated{0};       ///< Number of SMPTE frames generated
    size_t total_frames;              ///< Total SMPTE frames to generate
    // finished flag moved to FixedPlaybackStateBase
};

/// Process audio callback for fixed timecode playback
/// @param state Playback state (must remain valid for duration of playback)
/// @param params Audio callback parameters from the audio stream
/// @return success() to continue, failure() to stop stream when finished
[[nodiscard]] auto process_fixed_callback(FixedPlaybackState& state, audio::AudioCallbackParamsFloat const& params) -> Status;

}  // namespace statusbar::ltc
