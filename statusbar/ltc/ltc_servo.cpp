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
    buffer_position_ = 0;
}

auto ServoGenerator::update_timing(double actual_timestamp, double expected_timestamp) -> void
{
    // Calculate phase error (in seconds)
    double const error = actual_timestamp - expected_timestamp;

    // Accumulate error for integral control
    accumulated_error_ += error;

    // Simple proportional-integral control
    // Adjust sample generation rate to compensate for drift
    constexpr double kp = 0.01;   // Proportional gain
    constexpr double ki = 0.001;  // Integral gain

    phase_adjustment_ = 1.0 + ((kp * error) + (ki * accumulated_error_));

    // Clamp adjustment to reasonable range (+-0.1%)
    phase_adjustment_ = std::clamp(phase_adjustment_, 0.999, 1.001);
}

auto ServoGenerator::get_samples(size_t count, std::vector<float>& output) -> void
{
    output.clear();

    // Apply phase adjustment by slightly varying sample output rate
    double adjusted_position = static_cast<double>(buffer_position_);

    for (size_t i = 0; i < count; ++i) {
        // Check if we need to generate next frame
        if (buffer_position_ >= sample_buffer_.size()) {
            // Move to next frame
            current_timecode_.increment_frame();
            sample_buffer_.clear();
            generator_.generate_frame(current_timecode_, sample_buffer_);
            buffer_position_ = 0;
            adjusted_position = 0.0;
        }

        output.push_back(interpolate_sample(sample_buffer_, adjusted_position));

        // Advance position with servo adjustment
        adjusted_position += phase_adjustment_;
        buffer_position_ = static_cast<size_t>(adjusted_position);
    }
}

}  // namespace statusbar::ltc
