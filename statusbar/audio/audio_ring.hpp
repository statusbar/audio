#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Lock-free SPSC (single producer, single consumer) audio ring buffer
/// for bridging audio between different devices or threads.

#include "statusbar/audio/audio_types.hpp"

#include <atomic>
#include <cstddef>
#include <memory_resource>
#include <span>
#include <vector>

namespace statusbar::audio {

/// Non-template base class for AudioRing
/// Holds atomic state and provides thread-safe position accessors
/// Implementation in audio_ring_impl.cpp to avoid module inlining issues
class AudioRingBase
{
  public:
    AudioRingBase() = default;
    /// @param channels Number of audio channels
    /// @param capacity Number of frames capacity per channel
    AudioRingBase(size_t channels, size_t capacity) noexcept;

    ~AudioRingBase() = default;

    // Non-copyable, non-movable due to atomic members
    AudioRingBase(AudioRingBase const&) = delete;
    auto operator=(AudioRingBase const&) -> AudioRingBase& = delete;
    AudioRingBase(AudioRingBase&&) = delete;
    auto operator=(AudioRingBase&&) -> AudioRingBase& = delete;

    /// Initialize or reinitialize the base state
    /// @param channels Number of audio channels
    /// @param capacity Number of frames capacity per channel
    void init_base(size_t channels, size_t capacity) noexcept;

    // Configuration accessors
    [[nodiscard]] auto capacity() const noexcept { return capacity_; }
    [[nodiscard]] auto channels() const noexcept { return channels_; }

    // Atomic position accessors - implementations in .cpp to prevent inlining
    [[nodiscard]] auto available_read() const noexcept -> size_t;
    [[nodiscard]] auto available_write() const noexcept -> size_t;

    /// Clear the ring buffer (reset read/write positions)
    void clear() noexcept;

  protected:
    // Protected atomic accessors for derived class use
    [[nodiscard]] auto load_write_pos_relaxed() const noexcept -> size_t;
    [[nodiscard]] auto load_read_pos_relaxed() const noexcept -> size_t;
    /// @param value Write position to store with release ordering
    void store_write_pos_release(size_t value) noexcept;
    /// @param value Read position to store with release ordering
    void store_read_pos_release(size_t value) noexcept;

    size_t capacity_{0};
    size_t channels_{0};

  private:
    std::atomic<size_t> write_pos_{0};
    std::atomic<size_t> read_pos_{0};
};

/// Lock-free single-producer single-consumer ring buffer for non-interleaved audio
///
/// Designed for bridging audio between separate input and output devices
/// running on different clocks or threads. Provides zero-copy read/write
/// operations with atomic coordination.
///
/// Thread Safety:
/// - push() must be called from a single producer thread only
/// - pop() must be called from a single consumer thread only
/// - available_read() and available_write() are safe from either thread
template <typename T = float>
class AudioRing : public AudioRingBase
{
  public:
    /// Construct a ring buffer.
    /// @param channels Number of audio channels (non-interleaved)
    /// @param frames Number of frames capacity per channel
    /// @param memory_resource Memory resource for the per-channel sample
    ///        buffers. nullptr is treated as std::pmr::get_default_resource().
    AudioRing(size_t channels, size_t frames, std::pmr::memory_resource* memory_resource = nullptr)
        : AudioRingBase(channels, frames)
        , mem_resource_{memory_resource != nullptr ? memory_resource : std::pmr::get_default_resource()}
        , channel_data_{mem_resource_}
    {
        channel_data_.resize(channels, std::pmr::vector<T>{mem_resource_});
        for (auto& ch : channel_data_) {
            ch.resize(frames, T{});
        }
    }

    /// Default constructor (must call init() before use). Uses
    /// std::pmr::get_default_resource() for the per-channel buffers.
    AudioRing()
        : mem_resource_{std::pmr::get_default_resource()}
        , channel_data_{mem_resource_}
    {}

    /// Initialize or reinitialize the ring buffer
    /// @param channels Number of audio channels
    /// @param frames Number of frames capacity per channel
    void init(size_t channels, size_t frames)
    {
        init_base(channels, frames);
        channel_data_.resize(channels, std::pmr::vector<T>{mem_resource_});
        for (auto& ch : channel_data_) {
            ch.assign(frames, T{});
        }
    }

    /// Get the memory resource used by the per-channel sample buffers.
    [[nodiscard]] auto memory_resource() const noexcept -> std::pmr::memory_resource* { return mem_resource_; }

    /// Check if all frames from source buffers can be pushed
    /// @param src Span of input channel buffers (non-interleaved)
    /// @return true if all frames can be pushed, false otherwise
    [[nodiscard]] auto can_push(std::span<InputAudioBuffer<T> const> const src) const noexcept -> bool
    {
        if (src.empty() || src[0].sample.empty()) {
            return true;  // Empty push always succeeds
        }
        return src[0].sample.size() <= available_write();
    }

    /// Check if all frames requested can be popped into destination buffers
    /// @param dst Span of output channel buffers (non-interleaved)
    /// @return true if all frames can be popped, false otherwise
    [[nodiscard]] auto can_pop(std::span<OutputAudioBuffer<T> const> const dst) const noexcept -> bool
    {
        if (dst.empty() || dst[0].sample.empty()) {
            return true;  // Empty pop always succeeds
        }
        return dst[0].sample.size() <= available_read();
    }

