#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/dsp/dsp_biquad_design.hpp"
#include "statusbar/dsp/dsp_vec_base.hpp"
#include "statusbar/engine/engine_biquad_coeffs.hpp"
#include "statusbar/engine/engine_segment.hpp"
#include "statusbar/itc/itc_message_pipe.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace statusbar::engine {

namespace detail {

// Linearly interpolate the (frequency, Q, gain) parameters of a biquad
// design. Type is taken from b — caller guarantees a.type == b.type
// before calling; type changes are handled by snap, not lerp.
[[nodiscard]] inline auto lerp_biquad_design(
    statusbar::dsp::BiquadDesignParams const& a, statusbar::dsp::BiquadDesignParams const& b, double frac) noexcept
    -> statusbar::dsp::BiquadDesignParams
{
    statusbar::dsp::BiquadDesignParams r{};
    r.type = b.type;
    r.frequency_hz = a.frequency_hz + ((b.frequency_hz - a.frequency_hz) * frac);
    r.q = a.q + ((b.q - a.q) * frac);
    r.gain_db = a.gain_db + ((b.gain_db - a.gain_db) * frac);
    return r;
}

// Pack a single-channel pole/zero coefficient pair into the given
// channel of a (potentially SIMD) BiquadComplexCoeffs. Uses
// dsp::set_flattened_item which addresses any nesting depth uniformly.
template <typename T>
inline void pack_channel_into(
    BiquadComplexCoeffs<T>& dest,
    size_t channel,
    std::pair<statusbar::dsp::ComplexFirstOrderCoeffs<float>, statusbar::dsp::ComplexFirstOrderCoeffs<float>> const& src) noexcept
{
    using statusbar::dsp::set_flattened_item;
    set_flattened_item(dest.stage1.a0_re, src.first.a0_re, channel);
    set_flattened_item(dest.stage1.a0_im, src.first.a0_im, channel);
    set_flattened_item(dest.stage1.a1_re, src.first.a1_re, channel);
    set_flattened_item(dest.stage1.a1_im, src.first.a1_im, channel);
    set_flattened_item(dest.stage1.b1_re, src.first.b1_re, channel);
    set_flattened_item(dest.stage1.b1_im, src.first.b1_im, channel);
    set_flattened_item(dest.stage2.a0_re, src.second.a0_re, channel);
    set_flattened_item(dest.stage2.a0_im, src.second.a0_im, channel);
    set_flattened_item(dest.stage2.a1_re, src.second.a1_re, channel);
    set_flattened_item(dest.stage2.a1_im, src.second.a1_im, channel);
    set_flattened_item(dest.stage2.b1_re, src.second.b1_re, channel);
    set_flattened_item(dest.stage2.b1_im, src.second.b1_im, channel);
}

}  // namespace detail

// BiquadDesigner<T> — Tier 1 producer for biquad-shaped filters.
//
// Templated on T = the audio sample type. The number of independent
// channels comes from dsp::simd_flattened_size<T>:
//   T = float                                — 1 channel
//   T = double                               — 1 channel
//   T = simd_float32x4                       — 4 channels
//   T = simd_float32x8                       — 8 channels
//   T = SIMDVec<simd_float32x4, 2>           — 8 channels (nested)
//
// Each channel keeps independent design state (target, current,
// morph_start, legs_remaining) and runs its own always-3-leg morph or
// 1-leg snap on type change. Per-channel input pipes can be wired
// independently — channels with no pipe just hold whatever set_initial
// established for them.
//
// Once per leg, step() loops over all num_channels:
//   1. Pull latest target from this channel's input pipe (if any).
//   2. Advance this channel's morph state machine, computing the new
//      single-channel design and re-running dsp::design_biquad_pole_zero
//      to get its float coefficient endpoint.
//   3. Pack the result into the appropriate lane of the SIMD-typed
//      BiquadComplexCoeffs<T> using dsp::set_flattened_item (which
//      handles arbitrary nesting depth orthogonally).
// Then publish a single Segment<BiquadComplexCoeffs<T>> covering all
// channels' transitions.
//
// Works uniformly across any nesting depth: scalar, one-level SIMD,
// and nested SIMD all use the same per-channel loop and packing logic.
// Segment arithmetic on nested T is supported via dsp::mul which
// recurses through SIMDVec levels to broadcast the scalar to every
// leaf — see operator*(BiquadComplexCoeffs<T>, float) in
// engine_biquad_coeffs.hpp.

template <typename T = float>
class BiquadDesigner
{
  public:
    static constexpr size_t channel_count = statusbar::dsp::simd_flattened_size<T>::value;

    using coeffs_type = BiquadComplexCoeffs<T>;
    using design_params = statusbar::dsp::BiquadDesignParams;
    using input_pipe_type = itc::LatestPipe<design_params>;
    using output_pipe_type = itc::LatestPipe<Segment<coeffs_type>>;

    explicit BiquadDesigner(double sample_rate_recip) noexcept
        : sample_rate_recip_{sample_rate_recip}
    {}

    BiquadDesigner(BiquadDesigner const&) = delete;
    auto operator=(BiquadDesigner const&) -> BiquadDesigner& = delete;

