#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// The bridge between a stream's std::function-shaped audio callback and the
/// RT thread's (function pointer, context) slot: CallbackHolder owns the
/// callback and the scratch buffers its thunk builds the parameters in, and
/// publishes the thunk into the slot for as long as it lives.
///
/// Two holders may be alive on one slot for a moment, when a stream is
/// started again after a self-stop. Their destructors each clear the slot,
/// so the ORDER decides what the slot holds afterwards: the old holder goes
/// before the new one publishes (replace_callback_holder), or the new
/// callback is replaced by the null the old destructor publishes and the
/// stream runs silent while reporting success.

#include "statusbar/audio/audio_types.hpp"
#include "statusbar/itc/itc_rt_callback_slot.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace statusbar::audio {

/// RT-safe callback ABI: plain function pointer + user context.
using AudioCallbackFn =
    int (*)(void* user, float const* const* input, float* const* output, uint32_t frames, double stream_time) noexcept;

using AudioCallbackRT = statusbar::itc::RtCallbackSlot<AudioCallbackFn>;

/// Adapter to wrap the std::function-shaped callback for the RT thread.
/// Encapsulates the callback function and provides RT-safe access via thunk.
/// RAII: publishes into the slot on construction, clears it on destruction.
class CallbackHolder
{
  public:
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved) - moved in initializer list
    explicit CallbackHolder(
        AudioCallbackFloat&& callback, AudioCallbackRT& rt, uint32_t num_input_channels, uint32_t num_output_channels)
        : f_(std::move(callback))
        , rt_(&rt)
        , input_buffers_{}
        , output_buffers_{}
    {
        // Pre-allocate buffer vectors to avoid allocation in RT callback
        if (num_input_channels > 0) {
            input_buffers_.resize(num_input_channels);
        }
        if (num_output_channels > 0) {
            output_buffers_.resize(num_output_channels);
        }

        rt_->publish(&CallbackHolder::thunk, this);
    }

    ~CallbackHolder()
    {
        if (rt_ != nullptr) {
            rt_->clear();
        }
    }

    // Non-copyable, non-movable (prevent dangling rt_ pointer)
    CallbackHolder(CallbackHolder const&) = delete;
    auto operator=(CallbackHolder const&) -> CallbackHolder& = delete;
    CallbackHolder(CallbackHolder&&) = delete;
    auto operator=(CallbackHolder&&) -> CallbackHolder& = delete;

    /// The thunk the slot holds while this holder lives.
    static auto thunk(void* user, float const* const* input, float* const* output, uint32_t frames, double t) noexcept -> int
    {
        auto* self = static_cast<CallbackHolder*>(user);
#if __cpp_exceptions
        try {
#endif
            // Build AudioCallbackParams from raw pointers
            AudioCallbackParamsFloat params{};
            params.stream_time = t;

            // Setup input buffers if present
            if (input != nullptr && !self->input_buffers_.empty()) {
                for (size_t i = 0; i < self->input_buffers_.size(); ++i) {
                    self->input_buffers_[i].sample = std::span<float const>(input[i], frames);
                }
                params.input_buffers = self->input_buffers_;
            }

            // Setup output buffers if present
            if (output != nullptr && !self->output_buffers_.empty()) {
                for (size_t i = 0; i < self->output_buffers_.size(); ++i) {
                    self->output_buffers_[i].sample = std::span<float>(output[i], frames);
                }
                params.output_buffers = self->output_buffers_;
            }

            // Call user callback and convert Status to int
            auto result = self->f_(params);
            return result.has_value() ? 0 : -1;
#if __cpp_exceptions
        } catch (...) {
            return -1;  // Signal error on exception
        }
#endif
    }

  private:
    AudioCallbackFloat f_;  // inplace_function with V2 signature
    AudioCallbackRT* rt_;   // Pointer to RT callback struct (cleared on destruction)

    // Scratch storage for building AudioCallbackParams (pre-allocated at stream start)
    std::vector<InputAudioBufferFloat> input_buffers_;
    std::vector<OutputAudioBufferFloat> output_buffers_;
};

/// Put a holder for @p callback in @p holder, publishing into @p rt. The
/// holder there before goes first, so its destructor's clear of the shared
/// slot lands before the new publish and not after it: assigning the new
/// holder over the old one runs the two the other way round and leaves the
/// slot null.
inline void replace_callback_holder(
    std::unique_ptr<CallbackHolder>& holder,
    AudioCallbackFloat&& callback,
    AudioCallbackRT& rt,
    uint32_t num_input_channels,
    uint32_t num_output_channels)
{
    holder.reset();
    holder = std::make_unique<CallbackHolder>(std::move(callback), rt, num_input_channels, num_output_channels);
}

}  // namespace statusbar::audio
