#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Presentation-time based audio queue for time-indexed random access
/// Designed for AVB/TSN streaming where packets arrive out of order with presentation timestamps

#include "statusbar/audio/audio_types.hpp"
#include "statusbar/dsp/dsp.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <vector>

namespace statusbar::audio {

using statusbar::dsp::zero;  // Bring zero() into scope for ADL with scalars and SIMD types

/// Non-template base class for AudioPresentationQueue
/// Holds atomic state and provides thread-safe time accessors
/// Implementation in audio_presentation_queue.cpp to avoid module inlining issues
class AudioPresentationQueueBase
{
  public:
    AudioPresentationQueueBase() = default;
    /// @param channels Number of audio channels
    /// @param capacity Number of frames capacity
    AudioPresentationQueueBase(size_t channels, size_t capacity) noexcept;

    ~AudioPresentationQueueBase() = default;

    // Non-copyable, non-movable due to atomic member
    AudioPresentationQueueBase(AudioPresentationQueueBase const&) = delete;
    auto operator=(AudioPresentationQueueBase const&) -> AudioPresentationQueueBase& = delete;
    AudioPresentationQueueBase(AudioPresentationQueueBase&&) = delete;
    auto operator=(AudioPresentationQueueBase&&) -> AudioPresentationQueueBase& = delete;

    /// Initialize or reinitialize the base state
    /// @param channels Number of audio channels
    /// @param capacity Number of frames capacity
    void init_base(size_t channels, size_t capacity) noexcept;

    // Configuration accessors
    [[nodiscard]] auto capacity() const noexcept { return capacity_; }
    [[nodiscard]] auto channels() const noexcept { return channels_; }

    // Atomic time accessors - implementations in .cpp to prevent inlining across module boundary
    [[nodiscard]] auto center_time() const noexcept -> int64_t;
    [[nodiscard]] auto earliest_valid_time() const noexcept -> int64_t;
    [[nodiscard]] auto latest_valid_time() const noexcept -> int64_t;

    /// Check if a time range is writable (within future window)
    /// @param start_time Presentation time of first frame
    /// @param frame_count Number of frames to check
    [[nodiscard]] auto can_write(int64_t start_time, size_t frame_count) const noexcept -> bool;

    /// Check if requested frames can be read (always true if within capacity)
    /// @param frame_count Number of frames to check
    [[nodiscard]] auto can_read(size_t frame_count) const noexcept { return frame_count <= capacity_; }

  protected:
    /// Per-frame ownership state for SPSC coordination. One byte per slot.
    /// Transitions are CAS-protected; only the holder of Busy may transition
    /// it back to Full (after a write) or Empty (after a read drain).
    enum class SlotState : uint8_t
    {
        Empty = 0,  // Initial / consumer drained / never written
        Busy = 1,   // Producer or consumer is mid-access
        Full = 2,   // Producer wrote; consumer hasn't drained yet
    };

    /// Outcome of try_acquire_for_read().
    enum class ReadAcquireOutcome : uint8_t
    {
        Acquired,  // Was Full; CAS to Busy succeeded; caller owns the slot
        WasEmpty,  // Slot held no data; caller does not own it
        WasBusy,   // Producer is concurrently writing; caller does not own it
    };

    // Protected atomic accessors for derived class use
    [[nodiscard]] auto load_center_time_acquire() const noexcept -> int64_t;
    [[nodiscard]] auto load_center_time_relaxed() const noexcept -> int64_t;
    /// @param value Center time value to store with relaxed ordering
    void store_center_time_relaxed(int64_t value) noexcept;
    /// @param value Center time value to store with release ordering
    void store_center_time_release(int64_t value) noexcept;

    /// Convert presentation time to buffer index
    /// @param time Presentation time to convert
    [[nodiscard]] auto time_to_index(int64_t time) const noexcept -> size_t;

    /// Check if time is in valid range (uses atomic load)
    /// @param time Presentation time to check
    [[nodiscard]] auto is_time_in_range(int64_t time) const noexcept -> bool;

