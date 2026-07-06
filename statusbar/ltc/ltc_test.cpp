// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Unit tests for SMPTE LTC module
// Tests multiple frame rates including drop frame

#include "statusbar/ltc/ltc.hpp"

#include "statusbar/audio/audio.hpp"
#include "statusbar/status/status.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <expected>
#include <print>
#include <span>
#include <system_error>
#include <vector>

using namespace statusbar::ltc;

// Static assertions for constexpr verification

// frames_per_second() for all frame rates
static_assert(frames_per_second(FrameRate::Rate_23_976) == 24);
static_assert(frames_per_second(FrameRate::Rate_24) == 24);
static_assert(frames_per_second(FrameRate::Rate_25) == 25);
static_assert(frames_per_second(FrameRate::Rate_29_97_DF) == 30);
static_assert(frames_per_second(FrameRate::Rate_29_97_NDF) == 30);
static_assert(frames_per_second(FrameRate::Rate_30_DF) == 30);
static_assert(frames_per_second(FrameRate::Rate_30_NDF) == 30);

// is_drop_frame() for all frame rates
static_assert(is_drop_frame(FrameRate::Rate_23_976) == false);
static_assert(is_drop_frame(FrameRate::Rate_24) == false);
static_assert(is_drop_frame(FrameRate::Rate_25) == false);
static_assert(is_drop_frame(FrameRate::Rate_29_97_DF) == true);
static_assert(is_drop_frame(FrameRate::Rate_29_97_NDF) == false);
static_assert(is_drop_frame(FrameRate::Rate_30_DF) == true);
static_assert(is_drop_frame(FrameRate::Rate_30_NDF) == false);

// Timecode constexpr validation
static_assert(Timecode{.hours = 0, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF}.is_valid());
static_assert(Timecode{.hours = 23, .minutes = 59, .seconds = 59, .frames = 29, .rate = FrameRate::Rate_30_NDF}.is_valid());
static_assert(!Timecode{.hours = 24, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF}.is_valid());
static_assert(!Timecode{.hours = 0, .minutes = 60, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF}.is_valid());
static_assert(!Timecode{.hours = 0, .minutes = 0, .seconds = 60, .frames = 0, .rate = FrameRate::Rate_30_NDF}.is_valid());
static_assert(!Timecode{.hours = 0, .minutes = 0, .seconds = 0, .frames = 30, .rate = FrameRate::Rate_30_NDF}.is_valid());

// Timecode frame limits for different rates
static_assert(Timecode{.hours = 0, .minutes = 0, .seconds = 0, .frames = 23, .rate = FrameRate::Rate_24}.is_valid());
static_assert(!Timecode{.hours = 0, .minutes = 0, .seconds = 0, .frames = 24, .rate = FrameRate::Rate_24}.is_valid());
static_assert(Timecode{.hours = 0, .minutes = 0, .seconds = 0, .frames = 24, .rate = FrameRate::Rate_25}.is_valid());
static_assert(!Timecode{.hours = 0, .minutes = 0, .seconds = 0, .frames = 25, .rate = FrameRate::Rate_25}.is_valid());

// Drop frame validation - is_dropped_frame()
static_assert(Timecode{.hours = 0, .minutes = 1, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_29_97_DF}.is_dropped_frame());
static_assert(Timecode{.hours = 0, .minutes = 1, .seconds = 0, .frames = 1, .rate = FrameRate::Rate_29_97_DF}.is_dropped_frame());
static_assert(!Timecode{.hours = 0, .minutes = 1, .seconds = 0, .frames = 2, .rate = FrameRate::Rate_29_97_DF}.is_dropped_frame());
static_assert(!Timecode{.hours = 0, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_29_97_DF}.is_dropped_frame());
static_assert(!Timecode{.hours = 0, .minutes = 10, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_29_97_DF}.is_dropped_frame());
static_assert(!Timecode{.hours = 0, .minutes = 20, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_29_97_DF}.is_dropped_frame());

// Timecode frame count conversions (constexpr round-trip)
static_assert(Timecode{.hours = 0, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF}.to_frame_count() == 0);
static_assert(Timecode{.hours = 0, .minutes = 0, .seconds = 0, .frames = 1, .rate = FrameRate::Rate_30_NDF}.to_frame_count() == 1);
static_assert(Timecode{.hours = 0, .minutes = 0, .seconds = 1, .frames = 0, .rate = FrameRate::Rate_30_NDF}.to_frame_count() == 30);
static_assert(
    Timecode{.hours = 0, .minutes = 1, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF}.to_frame_count() == 1800);
static_assert(
    Timecode{.hours = 1, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF}.to_frame_count() == 108000);

// from_frame_count constexpr verification
static_assert(Timecode::from_frame_count(0, FrameRate::Rate_30_NDF).frames == 0);
static_assert(Timecode::from_frame_count(0, FrameRate::Rate_30_NDF).seconds == 0);
static_assert(Timecode::from_frame_count(30, FrameRate::Rate_30_NDF).seconds == 1);
static_assert(Timecode::from_frame_count(30, FrameRate::Rate_30_NDF).frames == 0);
static_assert(Timecode::from_frame_count(1800, FrameRate::Rate_30_NDF).minutes == 1);
static_assert(Timecode::from_frame_count(108000, FrameRate::Rate_30_NDF).hours == 1);

// LTCFrame constexpr construction and bit access
static_assert([]() constexpr {
    LTCFrame frame{Timecode{.hours = 1, .minutes = 2, .seconds = 3, .frames = 4, .rate = FrameRate::Rate_30_NDF}};
    return frame.data().size() == 10;  // 80 bits = 10 bytes
}());

// LTCFrame sync word verification (bits 64-79 = 0xBFFC)
static_assert([]() constexpr {
    LTCFrame frame{Timecode{.hours = 0, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF}};
    auto data = frame.data();
    // Sync word is in bytes 8-9 (bits 64-79), LSB first: 0xFC, 0xBF
    return data[8] == 0xFC && data[9] == 0xBF;
}());

// Timecode validation tests

TEST(timecode, validation_30ndf)
{
    Timecode tc{.hours = 10, .minutes = 30, .seconds = 45, .frames = 15, .rate = FrameRate::Rate_30_NDF};
    EXPECT_TRUE(tc.is_valid());

    Timecode invalid1{.hours = 24, .rate = FrameRate::Rate_30_NDF};  // Invalid hour
    EXPECT_FALSE(invalid1.is_valid());

    Timecode invalid2{.minutes = 60, .rate = FrameRate::Rate_30_NDF};  // Invalid minutes
    EXPECT_FALSE(invalid2.is_valid());

    Timecode invalid3{.seconds = 60, .rate = FrameRate::Rate_30_NDF};  // Invalid seconds
    EXPECT_FALSE(invalid3.is_valid());

    Timecode invalid4{.frames = 30, .rate = FrameRate::Rate_30_NDF};  // Invalid frame (30fps is 0-29)
    EXPECT_FALSE(invalid4.is_valid());
}

TEST(timecode, validation_24fps)
{
    Timecode tc{.hours = 10, .minutes = 30, .seconds = 45, .frames = 23, .rate = FrameRate::Rate_24};
    EXPECT_TRUE(tc.is_valid());

    Timecode invalid{.frames = 24, .rate = FrameRate::Rate_24};  // Invalid frame (24fps is 0-23)
    EXPECT_FALSE(invalid.is_valid());
}

TEST(timecode, validation_25fps)
{
    Timecode tc{.hours = 10, .minutes = 30, .seconds = 45, .frames = 24, .rate = FrameRate::Rate_25};
    EXPECT_TRUE(tc.is_valid());

    Timecode invalid{.frames = 25, .rate = FrameRate::Rate_25};  // Invalid frame (25fps is 0-24)
    EXPECT_FALSE(invalid.is_valid());
}

