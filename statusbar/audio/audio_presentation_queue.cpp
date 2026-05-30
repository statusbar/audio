// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Implementation of AudioPresentationQueueBase
/// Separated from module to prevent atomic operations from being inlined across module boundaries

#include "statusbar/audio/audio_presentation_queue.hpp"

#include "statusbar/audio/audio.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace statusbar::audio {

AudioPresentationQueueBase::AudioPresentationQueueBase(size_t channels, size_t capacity) noexcept
    : capacity_(capacity)
    , channels_(channels)
    , center_time_(0)
{}

void AudioPresentationQueueBase::init_base(size_t channels, size_t capacity) noexcept
{
    channels_ = channels;
    capacity_ = capacity;
    center_time_.store(0, std::memory_order_relaxed);
}

auto AudioPresentationQueueBase::center_time() const noexcept -> int64_t
{
    return center_time_.load(std::memory_order_acquire);
}

auto AudioPresentationQueueBase::earliest_valid_time() const noexcept -> int64_t
{
    return center_time_.load(std::memory_order_acquire);
}

auto AudioPresentationQueueBase::latest_valid_time() const noexcept -> int64_t
{
    return center_time_.load(std::memory_order_acquire) + static_cast<int64_t>(capacity_) - 1;
}

auto AudioPresentationQueueBase::can_write(int64_t start_time, size_t frame_count) const noexcept -> bool
{
    if (frame_count == 0) {
        return true;
    }
    int64_t const center = center_time_.load(std::memory_order_acquire);
    int64_t const end_time = start_time + static_cast<int64_t>(frame_count) - 1;
    return start_time >= center && end_time < center + static_cast<int64_t>(capacity_);
}

auto AudioPresentationQueueBase::load_center_time_acquire() const noexcept -> int64_t
{
    return center_time_.load(std::memory_order_acquire);
}

auto AudioPresentationQueueBase::load_center_time_relaxed() const noexcept -> int64_t
{
    return center_time_.load(std::memory_order_relaxed);
}

void AudioPresentationQueueBase::store_center_time_relaxed(int64_t value) noexcept
{
    center_time_.store(value, std::memory_order_relaxed);
}

void AudioPresentationQueueBase::store_center_time_release(int64_t value) noexcept
{
    center_time_.store(value, std::memory_order_release);
}

auto AudioPresentationQueueBase::time_to_index(int64_t time) const noexcept -> size_t
{
    // Handle negative times and large positive times correctly
    // Use modulo that always returns positive result
    int64_t const cap = static_cast<int64_t>(capacity_);
    int64_t mod = time % cap;
    if (mod < 0) {
        mod += cap;
    }
    return static_cast<size_t>(mod);
}

auto AudioPresentationQueueBase::is_time_in_range(int64_t time) const noexcept -> bool
{
    int64_t const center = center_time_.load(std::memory_order_acquire);
    return time >= center && time < center + static_cast<int64_t>(capacity_);
}

void AudioPresentationQueueBase::init_slot_states(size_t capacity)
{
    // make_unique<T[]>(n) value-initialises each element (C++20). For
    // std::atomic<uint8_t> that means zero, which is SlotState::Empty.
    slot_states_ = std::make_unique<std::atomic<uint8_t>[]>(capacity);
}

void AudioPresentationQueueBase::reset_all_slots() noexcept
{
    if (slot_states_ == nullptr) {
        return;
    }
    for (size_t i = 0; i < capacity_; ++i) {
        slot_states_[i].store(static_cast<uint8_t>(SlotState::Empty), std::memory_order_relaxed);
    }
}

auto AudioPresentationQueueBase::try_acquire_for_write(size_t idx) noexcept -> bool
{
    uint8_t expected = static_cast<uint8_t>(SlotState::Empty);
    return slot_states_[idx].compare_exchange_strong(
        expected, static_cast<uint8_t>(SlotState::Busy), std::memory_order_acquire, std::memory_order_relaxed);
}

auto AudioPresentationQueueBase::try_acquire_for_read(size_t idx) noexcept -> ReadAcquireOutcome
{
    uint8_t expected = static_cast<uint8_t>(SlotState::Full);
    if (slot_states_[idx].compare_exchange_strong(
            expected, static_cast<uint8_t>(SlotState::Busy), std::memory_order_acquire, std::memory_order_relaxed)) {
        return ReadAcquireOutcome::Acquired;
    }
    if (expected == static_cast<uint8_t>(SlotState::Busy)) {
        return ReadAcquireOutcome::WasBusy;
    }
    return ReadAcquireOutcome::WasEmpty;
}

void AudioPresentationQueueBase::release_after_write(size_t idx) noexcept
{
    slot_states_[idx].store(static_cast<uint8_t>(SlotState::Full), std::memory_order_release);
}

void AudioPresentationQueueBase::release_after_read(size_t idx) noexcept
{
    slot_states_[idx].store(static_cast<uint8_t>(SlotState::Empty), std::memory_order_release);
}

void AudioPresentationQueueBase::release_after_peek(size_t idx) noexcept
{
    slot_states_[idx].store(static_cast<uint8_t>(SlotState::Full), std::memory_order_release);
}

auto AudioPresentationQueueBase::load_slot_state_relaxed(size_t idx) const noexcept -> SlotState
{
    return static_cast<SlotState>(slot_states_[idx].load(std::memory_order_relaxed));
}

}  // namespace statusbar::audio