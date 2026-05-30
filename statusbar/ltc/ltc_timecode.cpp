// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/ltc/ltc_timecode.hpp"

#include <cstdint>
#include <ctime>
#include <cwchar>
#include <optional>
#include <string>
#include <string_view>

namespace statusbar::ltc {

/// Check if a character is an ASCII digit
[[nodiscard]] constexpr auto is_ascii_digit(char c) noexcept -> bool
{
    return c >= '0' && c <= '9';
}

/// Check if two characters are both ASCII digits
[[nodiscard]] constexpr auto are_two_ascii_digits(char c1, char c2) noexcept -> bool
{
    return is_ascii_digit(c1) && is_ascii_digit(c2);
}

/// Check if the frame rate string matches 29.97 NDF variants
[[nodiscard]] auto is_frame_rate_29_97_ndf(std::string_view str) noexcept -> bool
{
    return str == "29.97" || str == "29.97ndf" || str == "29.97NDF";
}

/// Check if the frame rate string matches 30 NDF variants
[[nodiscard]] auto is_frame_rate_30_ndf(std::string_view str) noexcept -> bool
{
    return str == "30" || str == "30ndf" || str == "30NDF";
}

auto frame_rate_name(FrameRate rate) -> std::string
{
    switch (rate) {
        case FrameRate::Rate_23_976:
            return "23.976 fps";
        case FrameRate::Rate_24:
            return "24 fps";
        case FrameRate::Rate_25:
            return "25 fps";
        case FrameRate::Rate_29_97_DF:
            return "29.97 fps DF";
        case FrameRate::Rate_29_97_NDF:
            return "29.97 fps NDF";
        case FrameRate::Rate_30_DF:
            return "30 fps DF";
        case FrameRate::Rate_30_NDF:
            return "30 fps NDF";
    }
    return "Unknown";
}

auto parse_frame_rate(std::string_view str) noexcept -> std::optional<FrameRate>
{
    if (str == "23.976") {
        return FrameRate::Rate_23_976;
    }
    if (str == "24") {
        return FrameRate::Rate_24;
    }
    if (str == "25") {
        return FrameRate::Rate_25;
    }
    if (str == "29.97df" || str == "29.97DF") {
        return FrameRate::Rate_29_97_DF;
    }
    if (is_frame_rate_29_97_ndf(str)) {
        return FrameRate::Rate_29_97_NDF;
    }
    if (str == "30df" || str == "30DF") {
        return FrameRate::Rate_30_DF;
    }
    if (is_frame_rate_30_ndf(str)) {
        return FrameRate::Rate_30_NDF;
    }
    return std::nullopt;
}

auto Timecode::from_string(std::string_view str, FrameRate frame_rate) noexcept -> std::optional<Timecode>
{
    // Parse HH:MM:SS:FF or HH:MM:SS;FF format
    if (str.length() != 11) {
        return std::nullopt;
    }

    auto const is_valid_separator = [](char c) noexcept { return c == ':' || c == ';'; };
    char const sep = str[8];
    if (str[2] != ':' || str[5] != ':' || !is_valid_separator(sep)) {
        return std::nullopt;
    }

    // Parse each component - manual parsing to avoid exceptions
    auto parse_two_digits = [](std::string_view const s, size_t const pos) -> std::optional<uint8_t> {
        if (pos + 2 > s.length()) {
            return std::nullopt;
        }
        char const c1 = s[pos];
        char const c2 = s[pos + 1];
        if (!are_two_ascii_digits(c1, c2)) {
            return std::nullopt;
        }
        return static_cast<uint8_t>(((c1 - '0') * 10) + (c2 - '0'));
    };

    auto const h = parse_two_digits(str, 0);
    auto const m = parse_two_digits(str, 3);
    auto const s = parse_two_digits(str, 6);
    auto const f = parse_two_digits(str, 9);
    if (!h.has_value() || !m.has_value() || !s.has_value() || !f.has_value()) {
        return std::nullopt;
    }

    Timecode tc;
    tc.hours = *h;
    tc.minutes = *m;
    tc.seconds = *s;
    tc.frames = *f;
    tc.rate = frame_rate;

    return tc;  // Caller should validate with is_valid() if needed
}

auto Timecode::from_realtime(FrameRate frame_rate, std::tm const& local_time, int32_t millis, int32_t offset_seconds) noexcept
    -> Timecode
{
    uint8_t const fps = frames_per_second(frame_rate);

    // Calculate current time-of-day in seconds
    int32_t const current_seconds = (local_time.tm_hour * 3600) + (local_time.tm_min * 60) + local_time.tm_sec;

    // Apply offset (current time - offset time = SMPTE time)
    int32_t smpte_seconds = current_seconds - offset_seconds;

    // Handle wrap-around (negative means we're before the offset time today)
    if (smpte_seconds < 0) {
        smpte_seconds += 24 * 3600;  // Wrap to previous day
    }

    // Handle 24-hour wrap
    smpte_seconds = smpte_seconds % (24 * 3600);

    Timecode tc;
    tc.rate = frame_rate;
    tc.hours = static_cast<uint8_t>(smpte_seconds / 3600);
    tc.minutes = static_cast<uint8_t>((smpte_seconds % 3600) / 60);
    tc.seconds = static_cast<uint8_t>(smpte_seconds % 60);
    tc.frames = static_cast<uint8_t>((millis * fps) / 1000);

    // For drop frame, adjust if we landed on a dropped frame
    if (is_drop_frame(frame_rate) && tc.is_drop_frame_skip()) {
        tc.frames = 2;
    }

    return tc;
}

}  // namespace statusbar::ltc