TEST(timecode, validation_drop_frame)
{
    // Valid drop frame timecodes
    Timecode valid1{.hours = 1, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_29_97_DF};
    EXPECT_TRUE(valid1.is_valid());  // Frame 0 OK at minute 0

    Timecode valid2{.hours = 1, .minutes = 10, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_29_97_DF};
    EXPECT_TRUE(valid2.is_valid());  // Frame 0 OK at minute 10

    Timecode valid3{.hours = 1, .minutes = 1, .seconds = 0, .frames = 2, .rate = FrameRate::Rate_29_97_DF};
    EXPECT_TRUE(valid3.is_valid());  // Frame 2 OK at minute 1

    // Invalid drop frame timecodes - frames 0, 1 dropped at non-10th minutes
    Timecode invalid1{.hours = 1, .minutes = 1, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_29_97_DF};
    EXPECT_FALSE(invalid1.is_valid());  // Frame 0 dropped at minute 1

    Timecode invalid2{.hours = 1, .minutes = 1, .seconds = 0, .frames = 1, .rate = FrameRate::Rate_29_97_DF};
    EXPECT_FALSE(invalid2.is_valid());  // Frame 1 dropped at minute 1

    Timecode invalid3{.hours = 1, .minutes = 5, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_29_97_DF};
    EXPECT_FALSE(invalid3.is_valid());  // Frame 0 dropped at minute 5
}

// Timecode increment tests

TEST(timecode, increment_30ndf)
{
    Timecode tc{.hours = 1, .minutes = 30, .seconds = 59, .frames = 29, .rate = FrameRate::Rate_30_NDF};

    tc.increment_frame();
    EXPECT_EQ(tc.frames, 0);
    EXPECT_EQ(tc.seconds, 0);
    EXPECT_EQ(tc.minutes, 31);
    EXPECT_EQ(tc.hours, 1);

    // Test rollover at 59 minutes
    tc = Timecode{.hours = 2, .minutes = 59, .seconds = 59, .frames = 29, .rate = FrameRate::Rate_30_NDF};
    tc.increment_frame();
    EXPECT_EQ(tc.frames, 0);
    EXPECT_EQ(tc.seconds, 0);
    EXPECT_EQ(tc.minutes, 0);
    EXPECT_EQ(tc.hours, 3);

    // Test rollover at 23 hours
    tc = Timecode{.hours = 23, .minutes = 59, .seconds = 59, .frames = 29, .rate = FrameRate::Rate_30_NDF};
    tc.increment_frame();
    EXPECT_EQ(tc.frames, 0);
    EXPECT_EQ(tc.seconds, 0);
    EXPECT_EQ(tc.minutes, 0);
    EXPECT_EQ(tc.hours, 0);
}

TEST(timecode, increment_24fps)
{
    Timecode tc{.hours = 1, .minutes = 0, .seconds = 59, .frames = 23, .rate = FrameRate::Rate_24};

    tc.increment_frame();
    EXPECT_EQ(tc.frames, 0);
    EXPECT_EQ(tc.seconds, 0);
    EXPECT_EQ(tc.minutes, 1);
}

TEST(timecode, increment_drop_frame)
{
    // Test drop frame skip at minute boundary
    Timecode tc{.hours = 1, .minutes = 0, .seconds = 59, .frames = 29, .rate = FrameRate::Rate_29_97_DF};

    tc.increment_frame();
    // At minute 1, frames 0 and 1 are skipped
    EXPECT_EQ(tc.frames, 2);  // Skipped to frame 2
    EXPECT_EQ(tc.seconds, 0);
    EXPECT_EQ(tc.minutes, 1);
    EXPECT_EQ(tc.hours, 1);

    // Test NO skip at 10th minute
    tc = Timecode{.hours = 1, .minutes = 9, .seconds = 59, .frames = 29, .rate = FrameRate::Rate_29_97_DF};
    tc.increment_frame();
    EXPECT_EQ(tc.frames, 0);  // No skip at minute 10
    EXPECT_EQ(tc.minutes, 10);

    // Test skip at minute 11
    tc = Timecode{.hours = 1, .minutes = 10, .seconds = 59, .frames = 29, .rate = FrameRate::Rate_29_97_DF};
    tc.increment_frame();
    EXPECT_EQ(tc.frames, 2);  // Skipped at minute 11
    EXPECT_EQ(tc.minutes, 11);
}

// Frame count tests

TEST(timecode, frame_count_30ndf)
{
    Timecode tc{.hours = 1, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF};
    uint32_t expected = 1 * 3600 * 30;  // 1 hour = 108000 frames
    EXPECT_EQ(tc.to_frame_count(), expected);

    tc = Timecode{.hours = 0, .minutes = 1, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF};
    expected = 1 * 60 * 30;  // 1 minute = 1800 frames
    EXPECT_EQ(tc.to_frame_count(), expected);

    tc = Timecode{.hours = 0, .minutes = 0, .seconds = 1, .frames = 0, .rate = FrameRate::Rate_30_NDF};
    expected = 1 * 30;  // 1 second = 30 frames
    EXPECT_EQ(tc.to_frame_count(), expected);
}

TEST(timecode, frame_count_24fps)
{
    Timecode tc{.hours = 1, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_24};
    uint32_t expected = 1 * 3600 * 24;  // 1 hour = 86400 frames
    EXPECT_EQ(tc.to_frame_count(), expected);
}

TEST(timecode, frame_count_drop_frame)
{
    // At minute 1, we've dropped 2 frames
    Timecode tc{.hours = 0, .minutes = 1, .seconds = 0, .frames = 2, .rate = FrameRate::Rate_29_97_DF};
    // 60 seconds * 30 fps - 2 dropped + 2 = 1800 frames
    uint32_t expected = 60 * 30;
    EXPECT_EQ(tc.to_frame_count(), expected);

    // At minute 10 (no drops for this minute, but 9 * 2 = 18 drops occurred before)
    tc = Timecode{.hours = 0, .minutes = 10, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_29_97_DF};
    // 10 * 60 * 30 - 18 = 17982 frames
    expected = 10 * 60 * 30 - 18;
    EXPECT_EQ(tc.to_frame_count(), expected);
}

TEST(timecode, from_frame_count_30ndf)
{
    auto tc = Timecode::from_frame_count(108000, FrameRate::Rate_30_NDF);  // 1 hour
    EXPECT_EQ(tc.hours, 1);
    EXPECT_EQ(tc.minutes, 0);
    EXPECT_EQ(tc.seconds, 0);
    EXPECT_EQ(tc.frames, 0);

    tc = Timecode::from_frame_count(1815, FrameRate::Rate_30_NDF);  // 1 minute, 0.5 seconds
    EXPECT_EQ(tc.hours, 0);
    EXPECT_EQ(tc.minutes, 1);
    EXPECT_EQ(tc.seconds, 0);
    EXPECT_EQ(tc.frames, 15);
}

TEST(timecode, from_frame_count_drop_frame)
{
    // Round-trip test: convert to frame count and back
    Timecode original{.hours = 1, .minutes = 23, .seconds = 45, .frames = 15, .rate = FrameRate::Rate_29_97_DF};
    uint32_t frame_count = original.to_frame_count();
    auto reconstructed = Timecode::from_frame_count(frame_count, FrameRate::Rate_29_97_DF);

    EXPECT_EQ(reconstructed.hours, original.hours);
    EXPECT_EQ(reconstructed.minutes, original.minutes);
    EXPECT_EQ(reconstructed.seconds, original.seconds);
    EXPECT_EQ(reconstructed.frames, original.frames);
}

TEST(timecode, string)
{
    Timecode tc{.hours = 1, .minutes = 30, .seconds = 45, .frames = 15, .rate = FrameRate::Rate_30_NDF};
    EXPECT_EQ(tc.to_string(), "01:30:45:15");

    // Drop frame uses semicolon separator
    tc.rate = FrameRate::Rate_29_97_DF;
    EXPECT_EQ(tc.to_string(), "01:30:45;15");
}

// Timecode decrement tests

TEST(timecode, decrement_30ndf)
{
    // Simple decrement within a second
    Timecode tc{.hours = 1, .minutes = 30, .seconds = 45, .frames = 15, .rate = FrameRate::Rate_30_NDF};
    tc.decrement_frame();
    EXPECT_EQ(tc.frames, 14);
    EXPECT_EQ(tc.seconds, 45);
    EXPECT_EQ(tc.minutes, 30);
    EXPECT_EQ(tc.hours, 1);

    // Decrement across second boundary
    tc = Timecode{.hours = 1, .minutes = 30, .seconds = 45, .frames = 0, .rate = FrameRate::Rate_30_NDF};
    tc.decrement_frame();
    EXPECT_EQ(tc.frames, 29);
    EXPECT_EQ(tc.seconds, 44);
    EXPECT_EQ(tc.minutes, 30);
    EXPECT_EQ(tc.hours, 1);

    // Decrement across minute boundary
    tc = Timecode{.hours = 1, .minutes = 30, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF};
    tc.decrement_frame();
    EXPECT_EQ(tc.frames, 29);
    EXPECT_EQ(tc.seconds, 59);
    EXPECT_EQ(tc.minutes, 29);
    EXPECT_EQ(tc.hours, 1);

    // Decrement across hour boundary
    tc = Timecode{.hours = 1, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF};
    tc.decrement_frame();
    EXPECT_EQ(tc.frames, 29);
    EXPECT_EQ(tc.seconds, 59);
    EXPECT_EQ(tc.minutes, 59);
    EXPECT_EQ(tc.hours, 0);

    // Decrement at midnight (wraps to 23:59:59:29)
    tc = Timecode{.hours = 0, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF};
    tc.decrement_frame();
    EXPECT_EQ(tc.frames, 29);
    EXPECT_EQ(tc.seconds, 59);
    EXPECT_EQ(tc.minutes, 59);
    EXPECT_EQ(tc.hours, 23);
}

