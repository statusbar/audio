// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// A stream started again after a self-stop replaces its CallbackHolder on a
// slot the old holder still owns. The slot must end up holding the new
// callback, not the null the old holder's destructor publishes.

#include "statusbar/audio/audio_callback_holder.hpp"

#include "statusbar/status/status.hpp"
#include "statusbar/test/test.hpp"

#include <cstdint>
#include <memory>

using namespace statusbar;
using namespace statusbar::audio;

namespace {

auto counting_callback(int& calls) -> AudioCallbackFloat
{
    return [&calls](AudioCallbackParamsFloat const&) -> Status {
        ++calls;
        return success();
    };
}

/// Invoke whatever the slot holds the way the RT thread does.
auto invoke_slot(AudioCallbackRT& rt) -> bool
{
    auto const cb = rt.load();
    if (cb.fn == nullptr) {
        return false;
    }
    return cb.fn(cb.ctx, nullptr, nullptr, 0, 0.0) == 0;
}

}  // namespace

TEST(audio_callback_holder, publishes_on_construction_and_clears_on_destruction)
{
    AudioCallbackRT rt;
    int calls = 0;
    {
        CallbackHolder holder{counting_callback(calls), rt, 0, 2};
        EXPECT_TRUE(invoke_slot(rt));
        EXPECT_EQ(calls, 1);
    }
    EXPECT_FALSE(invoke_slot(rt));
    EXPECT_EQ(calls, 1);
}

TEST(audio_callback_holder, replacing_the_holder_leaves_the_new_callback_in_the_slot)
{
    AudioCallbackRT rt;
    int first_calls = 0;
    int second_calls = 0;
    std::unique_ptr<CallbackHolder> holder;

    replace_callback_holder(holder, counting_callback(first_calls), rt, 0, 2);
    EXPECT_TRUE(invoke_slot(rt));
    EXPECT_EQ(first_calls, 1);

    // The restart: the old holder's clear must not outlive the new publish.
    replace_callback_holder(holder, counting_callback(second_calls), rt, 0, 2);
    EXPECT_TRUE(invoke_slot(rt));
    EXPECT_EQ(first_calls, 1);
    EXPECT_EQ(second_calls, 1);

    auto const cb = rt.load();
    EXPECT_EQ(cb.ctx, static_cast<void*>(holder.get()));
}

TEST(audio_callback_holder, assigning_over_the_old_holder_is_the_silent_stream)
{
    // The order the Linux restart ran before: make_unique publishes the new
    // callback, then the assignment destroys the old holder, whose destructor
    // publishes a null over it. Documented here so the hazard stays visible.
    AudioCallbackRT rt;
    int calls = 0;
    std::unique_ptr<CallbackHolder> holder = std::make_unique<CallbackHolder>(counting_callback(calls), rt, 0, 2);
    holder = std::make_unique<CallbackHolder>(counting_callback(calls), rt, 0, 2);
    EXPECT_FALSE(invoke_slot(rt));
    EXPECT_EQ(calls, 0);
}

TEST_MAIN(statusbar_audio, audio_callback_holder_test)
