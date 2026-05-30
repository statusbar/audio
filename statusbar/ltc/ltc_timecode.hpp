#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// SMPTE LTC Timecode Structure
// Represents a single SMPTE timecode value (HH:MM:SS:FF)
// Supports multiple frame rates per SMPTE 12M

#include <chrono>
#include <compare>
#include <cstdint>
#include <ctime>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace statusbar::ltc {

/// SMPTE frame rate standards
enum class FrameRate : uint8_t
{
    Rate_23_976,     ///< 23.976 fps (24/1.001) - Film pulldown
    Rate_24,         ///< 24 fps - Film
    Rate_25,         ///< 25 fps - PAL/SECAM
    Rate_29_97_DF,   ///< 29.97 fps drop frame (30/1.001) - NTSC broadcast
    Rate_29_97_NDF,  ///< 29.97 fps non-drop frame - NTSC non-broadcast
    Rate_30_DF,      ///< 30 fps drop frame
    Rate_30_NDF      ///< 30 fps non-drop frame
};

/// Get the nominal frames per second for a frame rate.
/// \param rate The frame rate to query.
/// \return Nominal integer frame count (24, 25, or 30).
[[nodiscard]] constexpr auto frames_per_second(FrameRate rate) noexcept
{
    switch (rate) {
        case FrameRate::Rate_23_976:
        case FrameRate::Rate_24:
            return 24;
        case FrameRate::Rate_25:
            return 25;
        case FrameRate::Rate_29_97_DF:
        case FrameRate::Rate_29_97_NDF:
        case FrameRate::Rate_30_DF:
        case FrameRate::Rate_30_NDF:
            return 30;
    }
    return 30;  // Default fallback
}

/// Check if a frame rate uses drop frame counting.
/// \param rate The frame rate to query.
/// \return true if the frame rate is drop-frame.
[[nodiscard]] constexpr auto is_drop_frame(FrameRate rate) noexcept
{
    return rate == FrameRate::Rate_29_97_DF || rate == FrameRate::Rate_30_DF;
}

/// Get the actual frame rate as a double (for timing calculations).
/// \param rate The frame rate to query.
/// \return Exact frame rate (e.g., 29.97 for NTSC, 24.0 for film).
[[nodiscard]] constexpr auto actual_frame_rate(FrameRate rate) noexcept
{
    switch (rate) {
        case FrameRate::Rate_23_976:
            return 24000.0 / 1001.0;  // 23.976...
        case FrameRate::Rate_24:
            return 24.0;
        case FrameRate::Rate_25:
            return 25.0;
        case FrameRate::Rate_29_97_DF:
        case FrameRate::Rate_29_97_NDF:
            return 30000.0 / 1001.0;  // 29.97...
        case FrameRate::Rate_30_DF:
        case FrameRate::Rate_30_NDF:
            return 30.0;
    }
    return 30.0;  // Default fallback
}

/// Get a human-readable name for the frame rate.
/// \param rate The frame rate to query.
/// \return Display string (e.g., "29.97 fps DF", "24 fps").
[[nodiscard]] auto frame_rate_name(FrameRate rate) -> std::string;

/// Parse frame rate from string
/// \param str Frame rate string (e.g., "23.976", "24", "25", "29.97df", "29.97", "30df", "30ndf", "30")
/// \return FrameRate if valid, nullopt otherwise
[[nodiscard]] auto parse_frame_rate(std::string_view str) noexcept -> std::optional<FrameRate>;

/// SMPTE Timecode structure supporting multiple frame rates
struct Timecode
{
    uint8_t hours{0};                        ///< Hours (0-23)
    uint8_t minutes{0};                      ///< Minutes (0-59)
    uint8_t seconds{0};                      ///< Seconds (0-59)
    uint8_t frames{0};                       ///< Frames (0 to fps-1)
    uint32_t user_bits{0};                   ///< 32-bit user data
    FrameRate rate{FrameRate::Rate_30_NDF};  ///< Frame rate
    bool color_frame{false};                 ///< Color frame flag