TEST(timecode, decrement_24fps)
{
    Timecode tc{.hours = 1, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_24};
    tc.decrement_frame();
    EXPECT_EQ(tc.frames, 23);  // 24fps goes to frame 23
    EXPECT_EQ(tc.seconds, 59);
    EXPECT_EQ(tc.minutes, 59);
    EXPECT_EQ(tc.hours, 0);
}

TEST(timecode, decrement_drop_frame)
{
    // Decrement from frame 2 at minute 1 should go to frame 29 of previous second
    // (skipping the dropped frames 0 and 1)
    Timecode tc{.hours = 1, .minutes = 1, .seconds = 0, .frames = 2, .rate = FrameRate::Rate_29_97_DF};
    tc.decrement_frame();
    EXPECT_EQ(tc.frames, 29);
    EXPECT_EQ(tc.seconds, 59);
    EXPECT_EQ(tc.minutes, 0);
    EXPECT_EQ(tc.hours, 1);

    // Decrement at 10th minute boundary (no drop frame skip)
    tc = Timecode{.hours = 1, .minutes = 10, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_29_97_DF};
    tc.decrement_frame();
    EXPECT_EQ(tc.frames, 29);
    EXPECT_EQ(tc.seconds, 59);
    EXPECT_EQ(tc.minutes, 9);

    // Decrement from frame 3 at minute 1 should go to frame 2
    tc = Timecode{.hours = 1, .minutes = 1, .seconds = 0, .frames = 3, .rate = FrameRate::Rate_29_97_DF};
    tc.decrement_frame();
    EXPECT_EQ(tc.frames, 2);
    EXPECT_EQ(tc.seconds, 0);
    EXPECT_EQ(tc.minutes, 1);
}

TEST(timecode, increment_decrement_roundtrip)
{
    // Increment then decrement should return to original
    Timecode original{.hours = 1, .minutes = 30, .seconds = 45, .frames = 15, .rate = FrameRate::Rate_30_NDF};
    Timecode tc = original;
    tc.increment_frame();
    tc.decrement_frame();
    EXPECT_EQ(tc, original);

    // Test with drop frame
    original = Timecode{.hours = 1, .minutes = 0, .seconds = 59, .frames = 29, .rate = FrameRate::Rate_29_97_DF};
    tc = original;
    tc.increment_frame();  // Goes to 01:01:00;02 (skips 0, 1)
    tc.decrement_frame();  // Should go back to 01:00:59;29
    EXPECT_EQ(tc, original);
}

// Timecodeis_dropped_frame tests =====

TEST(timecode, is_dropped_frame)
{
    // Non-drop frame rates never have dropped frames
    Timecode tc_ndf{.hours = 1, .minutes = 1, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF};
    EXPECT_FALSE(tc_ndf.is_dropped_frame());

    tc_ndf.frames = 1;
    EXPECT_FALSE(tc_ndf.is_dropped_frame());

    // Drop frame: frames 0 and 1 at non-10th minute boundaries are dropped
    Timecode tc_df{.hours = 1, .minutes = 1, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_29_97_DF};
    EXPECT_TRUE(tc_df.is_dropped_frame());

    tc_df.frames = 1;
    EXPECT_TRUE(tc_df.is_dropped_frame());

    tc_df.frames = 2;
    EXPECT_FALSE(tc_df.is_dropped_frame());  // Frame 2 is not dropped

    // 10th minute: no frames are dropped
    tc_df.minutes = 10;
    tc_df.frames = 0;
    EXPECT_FALSE(tc_df.is_dropped_frame());

    tc_df.frames = 1;
    EXPECT_FALSE(tc_df.is_dropped_frame());

    // Minute 0: no frames are dropped
    tc_df.minutes = 0;
    tc_df.frames = 0;
    EXPECT_FALSE(tc_df.is_dropped_frame());

    // Non-zero seconds: no frames are dropped
    tc_df.minutes = 1;
    tc_df.seconds = 30;
    tc_df.frames = 0;
    EXPECT_FALSE(tc_df.is_dropped_frame());
}

// Timecode comparison tests

TEST(timecode, comparison)
{
    Timecode tc1{.hours = 1, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF};
    Timecode tc2{.hours = 1, .minutes = 0, .seconds = 0, .frames = 1, .rate = FrameRate::Rate_30_NDF};
    Timecode tc3{.hours = 1, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF};

    // Equality
    EXPECT_TRUE(tc1 == tc3);
    EXPECT_FALSE(tc1 == tc2);

    // Less than
    EXPECT_TRUE(tc1 < tc2);
    EXPECT_FALSE(tc2 < tc1);
    EXPECT_FALSE(tc1 < tc3);

    // Greater than
    EXPECT_TRUE(tc2 > tc1);
    EXPECT_FALSE(tc1 > tc2);

    // Less than or equal
    EXPECT_TRUE(tc1 <= tc2);
    EXPECT_TRUE(tc1 <= tc3);
    EXPECT_FALSE(tc2 <= tc1);

    // Greater than or equal
    EXPECT_TRUE(tc2 >= tc1);
    EXPECT_TRUE(tc1 >= tc3);
    EXPECT_FALSE(tc1 >= tc2);
}

TEST(timecode, comparison_drop_frame)
{
    // Drop frame timecodes should compare correctly even with gaps
    Timecode tc1{.hours = 1, .minutes = 0, .seconds = 59, .frames = 29, .rate = FrameRate::Rate_29_97_DF};
    Timecode tc2{.hours = 1, .minutes = 1, .seconds = 0, .frames = 2, .rate = FrameRate::Rate_29_97_DF};

    EXPECT_TRUE(tc1 < tc2);
    EXPECT_TRUE(tc2 > tc1);

    // tc2 is exactly one frame after tc1
    EXPECT_EQ(tc2.to_frame_count() - tc1.to_frame_count(), 1);
}

// LTC frame encoding tests

TEST(frame, sync_word)
{
    Timecode tc{.rate = FrameRate::Rate_30_NDF};
    LTCFrame frame(tc);

    // Check sync word (bits 64-79) = 0x3FFD
    // In our bit order: 0xBFFC
    uint16_t sync_word = 0;
    for (size_t i = 0; i < 16; ++i) {
        if (frame.get_bit(64 + i)) {
            sync_word |= (1 << i);
        }
    }
    EXPECT_EQ(sync_word, 0xBFFC);  // 0x3FFD reversed per SMPTE bit order
}

TEST(frame, timecode_encoding)
{
    Timecode tc{.hours = 12, .minutes = 34, .seconds = 56, .frames = 23, .rate = FrameRate::Rate_30_NDF};
    LTCFrame frame(tc);

    // Verify frame units (bits 0-3) = 3 (BCD)
    uint8_t frame_units = 0;
    for (size_t i = 0; i < 4; ++i) {
        if (frame.get_bit(i)) {
            frame_units |= (1 << i);
        }
    }
    EXPECT_EQ(frame_units, 3);

    // Verify frame tens (bits 8-9) = 2 (BCD)
    uint8_t frame_tens = 0;
    for (size_t i = 0; i < 2; ++i) {
        if (frame.get_bit(8 + i)) {
            frame_tens |= (1 << i);
        }
    }
    EXPECT_EQ(frame_tens, 2);

    // Verify seconds units (bits 16-19) = 6 (BCD)
    uint8_t sec_units = 0;
    for (size_t i = 0; i < 4; ++i) {
        if (frame.get_bit(16 + i)) {
            sec_units |= (1 << i);
        }
    }
    EXPECT_EQ(sec_units, 6);

    // Verify seconds tens (bits 24-26) = 5 (BCD)
    uint8_t sec_tens = 0;
    for (size_t i = 0; i < 3; ++i) {
        if (frame.get_bit(24 + i)) {
            sec_tens |= (1 << i);
        }
    }
    EXPECT_EQ(sec_tens, 5);
}

