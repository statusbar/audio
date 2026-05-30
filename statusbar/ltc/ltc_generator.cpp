// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/ltc/ltc_generator.hpp"

namespace statusbar::ltc {

Generator::Generator(SampleRate sample_rate, FrameRate frame_rate, double rise_time_us) noexcept
    : sample_rate_(static_cast<uint32_t>(sample_rate))
    , frame_rate_(frame_rate)
    , rise_time_samples_(static_cast<size_t>((rise_time_us * 1e-6) * sample_rate_))
{
    // Calculate samples per frame based on actual frame rate
    double const fps = actual_frame_rate(frame_rate_);
    samples_per_frame_ = static_cast<uint32_t>(std::lround(sample_rate_ / fps));

    // LTC bit rate is 80 bits per frame
    // bits_per_second = 80 * fps
    double const bits_per_second = 80.0 * fps;
    samples_per_bit_ = sample_rate_ / bits_per_second;

    // Clamp rise time to SMPTE 12M specification (25-250 microseconds)
    size_t const min_rise = static_cast<size_t>(25.0e-6 * sample_rate_);
    size_t const max_rise = static_cast<size_t>(250.0e-6 * sample_rate_);
    if (rise_time_samples_ < min_rise) {
        rise_time_samples_ = min_rise;
    }
    if (rise_time_samples_ > max_rise) {
        rise_time_samples_ = max_rise;
    }
}

auto Generator::generate_frame(Timecode const& tc, std::vector<float>& samples) -> void
{
    LTCFrame const frame(tc);
    samples.clear();

    // Current level (-1.0 or +1.0)
    float level = 1.0F;
    float prev_level = level;

    // Sample position within current bit
    double bit_position = 0.0;

    // Track transition state for rise/fall time shaping
    size_t samples_since_transition = rise_time_samples_;  // Start settled

    // Generate samples for all 80 bits
    for (size_t bit_idx = 0; bit_idx < 80; ++bit_idx) {
        bool const bit_value = frame.get_bit(bit_idx);

        // Biphase mark encoding:
        // - Always transition at start of bit period
        // - Additional transition at center if bit is 1
        prev_level = level;
        level = -level;  // Transition at bit start
        samples_since_transition = 0;

        // First half of bit
        size_t const half_bit_samples = static_cast<size_t>(samples_per_bit_ / 2.0);
        for (size_t i = 0; i < half_bit_samples; ++i) {
            samples.push_back(apply_rise_fall(prev_level, level, samples_since_transition++));
        }

        // If bit is 1, transition at center
        if (bit_value) {
            prev_level = level;
            level = -level;
            samples_since_transition = 0;
        }

        // Second half of bit
        for (size_t i = 0; i < half_bit_samples; ++i) {
            samples.push_back(apply_rise_fall(prev_level, level, samples_since_transition++));
        }

        // Account for fractional samples
        bit_position += samples_per_bit_;
        size_t const expected_samples = static_cast<size_t>(bit_position);
        while (samples.size() < expected_samples && samples.size() < samples_per_frame_) {
            samples.push_back(apply_rise_fall(prev_level, level, samples_since_transition++));
        }
    }

    // Pad to exact frame length if needed
    while (samples.size() < samples_per_frame_) {
        samples.push_back(apply_rise_fall(prev_level, level, samples_since_transition++));
    }

    // Truncate if we generated too many samples
    samples.resize(samples_per_frame_);
}

}  // namespace statusbar::ltc
