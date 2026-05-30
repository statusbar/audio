#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// SMPTE LTC Audio Generator
// Generates audio samples from LTC frames using biphase mark encoding
// Supports multiple frame rates per SMPTE 12M

#include "statusbar/ltc/ltc_frame.hpp"
#include "statusbar/ltc/ltc_timecode.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace statusbar::ltc {

/// LTC audio sample generator
/// Generates square wave audio samples using biphase mark (Manchester) encoding
class Generator
{
  public:
    /// Supported audio sample rates
    enum class SampleRate : uint32_t
    {
        Rate_44100 = 44100,
        Rate_48000 = 48000,
        Rate_96000 = 96000
    };

    /// Construct generator with sample rate, frame rate, and optional rise/fall time
    /// @param sample_rate Audio sample rate (44.1kHz, 48kHz, or 96kHz)
    /// @param frame_rate SMPTE frame rate
    /// @param rise_time_us Rise/fall time in microseconds (SMPTE 12M: 25-250µs, default 40µs)
    explicit Generator(SampleRate sample_rate, FrameRate frame_rate = FrameRate::Rate_30_NDF, double rise_time_us = 40.0) noexcept;

    /// Generate audio samples for one frame
    /// @param tc Timecode to encode
    /// @param samples Output vector to fill with samples (will be resized to samples_per_frame)
    /// Caller should reserve space using samples_per_frame() to avoid reallocations
    auto generate_frame(Timecode const& tc, std::vector<float>& samples) -> void;

    /// Get audio sample rate
    [[nodiscard]] constexpr auto sample_rate() const noexcept -> uint32_t { return sample_rate_; }

    /// Get SMPTE frame rate
    [[nodiscard]] constexpr auto frame_rate() const noexcept -> FrameRate { return frame_rate_; }

    /// Get number of samples per frame
    [[nodiscard]] constexpr auto samples_per_frame() const noexcept -> uint32_t { return samples_per_frame_; }

    /// Get number of samples per bit (may be fractional)
    [[nodiscard]] constexpr auto samples_per_bit() const noexcept -> double { return samples_per_bit_; }

  private:
    /// Apply rise/fall time shaping using linear interpolation
    /// @param from_level Starting level
    /// @param to_level Target level
    /// @param samples_since_transition Samples elapsed since transition began
    /// @return Interpolated sample value
    [[nodiscard]] constexpr auto apply_rise_fall(float from_level, float to_level, size_t samples_since_transition) const noexcept
        -> float
    {
        if (samples_since_transition >= rise_time_samples_) {
            // Transition complete, return target level
            return to_level;
        }

        // Linear interpolation during rise/fall time
        float const t = static_cast<float>(samples_since_transition) / static_cast<float>(rise_time_samples_);
        return from_level + ((to_level - from_level) * t);
    }

    uint32_t sample_rate_;        // Audio sample rate (Hz)
    FrameRate frame_rate_;        // SMPTE frame rate
    uint32_t samples_per_frame_;  // Samples per video frame
    double samples_per_bit_;      // Samples per LTC bit
    size_t rise_time_samples_;    // Rise/fall time in samples (SMPTE 12M: 25-250µs)
};

}  // namespace statusbar::ltc