TEST(frame, drop_frame_flag)
{
    // Non-drop frame should have bit 10 = 0
    Timecode tc_ndf{.rate = FrameRate::Rate_30_NDF};
    LTCFrame frame_ndf(tc_ndf);
    EXPECT_FALSE(frame_ndf.get_bit(10));

    // Drop frame should have bit 10 = 1
    Timecode tc_df{.rate = FrameRate::Rate_29_97_DF};
    LTCFrame frame_df(tc_df);
    EXPECT_TRUE(frame_df.get_bit(10));
}

namespace {
// Count one-bits across the whole 80-bit frame via the public bit accessor.
auto frame_one_bits(LTCFrame const& frame) -> size_t
{
    size_t ones = 0;
    for (size_t i = 0; i < 80; ++i) {
        if (frame.get_bit(i)) {
            ++ones;
        }
    }
    return ones;
}
}  // namespace

TEST(frame, polarity_even_parity_all_rates)
{
    // SMPTE 12M: the polarity-correction bit must make the entire 80-bit frame
    // contain an even number of one-bits (so every frame's biphase waveform
    // starts at the same polarity). Exercise every rate across a spread of
    // timecodes and user-bit patterns — the sync word alone carries an odd 13
    // ones, so an implementation that forgets to include it (or the user bits)
    // fails here.
    FrameRate const rates[] = {
        FrameRate::Rate_23_976,
        FrameRate::Rate_24,
        FrameRate::Rate_25,
        FrameRate::Rate_29_97_DF,
        FrameRate::Rate_29_97_NDF,
        FrameRate::Rate_30_DF,
        FrameRate::Rate_30_NDF};
    uint32_t const user_bit_patterns[] = {0x00000000u, 0xFFFFFFFFu, 0xA5A5A5A5u, 0x12345678u};

    for (auto const rate : rates) {
        for (auto const ubits : user_bit_patterns) {
            for (uint8_t h = 0; h < 24; h += 7) {
                for (uint8_t s = 0; s < 60; s += 13) {
                    Timecode tc{.hours = h, .minutes = 45, .seconds = s, .frames = 12, .user_bits = ubits, .rate = rate};
                    LTCFrame frame(tc);
                    EXPECT_EQ(frame_one_bits(frame) % 2, size_t{0});
                }
            }
        }
    }
}

TEST(frame, polarity_bit_position_by_rate)
{
    // At 25 fps the correction bit is 27; bit 59 is a binary-group flag and must
    // stay 0. At every other rate the correction bit is 59; bit 27 (BGF0) stays
    // 0. Pick a timecode whose other 79 bits have odd parity so the correction
    // bit is actually driven to 1.
    // 25 fps: correction at bit 27.
    {
        Timecode tc{.hours = 1, .minutes = 0, .seconds = 0, .frames = 1, .rate = FrameRate::Rate_25};
        LTCFrame frame(tc);
        EXPECT_EQ(frame_one_bits(frame) % 2, size_t{0});
        EXPECT_FALSE(frame.get_bit(59));  // bit 59 is a BGF at 25 fps, never the correction bit
    }
    // 30 fps: correction at bit 59.
    {
        Timecode tc{.hours = 1, .minutes = 0, .seconds = 0, .frames = 1, .rate = FrameRate::Rate_30_NDF};
        LTCFrame frame(tc);
        EXPECT_EQ(frame_one_bits(frame) % 2, size_t{0});
        EXPECT_FALSE(frame.get_bit(27));  // bit 27 is BGF0 at 30 fps, never the correction bit
    }
}

TEST(frame, data_accessor)
{
    Timecode tc{.hours = 12, .minutes = 34, .seconds = 56, .frames = 23, .rate = FrameRate::Rate_30_NDF};
    LTCFrame frame(tc);

    // Get the raw data array
    auto data = frame.data();

    // Should be 10 bytes (80 bits)
    EXPECT_EQ(data.size(), 10);

    // Verify sync word in last two bytes (bits 64-79)
    // Sync word is 0xBFFC in our bit order
    EXPECT_EQ(data[8], 0xFC);
    EXPECT_EQ(data[9], 0xBF);

    // Verify frame units in first nibble (bits 0-3)
    // Frame 23: units = 3
    EXPECT_EQ(data[0] & 0x0F, 3);
}

TEST(frame, get_bit_out_of_range)
{
    Timecode tc{.rate = FrameRate::Rate_30_NDF};
    LTCFrame frame(tc);

    // Bit 79 should be valid (last bit)
    // Just check it doesn't crash
    (void)frame.get_bit(79);

    // Bit 80 and beyond should return false (out of range)
    EXPECT_FALSE(frame.get_bit(80));
    EXPECT_FALSE(frame.get_bit(100));
    EXPECT_FALSE(frame.get_bit(1000));
}

// Audio generator tests

TEST(generator, sample_counts_30fps)
{
    Generator gen_44(Generator::SampleRate::Rate_44100, FrameRate::Rate_30_NDF);
    EXPECT_EQ(gen_44.sample_rate(), 44100);
    EXPECT_EQ(gen_44.samples_per_frame(), 1470);  // 44100 / 30

    Generator gen_48(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);
    EXPECT_EQ(gen_48.sample_rate(), 48000);
    EXPECT_EQ(gen_48.samples_per_frame(), 1600);  // 48000 / 30

    Generator gen_96(Generator::SampleRate::Rate_96000, FrameRate::Rate_30_NDF);
    EXPECT_EQ(gen_96.sample_rate(), 96000);
    EXPECT_EQ(gen_96.samples_per_frame(), 3200);  // 96000 / 30
}

TEST(generator, sample_counts_24fps)
{
    Generator gen_48(Generator::SampleRate::Rate_48000, FrameRate::Rate_24);
    EXPECT_EQ(gen_48.sample_rate(), 48000);
    EXPECT_EQ(gen_48.samples_per_frame(), 2000);  // 48000 / 24
}

TEST(generator, sample_counts_25fps)
{
    Generator gen_48(Generator::SampleRate::Rate_48000, FrameRate::Rate_25);
    EXPECT_EQ(gen_48.sample_rate(), 48000);
    EXPECT_EQ(gen_48.samples_per_frame(), 1920);  // 48000 / 25
}

TEST(generator, sample_counts_29_97fps)
{
    Generator gen_48(Generator::SampleRate::Rate_48000, FrameRate::Rate_29_97_DF);
    EXPECT_EQ(gen_48.sample_rate(), 48000);
    // 48000 / (30000/1001) = 48000 * 1001 / 30000 = 1601.6
    // Rounded to 1602
    EXPECT_EQ(gen_48.samples_per_frame(), 1602);
}

TEST(generator, output_length)
{
    Generator gen(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);
    Timecode tc{.hours = 1, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF};

    std::vector<float> samples;
    samples.reserve(gen.samples_per_frame());
    gen.generate_frame(tc, samples);
    EXPECT_EQ(samples.size(), gen.samples_per_frame());
}

TEST(generator, output_range)
{
    Generator gen(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);
    Timecode tc{.rate = FrameRate::Rate_30_NDF};

    std::vector<float> samples;
    samples.reserve(gen.samples_per_frame());
    gen.generate_frame(tc, samples);

    // With rise/fall time, samples should be in range [-1.0, +1.0]
    // (not just the exact endpoints)
    for (float sample : samples) {
        EXPECT_TRUE(sample >= -1.0f && sample <= 1.0f);
    }
}

TEST(generator, transitions)
{
    // Use longer rise time (100µs) to ensure we capture intermediate samples
    Generator gen(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF, 100.0);
    Timecode tc{.rate = FrameRate::Rate_30_NDF};

    std::vector<float> samples;
    samples.reserve(gen.samples_per_frame());
    gen.generate_frame(tc, samples);

    // With rise/fall time shaping, verify we have transition regions
    // Count samples that are clearly in transition (not at exact endpoints)
    size_t transition_samples = 0;
    for (float sample : samples) {
        if (sample > -0.99f && sample < 0.99f) {
            ++transition_samples;
        }
    }

    // Should have many samples in transition with 100µs rise time
    EXPECT_TRUE(transition_samples > 100);
}