    /// Producer: CAS Empty -> Busy, or Full -> Busy when the slot holds a
    /// sample for an EARLIER time than @p time (a stale write the consumer
    /// has not drained). Returns true if the caller now owns the slot for
    /// writing. A Full slot for the same or a later time is never taken.
    [[nodiscard]] auto try_acquire_for_write(size_t idx, int64_t time) noexcept -> bool;
    /// CAS Full -> Busy. Returns Acquired / WasEmpty / WasBusy.
    [[nodiscard]] auto try_acquire_for_read(size_t idx) noexcept -> ReadAcquireOutcome;
    /// Release after write: Busy -> Full (release).
    void release_after_write(size_t idx) noexcept;
    /// Release after read drain: Busy -> Empty (release).
    void release_after_read(size_t idx) noexcept;
    /// Release after peek (non-consuming): Busy -> Full (release).
    void release_after_peek(size_t idx) noexcept;
    /// Allocate (or reallocate) the slot-state array, all Empty.
    void init_slot_states(size_t capacity);
    /// Reset every slot to Empty without reallocating. Caller must guarantee
    /// no concurrent producer/consumer access.
    void reset_all_slots() noexcept;
    /// Relaxed load of a single slot's state. For peek/read_sample diagnostics.
    [[nodiscard]] auto load_slot_state_relaxed(size_t idx) const noexcept -> SlotState;

    /// The presentation time of the sample a slot holds (never_written_time
    /// when none). Written by the Busy owner before release_after_write and
    /// read by the owner after a successful acquire; the slot state's
    /// release/acquire carries it across threads.
    [[nodiscard]] auto slot_time(size_t idx) const noexcept -> int64_t;
    /// Record the presentation time of the sample being written to a slot.
    void set_slot_time(size_t idx, int64_t time) noexcept;

    /// slot_time() of a slot nothing has been written to.
    static constexpr int64_t never_written_time = std::numeric_limits<int64_t>::min();

    size_t capacity_{0};
    size_t channels_{0};

  private:
    std::atomic<int64_t> center_time_{0};
    std::unique_ptr<std::atomic<uint8_t>[]> slot_states_{};
    std::unique_ptr<std::atomic<int64_t>[]> slot_times_{};
};

/// Lock-free SPSC audio queue with presentation-time indexed random access.
///
/// Unlike AudioRing (FIFO), this queue stores samples at specific presentation
/// times. Samples can arrive out of order but are always read in time order.
///
/// Concurrency contract:
/// - write()/write_sample() must be called from a single producer thread.
/// - read()/peek()/advance() must be called from a single consumer thread.
/// - center_time() and time-range queries are safe from either thread.
///
/// Each slot carries a single-byte SlotState atomic (Empty/Busy/Full) and the
/// presentation time of the sample it holds. Producer and consumer both CAS
/// Empty<->Busy / Full<->Busy before touching slot data, and skip on CAS
/// failure. Read advances center_time *before* draining slots so a
/// steady-state producer (with fresher acquire load of center) writes only
/// outside the read window. The CAS flag protects against an in-flight
/// producer that already loaded the old center, eliminating torn reads/writes
/// for any T including SIMD vector types.
///
/// A producer that loaded center before the consumer advanced it may still
/// land a sample for a time the consumer has passed: into a slot the consumer
/// has already drained, or into one it skipped as Busy. Such a sample sits in
/// the slot the time one window later maps to. The slot time catches it: the
/// producer re-checks center after taking the slot and gives it back if its
/// time has fallen behind; the consumer treats a slot whose time is not the
/// one it is reading as never written (ReadResult::frames_stale); and a
/// producer writing the later time takes a Full slot over when the time it
/// holds is earlier than its own.
///
/// On contention, drops are by-design — both sides skip the slot. This matches
/// real-time audio semantics where a producer running behind its presentation
/// schedule should drop, not stall.
template <typename T = float>
class AudioPresentationQueue : public AudioPresentationQueueBase
{
  public:
    /// Outcome of read()/peek(): how many of the requested frames were drained,
    /// how many carried real producer-written data, how many were dropped
    /// because the producer was concurrently writing the slot, and how many
    /// held a sample for an earlier time (a producer with a stale view of
    /// center) and so read as silence.
    /// frames_read - frames_with_data - frames_dropped_racing - frames_stale
    /// == frames that were never written.
    struct ReadResult
    {
        size_t frames_read{0};
        size_t frames_with_data{0};
        size_t frames_dropped_racing{0};
        size_t frames_stale{0};
    };

