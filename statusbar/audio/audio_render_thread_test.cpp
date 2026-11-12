// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// RenderThreadMarker: the stop()-from-the-render-thread decision

#include "statusbar/audio/audio_render_thread.hpp"

#include "statusbar/test/test.hpp"

#include <atomic>
#include <thread>

using namespace statusbar::audio;

TEST(render_thread_marker, nothing_is_the_render_thread_before_a_callback_ran)
{
    RenderThreadMarker const marker;
    EXPECT_FALSE(marker.is_current_thread());
    EXPECT_FALSE(stop_must_defer_teardown(marker));
}

TEST(render_thread_marker, the_thread_that_entered_the_callback_is_the_render_thread)
{
    RenderThreadMarker marker;
    marker.mark_current();
    EXPECT_TRUE(marker.is_current_thread());
    EXPECT_TRUE(stop_must_defer_teardown(marker));
}

TEST(render_thread_marker, another_thread_stopping_mid_callback_is_not_the_render_thread)
{
    // The render thread is inside its callback (marked, not returned) when a
    // control thread calls stop(): the old in_callback > 0 test said "defer"
    // here and left the AudioUnit running. Thread identity says "tear down".
    RenderThreadMarker marker;
    std::atomic<bool> marked{false};
    std::atomic<bool> release{false};
    std::atomic<bool> render_saw_itself{false};
    std::thread render([&] {
        marker.mark_current();
        render_saw_itself.store(marker.is_current_thread());
        marked.store(true);
        while (!release.load()) {
            std::this_thread::yield();
        }
    });
    while (!marked.load()) {
        std::this_thread::yield();
    }

    EXPECT_FALSE(marker.is_current_thread());
    EXPECT_FALSE(stop_must_defer_teardown(marker));
    EXPECT_TRUE(render_saw_itself.load());

    release.store(true);
    render.join();
}

TEST_MAIN(statusbar_audio, audio_render_thread_test)