    /// Check if timecode component values exceed valid ranges
    [[nodiscard]] constexpr auto is_out_of_range(uint8_t max_frames) const noexcept
    {
        return hours >= 24 || minutes >= 60 || seconds >= 60 || frames >= max_frames;
    }

    /// Check if the current position is a drop-frame skip point
    /// (frames 0,1 at start of each minute except 0, 10, 20, 30, 40, 50)
    [[nodiscard]] constexpr auto is_drop_frame_skip() const noexcept { return frames < 2 && seconds == 0 && (minutes % 10) != 0; }

    /// Check if at or before the drop-frame boundary (frames 0-2 at skip points)
    /// Used by decrement_frame to decide when to roll back to previous second
    [[nodiscard]] constexpr auto at_drop_frame_boundary() const noexcept
    {
        return frames <= 2 && seconds == 0 && (minutes % 10) != 0;
    }

    /// Validate timecode values for the configured frame rate
    [[nodiscard]] constexpr auto is_valid() const noexcept
    {
        uint8_t const max_frames = frames_per_second(rate);
        if (is_out_of_range(max_frames)) {
            return false;
        }

        // For drop frame, frames 0 and 1 are skipped at the start of each minute
        // except for minutes 0, 10, 20, 30, 40, 50
        if (is_drop_frame(rate)) {
            if (is_drop_frame_skip()) {
                return false;  // These frame numbers are dropped
            }
        }

        return true;
    }

    /// Check if this timecode is a dropped frame number
    /// (would be skipped in drop frame counting)
    [[nodiscard]] constexpr auto is_dropped_frame() const noexcept
    {
        if (!is_drop_frame(rate)) {
            return false;
        }
        return is_drop_frame_skip();
    }

    /// Increment to next frame (handles rollover and drop frame)
    constexpr auto increment_frame() noexcept -> void
    {
        uint8_t const max_frames = frames_per_second(rate);
        ++frames;

        if (frames >= max_frames) {
            frames = 0;
            ++seconds;
            if (seconds >= 60) {
                seconds = 0;
                ++minutes;
                if (minutes >= 60) {
                    minutes = 0;
                    ++hours;
                    if (hours >= 24) {
                        hours = 0;
                    }
                }
            }

            // Drop frame adjustment: skip frames 0 and 1 at the start of each minute
            // except for minutes 0, 10, 20, 30, 40, 50
            if (is_drop_frame(rate) && is_drop_frame_skip()) {
                frames = 2;  // Skip frames 0 and 1
            }
        }
    }

    /// Decrement to previous frame (handles rollover and drop frame)
    constexpr auto decrement_frame() noexcept -> void
    {
        uint8_t const max_frames = frames_per_second(rate);

        if (frames == 0 || (is_drop_frame(rate) && at_drop_frame_boundary())) {
            // Need to roll back to previous second
            if (seconds == 0) {
                seconds = 59;
                if (minutes == 0) {
                    minutes = 59;
                    if (hours == 0) {
                        hours = 23;
                    } else {
                        --hours;
                    }
                } else {
                    --minutes;
                }
            } else {
                --seconds;
            }
            frames = max_frames - 1;
        } else {
            --frames;
        }
    }

    /// Convert to total frame count (since midnight)
    /// For drop frame, accounts for dropped frame numbers
    [[nodiscard]] constexpr auto to_frame_count() const noexcept
    {
        uint8_t const fps = frames_per_second(rate);
        uint32_t total = (((hours * 3600U) + (minutes * 60U) + seconds) * fps) + frames;

        if (is_drop_frame(rate)) {
            // Subtract dropped frames: 2 frames per minute, except every 10th minute
            // Total drop frames = 2 * (total_minutes - floor(total_minutes / 10))
            uint32_t const total_minutes = (hours * 60U) + minutes;
            uint32_t const drop_frames = 2 * (total_minutes - (total_minutes / 10));
            total -= drop_frames;
        }

        return total;
    }