TEST(generator, frame_boundary_continuity)
{
    // Consecutive frames must join without a waveform discontinuity: the last
    // sample of one frame equals the first sample of the next (both sit at the
    // carried biphase level). A generator that reset its level every frame would
    // jump here whenever a frame ended on the opposite polarity.
    Generator gen(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);
    Timecode tc{.hours = 10, .minutes = 20, .seconds = 30, .frames = 0, .rate = FrameRate::Rate_30_NDF};

    std::vector<float> prev;
    prev.reserve(gen.samples_per_frame());
    gen.generate_frame(tc, prev);

    for (int i = 0; i < 8; ++i) {
        tc.increment_frame();
        std::vector<float> cur;
        cur.reserve(gen.samples_per_frame());
        gen.generate_frame(tc, cur);
        EXPECT_FALSE(cur.empty());
        EXPECT_EQ(prev.back(), cur.front());
        prev = std::move(cur);
    }
}

TEST(generator, rise_time)
{
    // Test with 100µs rise time (longer than default for clearer intermediate values)
    // At 48kHz, 100µs = ~5 samples, which should show clear intermediate transitions
    Generator gen(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF, 100.0);
    Timecode tc{.rate = FrameRate::Rate_30_NDF};

    std::vector<float> samples;
    samples.reserve(gen.samples_per_frame());
    gen.generate_frame(tc, samples);

    // Verify we have gradual transitions with intermediate values
    bool found_intermediate_value = false;
    for (float sample : samples) {
        // Look for samples that are clearly in transition (not near the endpoints)
        if (sample > -0.9f && sample < -0.1f) {
            found_intermediate_value = true;
            break;
        }
        if (sample > 0.1f && sample < 0.9f) {
            found_intermediate_value = true;
            break;
        }
    }

    EXPECT_TRUE(found_intermediate_value);
}

TEST(generator, frame_rate_accessor)
{
    Generator gen_30ndf(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);
    EXPECT_EQ(gen_30ndf.frame_rate(), FrameRate::Rate_30_NDF);

    Generator gen_24(Generator::SampleRate::Rate_48000, FrameRate::Rate_24);
    EXPECT_EQ(gen_24.frame_rate(), FrameRate::Rate_24);

    Generator gen_29_97df(Generator::SampleRate::Rate_48000, FrameRate::Rate_29_97_DF);
    EXPECT_EQ(gen_29_97df.frame_rate(), FrameRate::Rate_29_97_DF);

    Generator gen_25(Generator::SampleRate::Rate_48000, FrameRate::Rate_25);
    EXPECT_EQ(gen_25.frame_rate(), FrameRate::Rate_25);
}

TEST(generator, samples_per_bit_accessor)
{
    // 30 fps at 48kHz: 80 bits per frame, 1600 samples per frame
    // samples_per_bit = 1600 / 80 = 20
    Generator gen_30(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);
    double spb_30 = gen_30.samples_per_bit();
    EXPECT_TRUE(spb_30 > 19.9 && spb_30 < 20.1);

    // 24 fps at 48kHz: 80 bits per frame, 2000 samples per frame
    // samples_per_bit = 2000 / 80 = 25
    Generator gen_24(Generator::SampleRate::Rate_48000, FrameRate::Rate_24);
    double spb_24 = gen_24.samples_per_bit();
    EXPECT_TRUE(spb_24 > 24.9 && spb_24 < 25.1);

    // 29.97 fps at 48kHz: different calculation due to fractional rate
    Generator gen_29_97(Generator::SampleRate::Rate_48000, FrameRate::Rate_29_97_DF);
    double spb_29_97 = gen_29_97.samples_per_bit();
    // 48000 / (80 * 29.97) ≈ 20.02
    EXPECT_TRUE(spb_29_97 > 20.0 && spb_29_97 < 20.1);
}

// Servo generator tests

TEST(servo, basic_operation)
{
    ServoGenerator servo(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);
    Timecode tc{.hours = 1, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF};

    servo.set_timecode(tc);
    EXPECT_EQ(servo.current_timecode(), tc);

    std::vector<float> samples;
    samples.reserve(100);
    servo.get_samples(100, samples);
    EXPECT_EQ(samples.size(), 100);
}

TEST(servo, timecode_advancement)
{
    ServoGenerator servo(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);
    Timecode tc{.hours = 1, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF};

    servo.set_timecode(tc);

    // Request more samples than one frame
    size_t samples_per_frame = 1600;  // 48000 / 30
    std::vector<float> samples;
    samples.reserve(samples_per_frame + 100);
    servo.get_samples(samples_per_frame + 100, samples);

    // Timecode should have advanced
    EXPECT_EQ(servo.current_timecode().frames, 1);
}

TEST(servo, phase_adjustment)
{
    ServoGenerator servo(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);

    // Initial adjustment should be 1.0 (no correction)
    EXPECT_EQ(servo.phase_adjustment(), 1.0);

    // Simulate timing error
    servo.update_timing(1.0, 0.9);  // Actual ahead of expected

    // Adjustment should be slightly above 1.0 to slow down
    EXPECT_TRUE(servo.phase_adjustment() > 1.0);
    EXPECT_TRUE(servo.phase_adjustment() < 1.01);
}

TEST(servo, drop_frame_advancement)
{
    ServoGenerator servo(Generator::SampleRate::Rate_48000, FrameRate::Rate_29_97_DF);
    Timecode tc{.hours = 1, .minutes = 0, .seconds = 59, .frames = 29, .rate = FrameRate::Rate_29_97_DF};

    servo.set_timecode(tc);

    // Request enough samples to advance past frame boundary
    size_t samples_per_frame = 1602;  // ~48000 / 29.97
    std::vector<float> samples;
    samples.reserve(samples_per_frame + 100);
    servo.get_samples(samples_per_frame + 100, samples);

    // Timecode should have advanced and skipped to frame 2 at minute 1
    EXPECT_EQ(servo.current_timecode().frames, 2);
    EXPECT_EQ(servo.current_timecode().minutes, 1);
}

TEST(servo, frame_rate_accessor)
{
    ServoGenerator servo_30ndf(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);
    EXPECT_EQ(servo_30ndf.frame_rate(), FrameRate::Rate_30_NDF);

    ServoGenerator servo_24(Generator::SampleRate::Rate_48000, FrameRate::Rate_24);
    EXPECT_EQ(servo_24.frame_rate(), FrameRate::Rate_24);

    ServoGenerator servo_29_97df(Generator::SampleRate::Rate_48000, FrameRate::Rate_29_97_DF);
    EXPECT_EQ(servo_29_97df.frame_rate(), FrameRate::Rate_29_97_DF);

    ServoGenerator servo_25(Generator::SampleRate::Rate_48000, FrameRate::Rate_25);
    EXPECT_EQ(servo_25.frame_rate(), FrameRate::Rate_25);
}

TEST(servo, reset)
{
    ServoGenerator servo(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);
    Timecode tc{.hours = 1, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF};
    servo.set_timecode(tc);

    // Introduce some timing error
    servo.update_timing(1.0, 0.9);
    EXPECT_NE(servo.phase_adjustment(), 1.0);

    // Get some samples to advance buffer position
    std::vector<float> samples;
    samples.reserve(100);
    servo.get_samples(100, samples);

    // Reset should restore initial state
    servo.reset();
    EXPECT_EQ(servo.phase_adjustment(), 1.0);
}

TEST(servo, reset_accumulated_error)
{
    ServoGenerator servo(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);
    Timecode tc{.hours = 1, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF};
    servo.set_timecode(tc);

    // Accumulate some error over multiple updates
    servo.update_timing(1.0, 0.99);
    servo.update_timing(2.0, 1.99);
    servo.update_timing(3.0, 2.99);

    double adj_before_reset = servo.phase_adjustment();
    EXPECT_NE(adj_before_reset, 1.0);

    // Reset and verify accumulated error is cleared
    servo.reset();
    EXPECT_EQ(servo.phase_adjustment(), 1.0);

    // After reset, a single small error should produce only proportional response
    // (no integral component from previous errors)
    servo.update_timing(1.0, 0.99);
    double adj_after_reset = servo.phase_adjustment();

    // The adjustment after reset should be smaller than before reset
    // because there's no accumulated integral error
    EXPECT_TRUE(adj_after_reset < adj_before_reset);
}