    /// Construct a presentation queue.
    /// @param channels Number of audio channels (non-interleaved)
    /// @param capacity Number of frames capacity (how far into future samples can be stored)
    /// @param memory_resource Memory resource for the per-channel sample
    ///        buffers. nullptr is treated as std::pmr::get_default_resource().
    AudioPresentationQueue(size_t channels, size_t capacity, std::pmr::memory_resource* memory_resource = nullptr)
        : AudioPresentationQueueBase(channels, capacity)
        , channel_data_{memory_resource != nullptr ? memory_resource : std::pmr::get_default_resource()}
        , mem_resource_{channel_data_.get_allocator().resource()}
    {
        channel_data_.resize(channels, std::pmr::vector<T>{mem_resource_});
        for (auto& ch : channel_data_) {
            ch.resize(capacity, T{});
        }
        init_slot_states(capacity);
    }

    /// Default constructor (must call init() before use). Uses
    /// std::pmr::get_default_resource() for the per-channel buffers.
    AudioPresentationQueue()
        : channel_data_{std::pmr::get_default_resource()}
        , mem_resource_{channel_data_.get_allocator().resource()}
    {}

    /// Initialize or reinitialize the queue
    /// @param channels Number of audio channels
    /// @param capacity Number of frames capacity
    void init(size_t channels, size_t capacity)
    {
        init_base(channels, capacity);
        channel_data_.resize(channels, std::pmr::vector<T>{mem_resource_});
        for (auto& ch : channel_data_) {
            ch.assign(capacity, T{});
        }
        init_slot_states(capacity);
    }

    /// Get the memory resource used by the per-channel sample buffers.
    [[nodiscard]] auto memory_resource() const noexcept -> std::pmr::memory_resource* { return mem_resource_; }

    // Write operations (producer side)

    /// Write samples at a specific presentation time
    /// @param start_time Presentation time for first sample
    /// @param src Span of input channel buffers
    /// @return Number of frames successfully written (excludes out-of-range
    ///         and frames lost to a concurrent consumer holding the slot).
    auto write(int64_t const start_time, std::span<InputAudioBuffer<T> const> const src) noexcept -> size_t
    {
        int64_t const center = load_center_time_acquire();
        size_t const frames = src.empty() ? 0 : src[0].sample.size();
        size_t const num_channels = (src.size() < channels_) ? src.size() : channels_;
        size_t written = 0;

        for (size_t i = 0; i < frames; ++i) {
            int64_t const t = start_time + static_cast<int64_t>(i);
            auto const idx = acquire_slot_for_write(t, center);
            if (!idx.has_value()) {
                continue;
            }
            for (size_t c = 0; c < num_channels; ++c) {
                if (i < src[c].sample.size()) {
                    channel_data_[c][*idx] = src[c].sample[i];
                }
            }
            commit_write(*idx, t);
            ++written;
        }
        return written;
    }

    /// Write a single sample at a specific time and channel. With per-frame
    /// state, this takes whole-frame ownership; other channels at this slot
    /// retain whatever they held (typically zero, since the consumer's drain
    /// resets the entire frame).
    /// @param time Presentation time for the sample
    /// @param channel Channel index to write to
    /// @param value Sample value to write
    /// @return true if written, false if out of range or slot contended.
    auto write_sample(int64_t const time, size_t const channel, T const value) noexcept -> bool
    {
        if (channel >= channels_) {
            return false;
        }

        auto const idx = acquire_slot_for_write(time, load_center_time_acquire());
        if (!idx.has_value()) {
            return false;
        }
        channel_data_[channel][*idx] = value;
        commit_write(*idx, time);
        return true;
    }

    // Read operations (consumer side)