    // Connect a per-channel input pipe. Channels without a connected
    // pipe simply hold whatever set_initial established.
    void connect_input(size_t channel, input_pipe_type& pipe) noexcept { input_pipes_[channel] = &pipe; }

    // Convenience: connect ALL channels to one pipe (broadcast). Every
    // channel gets the same target updates from this pipe.
    void connect_input_broadcast(input_pipe_type& pipe) noexcept
    {
        for (size_t ch = 0; ch < channel_count; ++ch) {
            input_pipes_[ch] = &pipe;
        }
    }

    void connect_output(output_pipe_type& pipe) noexcept { output_pipe_ = &pipe; }

    // Initialize one channel's design. Other channels stay
    // default-constructed; callers should set_initial() each channel
    // they intend to use before calling step().
    void set_initial(size_t channel, design_params const& params) noexcept
    {
        current_design_[channel] = params;
        target_design_[channel] = params;
        morph_start_[channel] = params;
        legs_remaining_[channel] = 0;
        snap_pending_[channel] = false;
        detail::pack_channel_into(last_endpoint_, channel, statusbar::dsp::design_biquad_pole_zero(params, sample_rate_recip_));
        any_initialized_ = true;
    }

    // Convenience: initialize ALL channels to the same design.
    void set_initial(design_params const& params) noexcept
    {
        for (size_t ch = 0; ch < channel_count; ++ch) {
            set_initial(ch, params);
        }
    }

    // Advance one leg: pull latest targets per channel, advance each
    // channel's morph state, recompute coefficients, pack, publish.
    void step(float recip_samples_per_leg) noexcept
    {
        if (!any_initialized_) {
            return;
        }

        // Pull per-channel inputs and schedule per-channel morphs.
        for (size_t ch = 0; ch < channel_count; ++ch) {
            if (input_pipes_[ch] == nullptr) {
                continue;
            }
            auto incoming = input_pipes_[ch]->try_consume();
            if (!incoming) {
                continue;
            }
            if (incoming->type != target_design_[ch].type) {
                // Type change for this channel — snap rather than morph.
                target_design_[ch] = *incoming;
                current_design_[ch] = *incoming;
                legs_remaining_[ch] = 0;
                snap_pending_[ch] = true;
            } else {
                // Same type — start a fresh 3-leg morph.
                target_design_[ch] = *incoming;
                morph_start_[ch] = current_design_[ch];
                legs_remaining_[ch] = morph_legs;
                snap_pending_[ch] = false;
            }
        }

        // Compute the new SIMD endpoint by advancing each channel's
        // morph step (or holding) and packing into the appropriate lane.
        coeffs_type new_endpoint = last_endpoint_;
        for (size_t ch = 0; ch < channel_count; ++ch) {
            if (snap_pending_[ch]) {
                detail::pack_channel_into(
                    new_endpoint, ch, statusbar::dsp::design_biquad_pole_zero(target_design_[ch], sample_rate_recip_));
                snap_pending_[ch] = false;
            } else if (legs_remaining_[ch] > 0) {
                uint8_t const leg_idx = morph_legs - legs_remaining_[ch];
                double const frac = static_cast<double>(leg_idx + 1) / static_cast<double>(morph_legs);
                current_design_[ch] = detail::lerp_biquad_design(morph_start_[ch], target_design_[ch], frac);
                detail::pack_channel_into(
                    new_endpoint, ch, statusbar::dsp::design_biquad_pole_zero(current_design_[ch], sample_rate_recip_));
                --legs_remaining_[ch];
            }
            // else: this channel is holding — its lanes in new_endpoint
            // already match last_endpoint_ from the initial copy above.
        }

        if (output_pipe_ != nullptr && output_pipe_->can_publish()) {
            output_pipe_->publish(make_segment(last_endpoint_, new_endpoint, recip_samples_per_leg));
        }
        last_endpoint_ = new_endpoint;
    }

    // Read-only inspection for tests / diagnostics.
    [[nodiscard]] auto current_design(size_t channel) const noexcept -> design_params const& { return current_design_[channel]; }
    [[nodiscard]] auto target_design(size_t channel) const noexcept -> design_params const& { return target_design_[channel]; }
    [[nodiscard]] auto last_endpoint() const noexcept -> coeffs_type const& { return last_endpoint_; }
    [[nodiscard]] auto legs_remaining(size_t channel) const noexcept -> uint8_t { return legs_remaining_[channel]; }

  private:
    static constexpr uint8_t morph_legs = 3;

    double sample_rate_recip_;

    std::array<design_params, channel_count> current_design_{};
    std::array<design_params, channel_count> target_design_{};
    std::array<design_params, channel_count> morph_start_{};
    std::array<uint8_t, channel_count> legs_remaining_{};
    std::array<bool, channel_count> snap_pending_{};

    coeffs_type last_endpoint_{};
    bool any_initialized_{false};

    std::array<input_pipe_type*, channel_count> input_pipes_{};
    output_pipe_type* output_pipe_{nullptr};
};

}  // namespace statusbar::engine
