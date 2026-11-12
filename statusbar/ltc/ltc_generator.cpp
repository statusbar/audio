// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/ltc/ltc_generator.hpp"

#include <algorithm>

namespace statusbar::ltc {

namespace {

/// Clamp a rise time in samples to the SMPTE 12M range (25-250 microseconds).
auto clamp_rise_time(size_t rise_time_samples, uint32_t sample_rate) noexcept -> size_t
{
    size_t const min_rise = static_cast<size_t>(25.0e-6 * sample_rate);
    size_t const max_rise = static_cast<size_t>(250.0e-6 * sample_rate);
    return std::clamp(rise_time_samples, min_rise, max_rise);
}

}  // namespace

Generator::Generator(SampleRate sample_rate, FrameRate frame_rate, double rise_time_us) noexcept
    : sample_rate_(static_cast<uint32_t>(sample_rate))
    , frame_rate_(frame_rate)
    , frame_ratio_(frame_rate_ratio(frame_rate))
    , rise_time_samples_(clamp_rise_time(static_cast<size_t>((rise_time_us * 1e-6) * sample_rate_), sample_rate_))
{
    // The exact frame length is sample_rate * denominator / numerator samples
    // (1601.6 at 48 kHz / 29.97); each frame is that rounded down or up as the
    // carry dictates, so samples_per_frame_ is the rounded-up bound.
    uint64_t const scaled = static_cast<uint64_t>(sample_rate_) * frame_ratio_.denominator;
    samples_per_frame_ = static_cast<uint32_t>((scaled + frame_ratio_.numerator - 1) / frame_ratio_.numerator);

    // LTC bit rate is 80 bits per frame: bits_per_second = 80 * fps
    samples_per_bit_ = sample_rate_ / (80.0 * actual_frame_rate(frame_rate_));
}

auto Generator::next_frame_length() noexcept -> size_t
{
    // Exact rational accumulation: the frame length is sample_rate * den / num
    // samples; the remainder below one sample is carried to the next frame.
    uint64_t const scaled = (static_cast<uint64_t>(sample_rate_) * frame_ratio_.denominator) + frame_remainder_;
    frame_remainder_ = static_cast<uint32_t>(scaled % frame_ratio_.numerator);
    return static_cast<size_t>(scaled / frame_ratio_.numerator);
}

auto Generator::generate_frame(Timecode const& tc, std::vector<float>& samples) -> void
{
    LTCFrame const frame(tc);
    size_t const frame_length = next_frame_length();
    EncoderState state = begin_frame(samples);
    for (size_t bit_idx = 0; bit_idx < 80; ++bit_idx) {
        encode_bit(frame.get_bit(bit_idx), state, samples, frame_length);
    }
    finish_frame(state, samples, frame_length);
}

auto Generator::begin_frame(std::vector<float>& samples) const noexcept -> EncoderState
{
    samples.clear();
    // The level (-1.0 or +1.0) is carried from the previous frame so the
    // biphase waveform is continuous across frame boundaries. With a spec-correct
    // polarity-correction bit each frame has an even transition count, so this
    // returns to last_level_ at the end and the stream never doubles a boundary
    // transition.
    return EncoderState{
        .level = last_level_,
        .prev_level = last_level_,
        .samples_since_transition = rise_time_samples_,  // Start settled
        .bit_position = 0.0,
    };
}

auto Generator::finish_frame(EncoderState& state, std::vector<float>& samples, size_t frame_length) noexcept -> void
{
    // Pad to this frame's exact length, then truncate if we generated too many.
    emit_level(state, samples, frame_length);
    samples.resize(frame_length);
    last_level_ = state.level;
}

auto Generator::encode_bit(bool bit_value, EncoderState& state, std::vector<float>& samples, size_t frame_length) const -> void
{
    // Biphase mark encoding: always transition at the start of the bit period,
    // and again at the centre if the bit is 1.
    transition(state);
    emit_half_bit(state, samples);
    if (bit_value) {
        transition(state);
    }
    emit_half_bit(state, samples);

    // Account for fractional samples per bit.
    state.bit_position += samples_per_bit_;
    emit_level(state, samples, std::min(static_cast<size_t>(state.bit_position), frame_length));
}

auto Generator::emit_half_bit(EncoderState& state, std::vector<float>& samples) const -> void
{
    emit_level(state, samples, samples.size() + static_cast<size_t>(samples_per_bit_ / 2.0));
}

auto Generator::transition(EncoderState& state) noexcept -> void
{
    state.prev_level = state.level;
    state.level = -state.level;
    state.samples_since_transition = 0;
}

auto Generator::emit_level(EncoderState& state, std::vector<float>& samples, size_t up_to) const -> void
{
    while (samples.size() < up_to) {
        samples.push_back(apply_rise_fall(state.prev_level, state.level, state.samples_since_transition++));
    }
}

}  // namespace statusbar::ltc
