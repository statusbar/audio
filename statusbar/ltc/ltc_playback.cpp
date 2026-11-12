// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Implementation of LTC playback functions and base classes

#include "statusbar/ltc/ltc_playback.hpp"

#include "statusbar/audio/audio.hpp"
#include "statusbar/ltc/ltc.hpp"
#include "statusbar/ltc/ltc_generator.hpp"
#include "statusbar/ltc/ltc_servo.hpp"
#include "statusbar/ltc/ltc_timecode.hpp"
#include "statusbar/status/status.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <optional>
#include <span>
#include <string_view>

namespace statusbar::ltc {

//
// Free function implementations
//

auto parse_time_offset(std::string_view str) noexcept -> std::optional<int32_t>
{
    // Expected format: HH:MM:SS (8 characters)
    if (str.length() != 8) {
        return std::nullopt;
    }

    if (str[2] != ':' || str[5] != ':') {
        return std::nullopt;
    }

    auto is_ascii_digit = [](char c) -> bool { return c >= '0' && c <= '9'; };
    auto parse_two_digits = [&is_ascii_digit](std::string_view s, size_t pos) -> std::optional<int32_t> {
        if (pos + 2 > s.length()) {
            return std::nullopt;
        }
        char const c1 = s[pos];
        char const c2 = s[pos + 1];
        if (!is_ascii_digit(c1) || !is_ascii_digit(c2)) {
            return std::nullopt;
        }
        return ((c1 - '0') * 10) + (c2 - '0');
    };

    auto const h = parse_two_digits(str, 0);
    auto const m = parse_two_digits(str, 3);
    auto const s = parse_two_digits(str, 6);
    if (!h.has_value() || !m.has_value() || !s.has_value()) {
        return std::nullopt;
    }

    auto const is_valid_hms = [](int32_t hh, int32_t mm, int32_t ss) { return hh <= 23 && mm <= 59 && ss <= 59; };
    if (!is_valid_hms(*h, *m, *s)) {
        return std::nullopt;
    }

    return (*h * 3600) + (*m * 60) + *s;
}

auto get_local_time_info() noexcept -> LocalTimeInfo
{
    using namespace std::chrono;

    auto now = system_clock::now();
    auto time_t_now = system_clock::to_time_t(now);
    auto duration_since_epoch = now.time_since_epoch();
    auto millis = static_cast<int32_t>(duration_cast<milliseconds>(duration_since_epoch).count() % 1000);
    auto epoch_seconds = duration_cast<duration<double>>(duration_since_epoch).count();

    std::tm tm_local{};
#if defined(_WIN32)
    localtime_s(&tm_local, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_local);
#endif

    return {.local_time = tm_local, .millis = millis, .epoch_seconds = epoch_seconds};
}

auto utc_offset_seconds_at(double epoch_seconds) noexcept -> int32_t
{
    auto const when = static_cast<std::time_t>(std::floor(epoch_seconds));
    std::tm tm_local{};
#if defined(_WIN32)
    localtime_s(&tm_local, &when);
    std::tm tm_utc{};
    gmtime_s(&tm_utc, &when);
    int32_t const local_day_seconds = (tm_local.tm_hour * 3600) + (tm_local.tm_min * 60) + tm_local.tm_sec;
    int32_t const utc_day_seconds = (tm_utc.tm_hour * 3600) + (tm_utc.tm_min * 60) + tm_utc.tm_sec;
    int32_t offset = local_day_seconds - utc_day_seconds;
    if (tm_local.tm_yday != tm_utc.tm_yday) {
        offset += (tm_local.tm_yday > tm_utc.tm_yday || tm_local.tm_year > tm_utc.tm_year) ? 86400 : -86400;
    }
    return offset;
#else
    localtime_r(&when, &tm_local);
    return static_cast<int32_t>(tm_local.tm_gmtoff);
#endif
}

namespace {

/// The time-of-day fields of a std::tm for a second count within a day.
auto time_of_day(int64_t day_seconds) noexcept -> std::tm
{
    std::tm tm_local{};
    tm_local.tm_hour = static_cast<int>(day_seconds / 3600);
    tm_local.tm_min = static_cast<int>((day_seconds % 3600) / 60);
    tm_local.tm_sec = static_cast<int>(day_seconds % 60);
    return tm_local;
}

/// Seconds into the day for a local second count, positive for any input.
auto seconds_into_day(int64_t local_seconds) noexcept -> int64_t
{
    int64_t const day_seconds = local_seconds % 86400;
    return day_seconds < 0 ? day_seconds + 86400 : day_seconds;
}

}  // namespace

auto local_time_from_epoch(double epoch_seconds, int32_t utc_offset_seconds) noexcept -> LocalTimeInfo
{
    double const whole = std::floor(epoch_seconds);
    int64_t const local_seconds = static_cast<int64_t>(whole) + utc_offset_seconds;
    auto const millis = static_cast<int32_t>((epoch_seconds - whole) * 1000.0);
    return {
        .local_time = time_of_day(seconds_into_day(local_seconds)),
        .millis = std::clamp<int32_t>(millis, 0, 999),
        .epoch_seconds = epoch_seconds};
}

namespace {

/// Fill @p output from the servo without growing state.sample_buffer: frames
/// past its capacity are silence and counted in short_periods, because the
/// audio thread may not allocate.
auto render_servo_samples(ServoPlaybackState& state, std::span<float> output) -> void
{
    size_t const usable = std::min(output.size(), state.sample_buffer.capacity());
    state.servo->get_samples(usable, state.sample_buffer);
    std::copy_n(state.sample_buffer.begin(), usable, output.begin());
    std::fill(output.begin() + static_cast<std::ptrdiff_t>(usable), output.end(), 0.0F);
    if (usable < output.size()) {
        ++state.short_periods;
    }
}

/// Once a second: steer the servo by actual versus sample-count time, and
/// resync the timecode from the realtime clock when it has jumped.
auto update_servo_from_clock(ServoPlaybackState& state, std::tm const& local_time, int32_t millis, double current_epoch) -> void
{
    // Get expected time based on sample count
    double const expected_time =
        state.start_epoch + (static_cast<double>(state.total_samples_generated) / static_cast<double>(state.sample_rate));

    // Update servo with actual vs expected timing
    state.servo->update_timing(current_epoch, expected_time);
    state.last_update_epoch = current_epoch;

    // Resync from the realtime clock only when the timecode has jumped by a
    // whole frame or more. Small drift is absorbed smoothly by the servo's
    // rate adjustment above; a hard set_timecode mid-frame would truncate the
    // current frame's waveform, so reserve it for a genuine discontinuity.
    auto const rt_tc = Timecode::from_realtime(state.frame_rate, local_time, millis, state.time_offset_seconds);
    int64_t const frame_diff =
        static_cast<int64_t>(rt_tc.to_frame_count()) - static_cast<int64_t>(state.servo->current_timecode().to_frame_count());
    if ((frame_diff < 0 ? -frame_diff : frame_diff) >= 2) {
        state.servo->set_timecode(rt_tc, current_epoch);
    }
}

}  // namespace

auto process_servo_callback(
    ServoPlaybackState& state,
    std::tm const& local_time,
    int32_t millis,
    double current_epoch,
    audio::AudioCallbackParamsFloat const& params) -> Status
{
    if (!state.is_running()) {
        return failure(audio::AudioError::StreamNotRunning);
    }

    render_servo_samples(state, params.output_buffers[0].sample);
    state.total_samples_generated += params.output_buffers[0].sample.size();

    // Check if 1 second has elapsed since last update
    if (current_epoch - state.last_update_epoch >= 1.0) {
        update_servo_from_clock(state, local_time, millis, current_epoch);
    }

    return success();
}

auto process_servo_callback(ServoPlaybackState& state, double current_epoch, audio::AudioCallbackParamsFloat const& params)
    -> Status
{
    auto const local = local_time_from_epoch(current_epoch, state.utc_offset_seconds.load(std::memory_order_relaxed));
    return process_servo_callback(state, local.local_time, local.millis, current_epoch, params);
}

auto process_fixed_callback(FixedPlaybackState& state, audio::AudioCallbackParamsFloat const& params) -> Status
{
    auto const& output = params.output_buffers[0].sample;
    auto frames = output.size();

    for (size_t i = 0; i < frames; ++i) {
        // Generate new frame if needed
        if (state.frame_position == 0 || state.frame_buffer.empty()) {
            if (state.frames_generated >= state.total_frames) {
                // Duration reached - fill rest with silence and signal completion
                state.set_finished(true);
                for (auto j = i; j < frames; ++j) {
                    output[j] = 0.0F;
                }
                return failure(audio::AudioError::StreamNotRunning);
            }

            state.frame_buffer.clear();
            state.generator->generate_frame(state.current_tc, state.frame_buffer);
            state.current_tc.increment_frame();
            state.frames_generated++;
            state.frame_position = 0;
        }

        // Copy sample from frame buffer
        if (state.frame_position < state.frame_buffer.size()) {
            output[i] = state.frame_buffer[state.frame_position++];
        } else {
            output[i] = 0.0F;  // Safety fallback
        }

        // Reset position when frame is complete
        if (state.frame_position >= state.frame_buffer.size()) {
            state.frame_position = 0;
        }
    }

    return success();
}

//
// ServoPlaybackStateBase implementation
//

ServoPlaybackStateBase::ServoPlaybackStateBase() noexcept
    : running_(true)
{}

auto ServoPlaybackStateBase::is_running() const noexcept -> bool
{
    return running_.load(std::memory_order_acquire);
}

auto ServoPlaybackStateBase::set_running(bool value) noexcept -> void
{
    running_.store(value, std::memory_order_release);
}

//
// FixedPlaybackStateBase implementation
//

FixedPlaybackStateBase::FixedPlaybackStateBase() noexcept
    : finished_(false)
{}

auto FixedPlaybackStateBase::is_finished() const noexcept -> bool
{
    return finished_.load(std::memory_order_acquire);
}

auto FixedPlaybackStateBase::set_finished(bool value) noexcept -> void
{
    finished_.store(value, std::memory_order_release);
}

}  // namespace statusbar::ltc