    /// Check if all frames requested can be peeked into destination buffers
    /// @param dst Span of output channel buffers (non-interleaved)
    /// @return true if all frames can be peeked, false otherwise
    [[nodiscard]] auto can_peek(std::span<OutputAudioBuffer<T> const> const dst) const noexcept -> bool
    {
        if (dst.empty() || dst[0].sample.empty()) {
            return true;  // Empty peek always succeeds
        }
        return dst[0].sample.size() <= available_read();
    }

    /// Check if the requested number of frames can be skipped
    /// @param frames Number of frames to skip
    /// @return true if all frames can be skipped, false otherwise
    [[nodiscard]] auto can_skip(size_t const frames) const noexcept -> bool { return frames <= available_read(); }

    /// Push audio frames into the ring buffer from input buffers
    /// @param src Span of input channel buffers (non-interleaved). If src has
    ///   fewer channels than the ring, the missing channels are filled with
    ///   silence — the alternative (leaving prior contents intact while
    ///   advancing write_pos) would surface stale data on the consumer side.
    /// @return Number of frames actually pushed (may be less if buffer full)
    auto push(std::span<InputAudioBuffer<T> const> const src) noexcept -> size_t
    {
        if (src.empty() || src[0].sample.empty()) {
            return 0;
        }

        size_t const frames = src[0].sample.size();
        size_t const avail = available_write();
        size_t const to_write = (frames < avail) ? frames : avail;

        if (to_write == 0) {
            return 0;
        }

        size_t const w = load_write_pos_relaxed();
        size_t const num_channels = (src.size() < channels_) ? src.size() : channels_;
        for (size_t c = 0; c < num_channels; ++c) {
            auto const& input = src[c].sample;
            size_t const count = (to_write < input.size()) ? to_write : input.size();
            for (size_t i = 0; i < count; ++i) {
                channel_data_[c][(w + i) % capacity_] = input[i];
            }
        }
        // Fill any ring channels the caller didn't provide with silence —
        // the write position advances by to_write regardless, so unfilled
        // channels would otherwise expose whatever stale samples were left
        // there from a prior wrap.
        for (size_t c = num_channels; c < channels_; ++c) {
            for (size_t i = 0; i < to_write; ++i) {
                channel_data_[c][(w + i) % capacity_] = T{};
            }
        }
        store_write_pos_release(w + to_write);
        return to_write;
    }

    /// Pop audio frames from the ring buffer into output buffers
    /// @param dst Span of output channel buffers (non-interleaved) to write to
    /// @return Number of frames actually popped (remaining filled with silence)
    auto pop(std::span<OutputAudioBuffer<T> const> const dst) noexcept -> size_t
    {
        if (dst.empty() || dst[0].sample.empty()) {
            return 0;
        }

        size_t const frames = dst[0].sample.size();
        size_t const avail = available_read();
        size_t const to_read = (frames < avail) ? frames : avail;

        size_t const r = load_read_pos_relaxed();
        size_t const num_channels = (dst.size() < channels_) ? dst.size() : channels_;
        for (size_t c = 0; c < num_channels; ++c) {
            auto output = dst[c].sample;
            // Copy available data
            for (size_t i = 0; i < to_read; ++i) {
                output[i] = channel_data_[c][(r + i) % capacity_];
            }
            // Fill remainder with silence
            for (size_t i = to_read; i < output.size(); ++i) {
                output[i] = T{};
            }
        }
        store_read_pos_release(r + to_read);
        return to_read;
    }

    /// Peek at audio frames without consuming them
    /// @param dst Span of output channel buffers to write to
    /// @return Number of frames actually read
    auto peek(std::span<OutputAudioBuffer<T> const> const dst) const noexcept -> size_t
    {
        if (dst.empty() || dst[0].sample.empty()) {
            return 0;
        }

        size_t frames = dst[0].sample.size();
        size_t avail = available_read();
        size_t const to_read = (frames < avail) ? frames : avail;

        size_t r = load_read_pos_relaxed();
        size_t num_channels = (dst.size() < channels_) ? dst.size() : channels_;
        for (size_t c = 0; c < num_channels; ++c) {
            auto output = dst[c].sample;
            for (size_t i = 0; i < to_read; ++i) {
                output[i] = channel_data_[c][(r + i) % capacity_];
            }
            for (size_t i = to_read; i < output.size(); ++i) {
                output[i] = T{};
            }
        }
        return to_read;
    }

    /// Skip frames in the ring buffer (advance read position without copying)
    /// @param frames Number of frames to skip
    /// @return Number of frames actually skipped
    auto skip(size_t frames) noexcept -> size_t
    {
        size_t avail = available_read();
        size_t const to_skip = (frames < avail) ? frames : avail;
        size_t r = load_read_pos_relaxed();
        store_read_pos_release(r + to_skip);
        return to_skip;
    }

  private:
    std::pmr::memory_resource* mem_resource_{std::pmr::get_default_resource()};
    std::pmr::vector<std::pmr::vector<T>> channel_data_;
};

}  // namespace statusbar::audio