    /// Create from total frame count
    /// For drop frame, properly reconstructs the timecode with dropped frames
    /// \param frame_count Total number of frames since 00:00:00:00
    /// \param frame_rate SMPTE frame rate for timecode reconstruction
    [[nodiscard]] static constexpr auto from_frame_count(uint32_t frame_count, FrameRate frame_rate) noexcept
    {
        Timecode tc;
        tc.rate = frame_rate;
        uint8_t const fps = frames_per_second(frame_rate);

        if (is_drop_frame(frame_rate)) {
            // Drop frame calculation
            // Every minute (except 0, 10, 20...) drops 2 frames
            // Per 10-minute block: 9 * 2 = 18 frames dropped
            // Frames per 10-minute block = 10 * 60 * fps - 18

            uint32_t const frames_per_10min = ((10 * 60) * fps) - 18;
            uint32_t const frames_per_min = (60 * fps) - 2;  // For non-10th minutes

            // Calculate 10-minute blocks
            uint32_t const ten_min_blocks = frame_count / frames_per_10min;
            uint32_t remaining = frame_count % frames_per_10min;

            // First minute of each 10-min block has no drops
            uint32_t const first_min_frames = 60 * fps;

            uint32_t minutes_in_block = 0;
            uint32_t frames_in_minute = 0;

            if (remaining < first_min_frames) {
                // We're in the first (non-drop) minute of the block
                minutes_in_block = 0;
                frames_in_minute = remaining;
            } else {
                // We're in minutes 1-9 of the block (drop frame minutes)
                remaining -= first_min_frames;
                minutes_in_block = 1 + (remaining / frames_per_min);
                frames_in_minute = remaining % frames_per_min;

                // Add back the 2 dropped frames for display
                frames_in_minute += 2;
            }

            uint32_t const total_minutes = (ten_min_blocks * 10) + minutes_in_block;

            tc.frames = frames_in_minute % fps;
            tc.seconds = (frames_in_minute / fps) % 60;
            tc.minutes = total_minutes % 60;
            tc.hours = (total_minutes / 60) % 24;
        } else {
            // Non-drop frame - straightforward calculation
            tc.frames = frame_count % fps;
            uint32_t const total_seconds = frame_count / fps;
            tc.seconds = total_seconds % 60;
            uint32_t const total_minutes = total_seconds / 60;
            tc.minutes = total_minutes % 60;
            tc.hours = (total_minutes / 60) % 24;
        }

        return tc;
    }

    /// Comparison operators
    /// \param other Timecode to compare against (by frame count)
    constexpr auto operator<=>(Timecode const& other) const noexcept -> std::strong_ordering
    {
        // Compare by frame count for proper ordering
        return to_frame_count() <=> other.to_frame_count();
    }

    constexpr auto operator==(Timecode const& other) const noexcept -> bool
    {
        return hours == other.hours && minutes == other.minutes && seconds == other.seconds && frames == other.frames &&
            rate == other.rate;
    }

    /// Format as string HH:MM:SS:FF (or HH:MM:SS;FF for drop frame)
    [[nodiscard]] auto to_string() const -> std::string
    {
        char separator = is_drop_frame(rate) ? ';' : ':';
        return std::format("{:02}:{:02}:{:02}{}{:02}", hours, minutes, seconds, separator, frames);
    }

    /// Parse timecode from string
    /// \param str Timecode string in HH:MM:SS:FF or HH:MM:SS;FF format
    /// \param frame_rate Frame rate to use for the resulting timecode
    /// \return Timecode if valid format (not necessarily valid timecode), nullopt otherwise
    [[nodiscard]] static auto from_string(std::string_view str, FrameRate frame_rate) noexcept -> std::optional<Timecode>;

    /// Create timecode from local time with optional time-of-day offset
    /// \param frame_rate Frame rate to use
    /// \param local_time Local time (from localtime_r/localtime_s)
    /// \param millis Milliseconds within the current second (0-999)
    /// \param offset_seconds Time-of-day offset in seconds (this time maps to SMPTE 00:00:00:00)
    /// \return Timecode representing (given local time - offset)
    [[nodiscard]] static auto from_realtime(
        FrameRate frame_rate, std::tm const& local_time, int32_t millis, int32_t offset_seconds = 0) noexcept -> Timecode;
};

}  // namespace statusbar::ltc