TEST(servo, anti_windup_recovers_quickly)
{
    // Drive a large sustained error so the output saturates at the +0.1% clamp.
    // Without anti-windup the integrator would wind up huge and stay saturated
    // long after the error reverses; with conditional integration it recovers
    // within a couple of updates.
    ServoGenerator servo(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);
    for (int i = 0; i < 100; ++i) {
        servo.update_timing(1.0, 0.0);  // huge positive error → saturates high
    }
    EXPECT_TRUE(servo.phase_adjustment() >= 1.001 - 1e-9);

    // Reverse the error; a wound-up integrator would keep the output pinned high.
    servo.update_timing(0.0, 1.0);
    EXPECT_TRUE(servo.phase_adjustment() < 1.001);
    servo.update_timing(0.0, 1.0);
    EXPECT_TRUE(servo.phase_adjustment() <= 0.999 + 1e-9);
}

TEST(servo, get_samples_preserves_subsample_phase)
{
    // Consuming the same span in one big chunk vs many small chunks must land at
    // the same timecode — the fractional read position persists across calls,
    // so per-callback quantization can't accumulate a bias.
    auto const total = size_t{48000};
    ServoGenerator servo_a(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);
    ServoGenerator servo_b(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);
    Timecode const tc{.hours = 1, .rate = FrameRate::Rate_30_NDF};
    servo_a.set_timecode(tc);
    servo_b.set_timecode(tc);

    std::vector<float> out;
    out.reserve(total);
    servo_a.get_samples(total, out);
    for (size_t done = 0; done < total; done += 37) {
        servo_b.get_samples(std::min<size_t>(37, total - done), out);
    }
    EXPECT_TRUE(servo_a.current_timecode() == servo_b.current_timecode());
}

TEST(servo_interpolate, within_bounds)
{
    std::vector<float> buf = {0.0f, 1.0f, 2.0f, 3.0f};
    // Exact positions
    EXPECT_EQ(interpolate_sample(buf, 0.0), 0.0f);
    EXPECT_EQ(interpolate_sample(buf, 1.0), 1.0f);
    EXPECT_EQ(interpolate_sample(buf, 2.0), 2.0f);
    // Fractional interpolation
    float mid = interpolate_sample(buf, 0.5);
    EXPECT_TRUE(mid > 0.49f && mid < 0.51f);
}

TEST(servo_interpolate, last_sample)
{
    // pos == size-1: no next sample for interpolation, returns buf[pos]
    std::vector<float> buf = {1.0f, 2.0f, 3.0f};
    EXPECT_EQ(interpolate_sample(buf, 2.0), 3.0f);
}

TEST(servo_interpolate, past_end)
{
    // pos == size: out of bounds, must return 0 not crash
    std::vector<float> buf = {1.0f, 2.0f, 3.0f};
    EXPECT_EQ(interpolate_sample(buf, 3.0), 0.0f);
    EXPECT_EQ(interpolate_sample(buf, 4.0), 0.0f);
    EXPECT_EQ(interpolate_sample(buf, 100.0), 0.0f);
}

TEST(servo_interpolate, empty_buffer)
{
    std::vector<float> buf;
    EXPECT_EQ(interpolate_sample(buf, 0.0), 0.0f);
}

// Playback utility tests

TEST(playback, parse_time_offset_valid)
{
    // Basic valid cases
    auto result = parse_time_offset("00:00:00");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);

    result = parse_time_offset("01:00:00");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 3600);  // 1 hour

    result = parse_time_offset("09:00:00");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 9 * 3600);  // 9 hours

    result = parse_time_offset("12:30:45");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 12 * 3600 + 30 * 60 + 45);

    result = parse_time_offset("23:59:59");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 23 * 3600 + 59 * 60 + 59);
}

TEST(playback, parse_time_offset_invalid_format)
{
    // Wrong length
    EXPECT_FALSE(parse_time_offset("").has_value());
    EXPECT_FALSE(parse_time_offset("0:00:00").has_value());
    EXPECT_FALSE(parse_time_offset("00:0:00").has_value());
    EXPECT_FALSE(parse_time_offset("00:00:0").has_value());
    EXPECT_FALSE(parse_time_offset("000:00:00").has_value());

    // Wrong separators
    EXPECT_FALSE(parse_time_offset("00-00-00").has_value());
    EXPECT_FALSE(parse_time_offset("00;00;00").has_value());
    EXPECT_FALSE(parse_time_offset("00 00 00").has_value());

    // Non-digit characters
    EXPECT_FALSE(parse_time_offset("ab:00:00").has_value());
    EXPECT_FALSE(parse_time_offset("00:cd:00").has_value());
    EXPECT_FALSE(parse_time_offset("00:00:ef").has_value());
}

TEST(playback, parse_time_offset_invalid_values)
{
    // Out of range hours
    EXPECT_FALSE(parse_time_offset("24:00:00").has_value());
    EXPECT_FALSE(parse_time_offset("25:00:00").has_value());
    EXPECT_FALSE(parse_time_offset("99:00:00").has_value());

    // Out of range minutes
    EXPECT_FALSE(parse_time_offset("00:60:00").has_value());
    EXPECT_FALSE(parse_time_offset("00:99:00").has_value());

    // Out of range seconds
    EXPECT_FALSE(parse_time_offset("00:00:60").has_value());
    EXPECT_FALSE(parse_time_offset("00:00:99").has_value());
}

TEST(playback, get_realtime_timestamp)
{
    // Just verify it returns a reasonable value (current epoch time)
    double ts1 = get_realtime_timestamp();
    EXPECT_TRUE(ts1 > 0.0);

    // It should be monotonically increasing
    double ts2 = get_realtime_timestamp();
    EXPECT_TRUE(ts2 >= ts1);
}

TEST(playback, from_realtime_with_offset)
{
    // Test with zero offset (default)
    auto time_info = get_local_time_info();
    auto tc_default = Timecode::from_realtime(FrameRate::Rate_30_NDF, time_info.local_time, time_info.millis);
    auto tc_zero = Timecode::from_realtime(FrameRate::Rate_30_NDF, time_info.local_time, time_info.millis, 0);

    // They should be identical when using zero offset
    EXPECT_EQ(tc_zero.hours, tc_default.hours);
    EXPECT_EQ(tc_zero.minutes, tc_default.minutes);
    EXPECT_EQ(tc_zero.seconds, tc_default.seconds);
    EXPECT_EQ(tc_zero.frames, tc_default.frames);

    // The returned timecode should be valid
    EXPECT_TRUE(tc_zero.is_valid());
}

TEST(playback, from_realtime_with_offset_drop_frame)
{
    // Test drop frame adjustment
    auto time_info = get_local_time_info();
    auto tc = Timecode::from_realtime(FrameRate::Rate_29_97_DF, time_info.local_time, time_info.millis, 0);
    EXPECT_TRUE(tc.is_valid());  // Should never return an invalid drop frame timecode
}

// Fixed playback callback tests

TEST(playback, fixed_callback_basic)
{
    Generator gen(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);
    Timecode start_tc{.hours = 1, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF};

    FixedPlaybackState state(&gen, start_tc, 5);  // Generate 5 frames
    state.frame_buffer.reserve(gen.samples_per_frame());

    // Create mock output buffer
    std::vector<float> output_data(512, 0.0f);
    statusbar::audio::OutputAudioBufferFloat output_buf{std::span<float>(output_data)};
    std::array<statusbar::audio::OutputAudioBufferFloat, 1> output_bufs{output_buf};
    statusbar::audio::AudioCallbackParamsFloat params{
        .input_buffers = {}, .output_buffers = std::span(output_bufs), .stream_time = 0.0};

    // First callback should succeed and fill output
    auto result = process_fixed_callback(state, params);
    EXPECT_TRUE(result.has_value());
    EXPECT_FALSE(state.is_finished());
    EXPECT_TRUE(state.frames_generated > 0);
}

TEST(playback, fixed_callback_completion)
{
    Generator gen(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);
    Timecode start_tc{.rate = FrameRate::Rate_30_NDF};

    // Only 1 frame to generate, small buffer to complete quickly
    FixedPlaybackState state(&gen, start_tc, 1);
    state.frame_buffer.reserve(gen.samples_per_frame());

    // Create mock output buffer larger than one frame
    std::vector<float> output_data(gen.samples_per_frame() + 100, 0.0f);
    statusbar::audio::OutputAudioBuffer output_buf{std::span<float>(output_data)};
    std::array<statusbar::audio::OutputAudioBufferFloat, 1> output_bufs{output_buf};
    statusbar::audio::AudioCallbackParamsFloat params{
        .input_buffers = {}, .output_buffers = std::span(output_bufs), .stream_time = 0.0};

    // Process should complete and set finished flag
    auto result = process_fixed_callback(state, params);

    // Either this call or a subsequent one should complete
    while (!state.is_finished() && result.has_value()) {
        result = process_fixed_callback(state, params);
    }

    EXPECT_TRUE(state.is_finished());
    EXPECT_EQ(state.frames_generated, 1);
}