    /// Read samples starting at center_time and advance.
    /// Frames where the producer never wrote, or where the consumer lost a CAS
    /// race with the producer, are filled with T{} in dst. The ReadResult
    /// breaks down the outcome of each frame in the request.
    /// @param dst Span of output channel buffers
    auto read(std::span<OutputAudioBuffer<T> const> const dst) noexcept -> ReadResult
    {
        int64_t const center = load_center_time_relaxed();
        size_t const frames = dst.empty() ? 0 : dst[0].sample.size();

        // Advance center_time *before* draining. Producers that load center
        // after this store see the new value and reject writes to the range
        // we're about to drain. The slot CAS and the slot time handle
        // producers in-flight with the stale center.
        store_center_time_release(center + static_cast<int64_t>(frames));

        ReadResult result{.frames_read = frames};
        for (size_t i = 0; i < frames; ++i) {
            take_frame(center + static_cast<int64_t>(i), i, dst, result, true);
        }
        return result;
    }

    /// Peek at samples without advancing center_time. Briefly takes Busy
    /// ownership of each Full slot, copies channels to dst, then releases
    /// the slot back to Full (does not drain). Frames that weren't Full are
    /// filled with T{} and counted in ReadResult.frames_dropped_racing or as
    /// "never written" via subtraction.
    /// @param dst Span of output channel buffers
    auto peek(std::span<OutputAudioBuffer<T> const> const dst) noexcept -> ReadResult
    {
        int64_t const center = load_center_time_acquire();
        size_t const frames = dst.empty() ? 0 : dst[0].sample.size();
        ReadResult result{.frames_read = frames};
        for (size_t i = 0; i < frames; ++i) {
            take_frame(center + static_cast<int64_t>(i), i, dst, result, false);
        }
        return result;
    }

    /// Read a single sample at a specific time and channel. Best-effort,
    /// non-mutating: returns the slot's value if the producer has written it
    /// (Full state observed at call time), T{} otherwise. May return T{} even
    /// if a write is in-flight at the same time.
    /// @param time Presentation time of the sample to read
    /// @param channel Channel index to read from
    /// @return Sample value (zero if not written, out of range, or contended)
    [[nodiscard]] auto read_sample(int64_t const time, size_t const channel) noexcept -> T
    {
        if (channel >= channels_ || !is_time_in_range(time)) {
            return T{};
        }
        size_t const idx = time_to_index(time);
        if (try_acquire_for_read(idx) != ReadAcquireOutcome::Acquired) {
            return T{};
        }
        return take_held_sample(idx, time, channel);
    }

    // Time advancement

    /// Advance center_time by specified frames (zeros skipped samples).
    /// Like read(), this advances center_time first then drains; in-flight
    /// producer writes that lose the slot CAS are dropped.
    /// @param frames Number of frames to advance
    void advance(size_t frames) noexcept
    {
        int64_t center = load_center_time_relaxed();
        store_center_time_release(center + static_cast<int64_t>(frames));

        for (size_t i = 0; i < frames; ++i) {
            size_t idx = time_to_index(center + static_cast<int64_t>(i));
            if (try_acquire_for_read(idx) == ReadAcquireOutcome::Acquired) {
                for (size_t c = 0; c < channels_; ++c) {
                    zero_sample(channel_data_[c][idx]);
                }
                release_after_read(idx);
            }
            // WasEmpty: already clean. WasBusy: leave for next pass.
        }
    }

    /// Set center_time to a specific value (zeros entire buffer for safety).
    /// Not safe under concurrent producer/consumer access.
    /// @param time New center time value
    void set_center_time(int64_t time) noexcept
    {
        for (size_t c = 0; c < channels_; ++c) {
            for (size_t i = 0; i < capacity_; ++i) {
                zero_sample(channel_data_[c][i]);
            }
        }
        reset_all_slots();
        store_center_time_release(time);
    }

    // Utility

    /// Clear all data (zeros buffer, keeps center_time). Not safe under
    /// concurrent producer/consumer access.
    void clear() noexcept
    {
        for (size_t c = 0; c < channels_; ++c) {
            for (size_t i = 0; i < capacity_; ++i) {
                zero_sample(channel_data_[c][i]);
            }
        }
        reset_all_slots();
    }

    /// Reset completely (center_time = 0, zeros buffer). Not safe under
    /// concurrent producer/consumer access.
    void reset() noexcept
    {
        clear();
        store_center_time_release(0);
    }

