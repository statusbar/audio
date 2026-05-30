#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/dsp/dsp_vec_base.hpp"
#include "statusbar/engine/engine_gain_coeffs.hpp"
#include "statusbar/engine/engine_segment.hpp"
#include "statusbar/itc/itc_message_pipe.hpp"

#include <array>
#include <cstddef>

namespace statusbar::engine {

// GainDesigner<T> — Tier 1 producer for a single Tier 0 gain element.
//
// Templated on T = the audio sample type. The number of independent
// channels comes from dsp::simd_flattened_size<T>:
//   T = float                                — 1 channel
//   T = simd_float32x4                       — 4 channels
//   T = SIMDVec<simd_float32x4, 2>           — 8 channels (nested)
//
// Each channel keeps its own target amplitude, set independently via
// set_initial(channel, ...) and updated independently via per-channel
// itc::LatestPipe<float> input. Per-channel input pipes can be wired
// independently — channels with no pipe just hold whatever set_initial
// established.
//
// Once per leg, step() loops over all channel_count channels, packs each
// channel's latest target into the appropriate lane of a SIMD-typed
// GainAmplitudeCoeffs<T> via dsp::set_flattened_item, and publishes a
// single Segment<GainAmplitudeCoeffs<T>> covering all channels'
// transitions.
//
// Why simpler than BiquadDesigner:
//   - No morph state machine — gain is a scalar, the per-channel
//     transition is a single 1-leg linear ramp from previous endpoint
//     to new target. The 1-leg / ~5.333 ms minimum fade is enforced by
//     the engine's leg granularity itself.
//   - No design math — coefficients ARE the amplitudes, no cos/sin/pow.
//   - No type discriminator — gain has only one shape.
//   - No sample-rate dependency — the constructor takes nothing.
//
// Taper curves (audio taper, log fader law, S-curve) and velocity
// clamping for fast user gestures remain upstream concerns. Tier 1
// trusts the per-leg, per-channel target as the curve-evaluated,
// velocity-clamped value to interpolate toward.

template <typename T = float>
class GainDesigner
{
  public:
    static constexpr size_t channel_count = statusbar::dsp::simd_flattened_size<T>::value;

    using coeffs_type = GainAmplitudeCoeffs<T>;
    using input_pipe_type = itc::LatestPipe<float>;
    using output_pipe_type = itc::LatestPipe<Segment<coeffs_type>>;

    GainDesigner() noexcept = default;

    GainDesigner(GainDesigner const&) = delete;
    auto operator=(GainDesigner const&) -> GainDesigner& = delete;

    // Connect a per-channel input pipe. Channels without a connected
    // pipe simply hold whatever set_initial established.
    void connect_input(size_t channel, input_pipe_type& pipe) noexcept { input_pipes_[channel] = &pipe; }

    // Convenience: connect ALL channels to one pipe (broadcast). Every
    // channel reads from the same source — useful when all lanes track
    // the same fader.
    void connect_input_broadcast(input_pipe_type& pipe) noexcept
    {
        for (size_t ch = 0; ch < channel_count; ++ch) {
            input_pipes_[ch] = &pipe;
        }
    }

    void connect_output(output_pipe_type& pipe) noexcept { output_pipe_ = &pipe; }

    // Initialize one channel's amplitude. Other channels stay
    // default-constructed (zero); callers should set_initial() each
    // channel they intend to use before calling step().
    void set_initial(size_t channel, float amplitude) noexcept
    {
        target_amplitude_[channel] = amplitude;
        statusbar::dsp::set_flattened_item(last_endpoint_.amplitude, amplitude, channel);
        any_initialized_ = true;
    }

    // Convenience: initialize ALL channels to the same amplitude.
    void set_initial(float amplitude) noexcept
    {
        for (size_t ch = 0; ch < channel_count; ++ch) {
            set_initial(ch, amplitude);
        }
    }

    // Advance one leg: pull latest per-channel targets, pack into the
    // SIMD endpoint, publish.
    void step(float recip_samples_per_leg) noexcept
    {
        if (!any_initialized_) {
            return;
        }

        coeffs_type new_endpoint = last_endpoint_;

        for (size_t ch = 0; ch < channel_count; ++ch) {
            if (input_pipes_[ch] == nullptr) {
                continue;
            }
            if (auto incoming = input_pipes_[ch]->try_consume()) {
                target_amplitude_[ch] = *incoming;
                statusbar::dsp::set_flattened_item(new_endpoint.amplitude, *incoming, ch);
            }
        }

        if (output_pipe_ != nullptr && output_pipe_->can_publish()) {
            output_pipe_->publish(make_segment(last_endpoint_, new_endpoint, recip_samples_per_leg));
        }
        last_endpoint_ = new_endpoint;
    }

    // Read-only inspection for tests / diagnostics.
    [[nodiscard]] auto target_amplitude(size_t channel) const noexcept -> float { return target_amplitude_[channel]; }
    [[nodiscard]] auto last_endpoint() const noexcept -> coeffs_type const& { return last_endpoint_; }

  private:
    std::array<float, channel_count> target_amplitude_{};
    coeffs_type last_endpoint_{};
    bool any_initialized_{false};

    std::array<input_pipe_type*, channel_count> input_pipes_{};
    output_pipe_type* output_pipe_{nullptr};
};

}  // namespace statusbar::engine
