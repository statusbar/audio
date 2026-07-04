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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <optional>
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

auto process_servo_callback(
    ServoPlaybackState& state,
    std::tm const& local_time,
    int32_t millis,
    double current_epoch,
    audio::AudioCallbackParamsFloat const& params) -> Status
{
    using namespace std::chrono;

    if (!state.is_running()) {
        return failure(audio::AudioError::StreamNotRunning);
    }

    auto frames = params.output_buffers[0].sample.size();

    // Get samples from servo generator
    state.servo->get_samples(frames, state.sample_buffer);

    // Copy to output
    for (size_t i = 0; i < frames; ++i) {
        params.output_buffers[0].sample[i] = state.sample_buffer[i];
    }

    state.total_samples_generated += frames;

    // Check if 1 second has elapsed since last update
    if (current_epoch - state.last_update_epoch >= 1.0) {
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
        auto const& cur_tc = state.servo->current_timecode();
        int64_t const frame_diff =
            static_cast<int64_t>(rt_tc.to_frame_count()) - static_cast<int64_t>(cur_tc.to_frame_count());
        if ((frame_diff < 0 ? -frame_diff : frame_diff) >= 2) {
            state.servo->set_timecode(rt_tc, current_epoch);
        }
    }

    return success();
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