  protected:
    /// Producer: own the slot for @p time, or nullopt. Out of range against
    /// @p center (the producer's view) is a drop; so is a slot the consumer
    /// holds or that holds a sample for the same or a later time. Once the
    /// slot is held, center is read again: if @p time has fallen behind it
    /// (the consumer advanced while this write was in flight), the slot is
    /// given back clean, so the sample never lands in the slot the time one
    /// window later maps to.
    /// @param time Presentation time of the frame
    /// @param center The producer's view of center_time
    [[nodiscard]] auto acquire_slot_for_write(int64_t time, int64_t center) noexcept -> std::optional<size_t>
    {
        if (time < center || time >= center + static_cast<int64_t>(capacity_)) {
            return std::nullopt;
        }
        size_t const idx = time_to_index(time);
        if (!try_acquire_for_write(idx, time)) {
            return std::nullopt;
        }
        if (time < load_center_time_acquire()) {
            drain_stale_slot(idx);
            return std::nullopt;
        }
        return idx;
    }

    /// Producer: publish the frame written to @p idx as the sample for @p time.
    void commit_write(size_t idx, int64_t time) noexcept
    {
        set_slot_time(idx, time);
        release_after_write(idx);
    }

    /// Per-channel sample storage; protected so a test can plant a slot.
    std::pmr::vector<std::pmr::vector<T>> channel_data_;

  private:
    /// Zero a sample value (uses zero(T&) from statusbar::dsp for scalars and SIMD types)
    static void zero_sample(T& v) noexcept { zero(v); }

    /// Zero every channel of a slot the caller holds Busy and release it Empty.
    void drain_stale_slot(size_t idx) noexcept
    {
        for (size_t c = 0; c < channels_; ++c) {
            zero_sample(channel_data_[c][idx]);
        }
        release_after_read(idx);
    }

    /// Value of a held (Busy) slot for @p time on @p channel, handing the slot
    /// back Full; or T{} after draining it when it holds a sample for another
    /// time (a producer with a stale view of center wrote it).
    [[nodiscard]] auto take_held_sample(size_t idx, int64_t time, size_t channel) noexcept -> T
    {
        if (slot_time(idx) != time) {
            drain_stale_slot(idx);
            return T{};
        }
        T const value = channel_data_[channel][idx];
        release_after_peek(idx);
        return value;
    }

    /// Consumer: one frame of read() (@p drain) or peek(). A slot that holds a
    /// sample for a time other than @p time was written by a producer with a
    /// stale view of center: it reads as silence and is drained either way.
    void take_frame(
        int64_t time, size_t i, std::span<OutputAudioBuffer<T> const> const dst, ReadResult& result, bool drain) noexcept
    {
        size_t const num_channels = (dst.size() < channels_) ? dst.size() : channels_;
        size_t const idx = time_to_index(time);
        auto const outcome = try_acquire_for_read(idx);
        if (outcome == ReadAcquireOutcome::Acquired && slot_time(idx) == time) {
            copy_frame_out(idx, i, dst, num_channels, drain);
            ++result.frames_with_data;
            return;
        }
        for (size_t c = 0; c < num_channels; ++c) {
            zero_sample(dst[c].sample[i]);
        }
        if (outcome == ReadAcquireOutcome::Acquired) {
            drain_stale_slot(idx);
            ++result.frames_stale;
        } else if (outcome == ReadAcquireOutcome::WasBusy) {
            ++result.frames_dropped_racing;
        }
    }

    /// Copy a held slot's channels to frame @p i of @p dst, then drain it
    /// (zero, Empty) or hand it back Full.
    void copy_frame_out(
        size_t idx, size_t i, std::span<OutputAudioBuffer<T> const> const dst, size_t num_channels, bool drain) noexcept
    {
        for (size_t c = 0; c < num_channels; ++c) {
            dst[c].sample[i] = channel_data_[c][idx];
        }
        if (!drain) {
            release_after_peek(idx);
            return;
        }
        for (size_t c = 0; c < channels_; ++c) {
            zero_sample(channel_data_[c][idx]);
        }
        release_after_read(idx);
    }

    std::pmr::memory_resource* mem_resource_{std::pmr::get_default_resource()};
};

}  // namespace statusbar::audio
