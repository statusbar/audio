#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Which thread the audio callback runs on.
///
/// A stream's stop() may be called from the render thread itself, inside the
/// callback (a callback that returns failure, or user code that stops the
/// stream it is being driven by). Tearing the device down from there deadlocks
/// (AudioOutputUnitStop waits for the render thread, which is the caller), so
/// that case must defer the teardown. "A callback is in flight" is not that
/// case: another thread stopping while the render thread is mid-callback must
/// still tear the device down, which the teardown handles by waiting for the
/// callback to return. The marker tells the two apart by thread identity.

#include <atomic>
#include <thread>

namespace statusbar::audio {

class RenderThreadMarker
{
  public:
    /// Called on entry to the audio callback: remembers the calling thread.
    /// A relaxed store of a trivially copyable id; nothing else happens here.
    void mark_current() noexcept { id_.store(std::this_thread::get_id(), std::memory_order_relaxed); }

    /// True when the calling thread is the one that last entered the callback.
    /// False before any callback has run.
    [[nodiscard]] auto is_current_thread() const noexcept -> bool
    {
        return id_.load(std::memory_order_relaxed) == std::this_thread::get_id();
    }

  private:
    std::atomic<std::thread::id> id_{};
};

/// Whether a stop() called now must leave the device teardown to the
/// destructor: exactly when the caller is the render thread. Pure function of
/// the marker so the decision is testable without hardware.
[[nodiscard]] inline auto stop_must_defer_teardown(RenderThreadMarker const& render_thread) noexcept -> bool
{
    return render_thread.is_current_thread();
}

}  // namespace statusbar::audio
