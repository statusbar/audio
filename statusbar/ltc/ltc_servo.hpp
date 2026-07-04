#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// SMPTE LTC Clock Servo
// Compensates for drift between audio sample clock and SMPTE reference clock
// Supports multiple frame rates per SMPTE 12M

#include "statusbar/ltc/ltc_generator.hpp"
#include "statusbar/ltc/ltc_timecode.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace statusbar::ltc {

/// Interpolate a sample from a buffer at a fractional position
/// Returns 0.0f if pos is out of bounds
[[nodiscard]] auto interpolate_sample(std::span<float const> buffer, double position) noexcept -> float;

/// Servo-controlled LTC generator
/// Adjusts sample generation to maintain sync between audio clock and SMPTE reference
class ServoGenerator
{
  public:
    /// Construct servo generator
    /// @param sample_rate Audio sample rate (44.1kHz, 48kHz, or 96kHz)
    /// @param frame_rate SMPTE frame rate
    /// @param rise_time_us Rise/fall time in microseconds (SMPTE 12M: 25-250µs, default 40µs)
    explicit ServoGenerator(
        Generator::SampleRate sample_rate, FrameRate frame_rate = FrameRate::Rate_30_NDF, double rise_time_us = 40.0);

    /// Set current timecode and reference timestamp
    /// Call this at each video frame boundary with the SMPTE time
    /// @param tc SMPTE timecode to set
    /// @param reference_timestamp_seconds Wall-clock timestamp for drift compensation (0.0 to skip)
    auto set_timecode(Timecode const& tc, double reference_timestamp_seconds = 0.0) -> void;

    /// Update timing information (call when new frame timing is available)
    /// @param actual_timestamp Actual time when frame occurred
    /// @param expected_timestamp Expected time based on sample count
    auto update_timing(double actual_timestamp, double expected_timestamp) -> void;

    /// Get audio samples
    /// @param count Number of samples to generate
    /// @param output Output vector to fill with samples (will be resized to count)
    /// Caller should reserve space to avoid reallocations
    auto get_samples(size_t count, std::vector<float>& output) -> void;

    /// Get current timecode
    [[nodiscard]] auto current_timecode() const noexcept -> Timecode const& { return current_timecode_; }

    /// Get SMPTE frame rate
    [[nodiscard]] auto frame_rate() const noexcept -> FrameRate { return frame_rate_; }

    /// Get phase adjustment factor (1.0 = no adjustment)
    [[nodiscard]] auto phase_adjustment() const noexcept -> double { return phase_adjustment_; }

    /// Reset servo state
    auto reset() noexcept -> void
    {
        accumulated_error_ = 0.0;
        phase_adjustment_ = 1.0;
        read_position_ = 0.0;
    }

  private:
    Generator generator_;                   // LTC generator
    FrameRate frame_rate_;                  // SMPTE frame rate
    Timecode current_timecode_;             // Current timecode
    std::vector<float> sample_buffer_;      // Sample buffer for current frame
    double read_position_{0.0};             // Fractional read position, persisted across get_samples() calls
    double accumulated_error_{0.0};         // Accumulated timing error
    double phase_adjustment_{1.0};          // Current phase adjustment factor
    double last_reference_timestamp_{0.0};  // Last reference timestamp
};

}  // namespace statusbar::ltc