TEST(playback, fixed_callback_timecode_advancement)
{
    Generator gen(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);
    Timecode start_tc{.hours = 0, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF};

    FixedPlaybackState state(&gen, start_tc, 10);
    state.frame_buffer.reserve(gen.samples_per_frame());

    // Create output buffer
    std::vector<float> output_data(gen.samples_per_frame() * 3, 0.0f);
    statusbar::audio::OutputAudioBufferFloat output_buf{std::span<float>(output_data)};
    std::array<statusbar::audio::OutputAudioBufferFloat, 1> output_bufs{output_buf};
    statusbar::audio::AudioCallbackParamsFloat params{
        .input_buffers = {}, .output_buffers = std::span(output_bufs), .stream_time = 0.0};

    // Process a few callbacks
    (void)process_fixed_callback(state, params);
    (void)process_fixed_callback(state, params);

    // Timecode should have advanced
    EXPECT_TRUE(state.frames_generated > 0);
    EXPECT_TRUE(state.current_tc.frames > 0 || state.current_tc.seconds > 0);
}

TEST(playback, fixed_callback_output_in_range)
{
    Generator gen(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);
    Timecode start_tc{.rate = FrameRate::Rate_30_NDF};

    FixedPlaybackState state(&gen, start_tc, 5);
    state.frame_buffer.reserve(gen.samples_per_frame());

    std::vector<float> output_data(1024, 0.0f);
    statusbar::audio::OutputAudioBufferFloat output_buf{std::span<float>(output_data)};
    std::array<statusbar::audio::OutputAudioBufferFloat, 1> output_bufs{output_buf};
    statusbar::audio::AudioCallbackParamsFloat params{
        .input_buffers = {}, .output_buffers = std::span(output_bufs), .stream_time = 0.0};

    (void)process_fixed_callback(state, params);

    // All samples should be in valid range
    for (float sample : output_data) {
        EXPECT_TRUE(sample >= -1.0f && sample <= 1.0f);
    }
}

// Servo playback callback tests

TEST(playback, servo_callback_basic)
{
    ServoGenerator servo(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);
    Timecode start_tc{.hours = 1, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF};
    servo.set_timecode(start_tc);

    auto time_info = get_local_time_info();
    ServoPlaybackState state(&servo, 48000, FrameRate::Rate_30_NDF, time_info.epoch_seconds);
    state.sample_buffer.reserve(512);

    // Create mock output buffer
    std::vector<float> output_data(256, 0.0f);
    statusbar::audio::OutputAudioBufferFloat output_buf{std::span<float>(output_data)};
    std::array<statusbar::audio::OutputAudioBufferFloat, 1> output_bufs{output_buf};
    statusbar::audio::AudioCallbackParamsFloat params{
        .input_buffers = {}, .output_buffers = std::span(output_bufs), .stream_time = 0.0};

    auto result = process_servo_callback(state, time_info.local_time, time_info.millis, time_info.epoch_seconds, params);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(state.total_samples_generated, 256);
}

TEST(playback, servo_callback_stopped)
{
    ServoGenerator servo(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);

    auto time_info = get_local_time_info();
    ServoPlaybackState state(&servo, 48000, FrameRate::Rate_30_NDF, time_info.epoch_seconds);
    state.set_running(false);  // Stop playback

    std::vector<float> output_data(256, 0.0f);
    statusbar::audio::OutputAudioBufferFloat output_buf{std::span<float>(output_data)};
    std::array<statusbar::audio::OutputAudioBufferFloat, 1> output_bufs{output_buf};
    statusbar::audio::AudioCallbackParamsFloat params{
        .input_buffers = {}, .output_buffers = std::span(output_bufs), .stream_time = 0.0};

    // Should return failure when not running
    auto result = process_servo_callback(state, time_info.local_time, time_info.millis, time_info.epoch_seconds, params);
    EXPECT_FALSE(result.has_value());
}

TEST(playback, servo_callback_output_in_range)
{
    ServoGenerator servo(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);
    Timecode start_tc{.rate = FrameRate::Rate_30_NDF};
    servo.set_timecode(start_tc);

    auto time_info = get_local_time_info();
    ServoPlaybackState state(&servo, 48000, FrameRate::Rate_30_NDF, time_info.epoch_seconds);
    state.sample_buffer.reserve(512);

    std::vector<float> output_data(512, 0.0f);
    statusbar::audio::OutputAudioBufferFloat output_buf{std::span<float>(output_data)};
    std::array<statusbar::audio::OutputAudioBufferFloat, 1> output_bufs{output_buf};
    statusbar::audio::AudioCallbackParamsFloat params{
        .input_buffers = {}, .output_buffers = std::span(output_bufs), .stream_time = 0.0};

    (void)process_servo_callback(state, time_info.local_time, time_info.millis, time_info.epoch_seconds, params);

    // All samples should be in valid range
    for (float sample : output_data) {
        EXPECT_TRUE(sample >= -1.0f && sample <= 1.0f);
    }
}

TEST(playback, servo_callback_with_offset)
{
    ServoGenerator servo(Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF);

    auto time_info = get_local_time_info();
    int32_t offset = 9 * 3600;  // 9 AM offset
    ServoPlaybackState state(&servo, 48000, FrameRate::Rate_30_NDF, time_info.epoch_seconds, offset);
    state.sample_buffer.reserve(512);

    std::vector<float> output_data(256, 0.0f);
    statusbar::audio::OutputAudioBufferFloat output_buf{std::span<float>(output_data)};
    std::array<statusbar::audio::OutputAudioBufferFloat, 1> output_bufs{output_buf};
    statusbar::audio::AudioCallbackParamsFloat params{
        .input_buffers = {}, .output_buffers = std::span(output_bufs), .stream_time = 0.0};

    // Should work with time offset
    auto result = process_servo_callback(state, time_info.local_time, time_info.millis, time_info.epoch_seconds, params);
    EXPECT_TRUE(result.has_value());
}

// Frame rate helper tests

TEST(framerate, frames_per_second)
{
    EXPECT_EQ(frames_per_second(FrameRate::Rate_23_976), 24);
    EXPECT_EQ(frames_per_second(FrameRate::Rate_24), 24);
    EXPECT_EQ(frames_per_second(FrameRate::Rate_25), 25);
    EXPECT_EQ(frames_per_second(FrameRate::Rate_29_97_DF), 30);
    EXPECT_EQ(frames_per_second(FrameRate::Rate_29_97_NDF), 30);
    EXPECT_EQ(frames_per_second(FrameRate::Rate_30_DF), 30);
    EXPECT_EQ(frames_per_second(FrameRate::Rate_30_NDF), 30);
}

TEST(framerate, is_drop_frame)
{
    EXPECT_FALSE(is_drop_frame(FrameRate::Rate_23_976));
    EXPECT_FALSE(is_drop_frame(FrameRate::Rate_24));
    EXPECT_FALSE(is_drop_frame(FrameRate::Rate_25));
    EXPECT_TRUE(is_drop_frame(FrameRate::Rate_29_97_DF));
    EXPECT_FALSE(is_drop_frame(FrameRate::Rate_29_97_NDF));
    EXPECT_TRUE(is_drop_frame(FrameRate::Rate_30_DF));
    EXPECT_FALSE(is_drop_frame(FrameRate::Rate_30_NDF));
}

TEST(framerate, actual_frame_rate)
{
    // Check approximate values (floating point comparison)
    double rate_23_976 = actual_frame_rate(FrameRate::Rate_23_976);
    EXPECT_TRUE(rate_23_976 > 23.97 && rate_23_976 < 23.98);

    double rate_29_97 = actual_frame_rate(FrameRate::Rate_29_97_DF);
    EXPECT_TRUE(rate_29_97 > 29.96 && rate_29_97 < 29.98);

    EXPECT_EQ(actual_frame_rate(FrameRate::Rate_24), 24.0);
    EXPECT_EQ(actual_frame_rate(FrameRate::Rate_25), 25.0);
    EXPECT_EQ(actual_frame_rate(FrameRate::Rate_30_NDF), 30.0);
}

