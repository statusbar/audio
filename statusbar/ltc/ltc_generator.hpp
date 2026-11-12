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
    /// @param samples Output vector to fill with samples. Its size is
    ///        samples_per_frame() or one less: the fractional part of the
    ///        exact frame length (1601.6 at 48 kHz / 29.97) is carried from
    ///        frame to frame, so a run of frames consumes exactly
    ///        samples_for_frames() samples and the stream keeps the rate.
    /// Caller should reserve space using samples_per_frame() to avoid reallocations
    auto generate_frame(Timecode const& tc, std::vector<float>& samples) -> void;

    /// Get audio sample rate
    [[nodiscard]] constexpr auto sample_rate() const noexcept -> uint32_t { return sample_rate_; }

    /// Get SMPTE frame rate
    [[nodiscard]] constexpr auto frame_rate() const noexcept -> FrameRate { return frame_rate_; }

    /// Get the longest frame in samples (the exact length rounded up); every
    /// frame generate_frame() produces is this long or one sample shorter.
    [[nodiscard]] constexpr auto samples_per_frame() const noexcept -> uint32_t { return samples_per_frame_; }

    /// Exact number of samples @p frames consecutive frames occupy from a
    /// fresh generator: floor(frames * sample_rate / fps).
    /// @param frames Number of frames
    [[nodiscard]] constexpr auto samples_for_frames(uint64_t frames) const noexcept -> uint64_t
    {
        return frames * static_cast<uint64_t>(sample_rate_) * frame_ratio_.denominator / frame_ratio_.numerator;
    }

    /// Get number of samples per bit (may be fractional)
    [[nodiscard]] constexpr auto samples_per_bit() const noexcept -> double { return samples_per_bit_; }

    /// Reset biphase level continuity to a known start polarity and the
    /// fractional-sample carry. Call when starting a fresh, non-contiguous
    /// stream so the first frame is deterministic.
    constexpr auto reset() noexcept -> void
    {
        last_level_ = 1.0F;
        frame_remainder_ = 0;
    }

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

    /// Biphase encoder state while one frame is being generated.
    struct EncoderState
    {
        float level;                      // Current level (-1.0 or +1.0)
        float prev_level;                 // Level before the latest transition
        size_t samples_since_transition;  // For rise/fall shaping
        double bit_position;              // Exact sample position of the next bit boundary
    };

    /// Length of the next frame in samples, advancing the fractional carry.
    [[nodiscard]] auto next_frame_length() noexcept -> size_t;
    /// Clear @p samples and start the encoder at the carried level.
    [[nodiscard]] auto begin_frame(std::vector<float>& samples) const noexcept -> EncoderState;
    /// Pad or trim to @p frame_length and carry the level into the next frame.
    auto finish_frame(EncoderState& state, std::vector<float>& samples, size_t frame_length) noexcept -> void;
    /// Append one biphase-mark bit, never past @p frame_length samples.
    auto encode_bit(bool bit_value, EncoderState& state, std::vector<float>& samples, size_t frame_length) const -> void;
    /// Flip the level and restart the rise/fall shaping.
    static auto transition(EncoderState& state) noexcept -> void;
    /// Append half a bit period of samples at the current level.
    auto emit_half_bit(EncoderState& state, std::vector<float>& samples) const -> void;
    /// Append shaped samples at the current level until @p samples holds @p up_to.
    auto emit_level(EncoderState& state, std::vector<float>& samples, size_t up_to) const -> void;

    uint32_t sample_rate_;          // Audio sample rate (Hz)
    FrameRate frame_rate_;          // SMPTE frame rate
    FrameRateRatio frame_ratio_;    // Exact frame rate as numerator/denominator
    uint32_t samples_per_frame_;    // Longest frame in samples (exact length rounded up)
    uint32_t frame_remainder_ = 0;  // Fractional-sample carry, in units of 1/numerator sample
    double samples_per_bit_;        // Samples per LTC bit
    size_t rise_time_samples_;      // Rise/fall time in samples (SMPTE 12M: 25-250µs)
    float last_level_ = 1.0F;       // Biphase signal level carried across frames for continuity
};

}  // namespace statusbar::ltc
