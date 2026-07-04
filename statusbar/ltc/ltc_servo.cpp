// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/ltc/ltc_servo.hpp"

namespace statusbar::ltc {

auto interpolate_sample(std::span<float const> buffer, double position) noexcept -> float
{
    if (buffer.empty()) {
        return 0.0f;
    }

    auto const pos = static_cast<size_t>(position);
    if (pos >= buffer.size()) {
        return 0.0f;
    }

    double const frac = position - static_cast<double>(pos);

    if (pos + 1 < buffer.size()) {
        return (buffer[pos] * (1.0f - static_cast<float>(frac))) + (buffer[pos + 1] * static_cast<float>(frac));
    }

    return buffer[pos];
}

ServoGenerator::ServoGenerator(Generator::SampleRate sample_rate, FrameRate frame_rate, double rise_time_us)
    : generator_(sample_rate, frame_rate, rise_time_us)
    , frame_rate_(frame_rate)
{
    current_timecode_.rate = frame_rate;
    sample_buffer_.reserve(static_cast<size_t>(generator_.samples_per_frame()) * 2);
}

auto ServoGenerator::set_timecode(Timecode const& tc, double reference_timestamp_seconds) -> void
{
    current_timecode_ = tc;
    last_reference_timestamp_ = reference_timestamp_seconds;

    // Generate samples for this frame
    sample_buffer_.clear();
    generator_.generate_frame(tc, sample_buffer_);
    read_position_ = 0.0;
}

auto ServoGenerator::update_timing(double actual_timestamp, double expected_timestamp) -> void
{
    // Calculate phase error (in seconds)
    double const error = actual_timestamp - expected_timestamp;

    // Proportional-integral control: adjust the sample read rate to compensate
    // for drift.
    constexpr double kp = 0.01;   // Proportional gain
    constexpr double ki = 0.001;  // Integral gain

    double const proposed_accum = accumulated_error_ + error;
    double const unclamped = 1.0 + ((kp * error) + (ki * proposed_accum));

    // Clamp adjustment to a reasonable range (+-0.1%).
    phase_adjustment_ = std::clamp(unclamped, 0.999, 1.001);

    // Anti-windup (conditional integration): only commit the integral step when
    // the output isn't saturated, so the integrator can't wind up and overshoot
    // once the error reverses.
    if (phase_adjustment_ == unclamped) {
        accumulated_error_ = proposed_accum;
    }
}

auto ServoGenerator::get_samples(size_t count, std::vector<float>& output) -> void
{
    output.clear();

    for (size_t i = 0; i < count; ++i) {
        // Cross into the next frame once the current one is consumed, carrying
        // the fractional overshoot rather than snapping back to 0 — otherwise
        // ~0.5 sample of phase is dropped at every frame boundary.
        while (read_position_ >= static_cast<double>(sample_buffer_.size())) {
            double const overshoot = read_position_ - static_cast<double>(sample_buffer_.size());
            current_timecode_.increment_frame();
            sample_buffer_.clear();
            generator_.generate_frame(current_timecode_, sample_buffer_);
            read_position_ = overshoot;
        }

        output.push_back(interpolate_sample(sample_buffer_, read_position_));

        // Advance the fractional read position by the servo rate; it persists
        // across get_samples() calls so sub-sample corrections aren't quantized.
        read_position_ += phase_adjustment_;
    }
}

}  // namespace statusbar::ltc
