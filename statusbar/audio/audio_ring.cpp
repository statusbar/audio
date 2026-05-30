// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Implementation of AudioRingBase
/// Separated from module to prevent atomic operations from being inlined across module boundaries

#include "statusbar/audio/audio_ring.hpp"

#include "statusbar/audio/audio.hpp"

#include <atomic>
#include <cstddef>

namespace statusbar::audio {

AudioRingBase::AudioRingBase(size_t channels, size_t capacity) noexcept
    : capacity_(capacity)
    , channels_(channels)
    , write_pos_(0)
    , read_pos_(0)
{}

void AudioRingBase::init_base(size_t channels, size_t capacity) noexcept
{
    channels_ = channels;
    capacity_ = capacity;
    write_pos_.store(0, std::memory_order_relaxed);
    read_pos_.store(0, std::memory_order_relaxed);
}

auto AudioRingBase::available_read() const noexcept -> size_t
{
    return write_pos_.load(std::memory_order_acquire) - read_pos_.load(std::memory_order_acquire);
}

auto AudioRingBase::available_write() const noexcept -> size_t
{
    return capacity_ - available_read();
}

void AudioRingBase::clear() noexcept
{
    write_pos_.store(0, std::memory_order_relaxed);
    read_pos_.store(0, std::memory_order_relaxed);
}

auto AudioRingBase::load_write_pos_relaxed() const noexcept -> size_t
{
    return write_pos_.load(std::memory_order_relaxed);
}

auto AudioRingBase::load_read_pos_relaxed() const noexcept -> size_t
{
    return read_pos_.load(std::memory_order_relaxed);
}

void AudioRingBase::store_write_pos_release(size_t value) noexcept
{
    write_pos_.store(value, std::memory_order_release);
}

void AudioRingBase::store_read_pos_release(size_t value) noexcept
{
    read_pos_.store(value, std::memory_order_release);
}

}  // namespace statusbar::audio