TEST(framerate, parse_frame_rate)
{
    // Valid frame rate strings
    auto result = parse_frame_rate("23.976");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, FrameRate::Rate_23_976);

    result = parse_frame_rate("24");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, FrameRate::Rate_24);

    result = parse_frame_rate("25");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, FrameRate::Rate_25);

    result = parse_frame_rate("29.97df");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, FrameRate::Rate_29_97_DF);

    result = parse_frame_rate("29.97DF");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, FrameRate::Rate_29_97_DF);

    result = parse_frame_rate("29.97");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, FrameRate::Rate_29_97_NDF);

    result = parse_frame_rate("29.97ndf");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, FrameRate::Rate_29_97_NDF);

    result = parse_frame_rate("30df");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, FrameRate::Rate_30_DF);

    result = parse_frame_rate("30");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, FrameRate::Rate_30_NDF);

    result = parse_frame_rate("30ndf");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, FrameRate::Rate_30_NDF);

    // Invalid frame rate strings
    EXPECT_FALSE(parse_frame_rate("").has_value());
    EXPECT_FALSE(parse_frame_rate("invalid").has_value());
    EXPECT_FALSE(parse_frame_rate("60").has_value());
    EXPECT_FALSE(parse_frame_rate("29.97 df").has_value());
}

TEST(framerate, frame_rate_name)
{
    EXPECT_EQ(frame_rate_name(FrameRate::Rate_23_976), "23.976 fps");
    EXPECT_EQ(frame_rate_name(FrameRate::Rate_24), "24 fps");
    EXPECT_EQ(frame_rate_name(FrameRate::Rate_25), "25 fps");
    EXPECT_EQ(frame_rate_name(FrameRate::Rate_29_97_DF), "29.97 fps DF");
    EXPECT_EQ(frame_rate_name(FrameRate::Rate_29_97_NDF), "29.97 fps NDF");
    EXPECT_EQ(frame_rate_name(FrameRate::Rate_30_DF), "30 fps DF");
    EXPECT_EQ(frame_rate_name(FrameRate::Rate_30_NDF), "30 fps NDF");
}

// Edge case tests

TEST(timecode, increment_24hr_rollover)
{
    // Test 24-hour rollover: 23:59:59:29 -> 00:00:00:00
    Timecode tc{.hours = 23, .minutes = 59, .seconds = 59, .frames = 29, .rate = FrameRate::Rate_30_NDF};
    EXPECT_TRUE(tc.is_valid());

    tc.increment_frame();
    EXPECT_EQ(tc.hours, 0);
    EXPECT_EQ(tc.minutes, 0);
    EXPECT_EQ(tc.seconds, 0);
    EXPECT_EQ(tc.frames, 0);
    EXPECT_TRUE(tc.is_valid());
}

TEST(timecode, decrement_24hr_rollover)
{
    // Test 24-hour rollover: 00:00:00:00 -> 23:59:59:29
    Timecode tc{.hours = 0, .minutes = 0, .seconds = 0, .frames = 0, .rate = FrameRate::Rate_30_NDF};
    EXPECT_TRUE(tc.is_valid());

    tc.decrement_frame();
    EXPECT_EQ(tc.hours, 23);
    EXPECT_EQ(tc.minutes, 59);
    EXPECT_EQ(tc.seconds, 59);
    EXPECT_EQ(tc.frames, 29);
    EXPECT_TRUE(tc.is_valid());
}

TEST(timecode, drop_frame_30fps)
{
    // Test 30 fps drop frame (not 29.97)
    Timecode tc{.hours = 1, .minutes = 0, .seconds = 59, .frames = 29, .rate = FrameRate::Rate_30_DF};
    EXPECT_TRUE(tc.is_valid());

    // Increment should skip frames 0 and 1 at minute 1
    tc.increment_frame();
    EXPECT_EQ(tc.hours, 1);
    EXPECT_EQ(tc.minutes, 1);
    EXPECT_EQ(tc.seconds, 0);
    EXPECT_EQ(tc.frames, 2);  // Skipped 0 and 1
    EXPECT_TRUE(tc.is_valid());

    // Decrement should go back to 59:29
    tc.decrement_frame();
    EXPECT_EQ(tc.minutes, 0);
    EXPECT_EQ(tc.seconds, 59);
    EXPECT_EQ(tc.frames, 29);
    EXPECT_TRUE(tc.is_valid());
}

TEST(timecode, from_string_valid_then_invalid)
{
    // Valid parse but invalid timecode (frame 30 in 30fps)
    auto result = Timecode::from_string("01:02:03:30", FrameRate::Rate_30_NDF);
    EXPECT_TRUE(result.has_value());   // Parse succeeds
    EXPECT_FALSE(result->is_valid());  // But timecode is invalid

    // Valid parse but invalid timecode (frame 0 at minute 1 in drop frame)
    result = Timecode::from_string("01:01:00:00", FrameRate::Rate_29_97_DF);
    EXPECT_TRUE(result.has_value());   // Parse succeeds
    EXPECT_FALSE(result->is_valid());  // But timecode is invalid (dropped frame)

    // Valid parse but invalid hours
    result = Timecode::from_string("25:00:00:00", FrameRate::Rate_30_NDF);
    EXPECT_TRUE(result.has_value());   // Parse succeeds
    EXPECT_FALSE(result->is_valid());  // But timecode is invalid
}

TEST(timecode, frame_count_roundtrip_all_rates)
{
    // Test round-trip conversion for all frame rates
    FrameRate rates[] = {
        FrameRate::Rate_23_976,
        FrameRate::Rate_24,
        FrameRate::Rate_25,
        FrameRate::Rate_29_97_DF,
        FrameRate::Rate_29_97_NDF,
        FrameRate::Rate_30_DF,
        FrameRate::Rate_30_NDF};

    for (auto rate : rates) {
        // Test a few timecodes at this rate
        Timecode tc1{.hours = 1, .minutes = 30, .seconds = 45, .frames = 10, .rate = rate};

        // For drop frame rates at dropped positions, adjust to valid frame
        if (is_drop_frame(rate) && tc1.seconds == 0 && (tc1.minutes % 10) != 0 && tc1.frames < 2) {
            tc1.frames = 2;
        }

        if (tc1.frames >= frames_per_second(rate)) {
            tc1.frames = frames_per_second(rate) - 1;
        }

        uint32_t count = tc1.to_frame_count();
        Timecode tc2 = Timecode::from_frame_count(count, rate);

        EXPECT_EQ(tc2.hours, tc1.hours);
        EXPECT_EQ(tc2.minutes, tc1.minutes);
        EXPECT_EQ(tc2.seconds, tc1.seconds);
        EXPECT_EQ(tc2.frames, tc1.frames);
    }
}

TEST(frame, drop_frame_flag_30fps)
{
    // Test 30 fps drop frame flag (bit 10)
    Timecode tc_30df{.rate = FrameRate::Rate_30_DF};
    LTCFrame frame_30df(tc_30df);
    EXPECT_TRUE(frame_30df.get_bit(10));

    Timecode tc_30ndf{.rate = FrameRate::Rate_30_NDF};
    LTCFrame frame_30ndf(tc_30ndf);
    EXPECT_FALSE(frame_30ndf.get_bit(10));
}

TEST(generator, all_sample_rates)
{
    // Test all sample rates work with all frame rates
    Generator::SampleRate sample_rates[] = {
        Generator::SampleRate::Rate_44100, Generator::SampleRate::Rate_48000, Generator::SampleRate::Rate_96000};

    FrameRate frame_rates[] = {
        FrameRate::Rate_23_976, FrameRate::Rate_24, FrameRate::Rate_25, FrameRate::Rate_29_97_DF, FrameRate::Rate_30_NDF};

    for (auto sr : sample_rates) {
        for (auto fr : frame_rates) {
            Generator gen(sr, fr);
            Timecode tc{.rate = fr};

            std::vector<float> samples;
            samples.reserve(gen.samples_per_frame());
            gen.generate_frame(tc, samples);

            // Should generate correct number of samples
            EXPECT_EQ(samples.size(), gen.samples_per_frame());

            // All samples should be in valid range
            for (float sample : samples) {
                EXPECT_TRUE(sample >= -1.0f && sample <= 1.0f);
            }
        }
    }
}

// Main test runner function required by create_test_sourcelist
TEST_MAIN(statusbar_ltc, ltc_test)