// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// AudioPresentationQueue tests

#include "statusbar/audio/audio.hpp"
#include "statusbar/dsp/dsp.hpp"
#include "statusbar/status/status.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <span>
#include <vector>

using namespace statusbar::audio;
using namespace statusbar::dsp;

// Test group: presentation_queue_construct
TEST(presentation_queue_construct, direct_construction)
{
    AudioPresentationQueue<float> queue(2, 1024);

    EXPECT_EQ(queue.channels(), 2);
    EXPECT_EQ(queue.capacity(), 1024);
    EXPECT_EQ(queue.center_time(), 0);
}

TEST(presentation_queue_construct, default_then_init)
{
    AudioPresentationQueue<float> queue;
    queue.init(4, 512);

    EXPECT_EQ(queue.channels(), 4);
    EXPECT_EQ(queue.capacity(), 512);
    EXPECT_EQ(queue.center_time(), 0);
}

TEST(presentation_queue_construct, reinit)
{
    AudioPresentationQueue<float> queue(2, 256);
    queue.init(8, 2048);

    EXPECT_EQ(queue.channels(), 8);
    EXPECT_EQ(queue.capacity(), 2048);
}

// Test group: presentation_queue_time_range
TEST(presentation_queue_time_range, initial_range)
{
    AudioPresentationQueue<float> queue(2, 1000);

    EXPECT_EQ(queue.earliest_valid_time(), 0);
    EXPECT_EQ(queue.latest_valid_time(), 999);
}

TEST(presentation_queue_time_range, after_set_center_time)
{
    AudioPresentationQueue<float> queue(2, 1000);
    queue.set_center_time(5000);

    EXPECT_EQ(queue.center_time(), 5000);
    EXPECT_EQ(queue.earliest_valid_time(), 5000);
    EXPECT_EQ(queue.latest_valid_time(), 5999);
}

// Test group: presentation_queue_write_read
TEST(presentation_queue_write_read, write_at_center)
{
    AudioPresentationQueue<float> queue(2, 100);

    // Write samples at center_time (0)
    std::array<float, 4> ch0_data = {1.0f, 2.0f, 3.0f, 4.0f};
    std::array<float, 4> ch1_data = {5.0f, 6.0f, 7.0f, 8.0f};

    std::array<InputAudioBuffer<float>, 2> input = {
        InputAudioBuffer<float>{.sample = ch0_data}, InputAudioBuffer<float>{.sample = ch1_data}};

    size_t written = queue.write(0, input);
    EXPECT_EQ(written, 4);

    // Read them back
    std::array<float, 4> out0 = {};
    std::array<float, 4> out1 = {};
    std::array<OutputAudioBuffer<float>, 2> output = {
        OutputAudioBuffer<float>{.sample = out0}, OutputAudioBuffer<float>{.sample = out1}};

    auto const read_result = queue.read(output);
    EXPECT_EQ(read_result.frames_read, 4);
    EXPECT_EQ(read_result.frames_with_data, 4);
    EXPECT_EQ(read_result.frames_dropped_racing, 0);

    EXPECT_EQ(out0[0], 1.0f);
    EXPECT_EQ(out0[1], 2.0f);
    EXPECT_EQ(out0[2], 3.0f);
    EXPECT_EQ(out0[3], 4.0f);

    EXPECT_EQ(out1[0], 5.0f);
    EXPECT_EQ(out1[1], 6.0f);
    EXPECT_EQ(out1[2], 7.0f);
    EXPECT_EQ(out1[3], 8.0f);

    // Center time should have advanced
    EXPECT_EQ(queue.center_time(), 4);
}

TEST(presentation_queue_write_read, write_at_future_time)
{
    AudioPresentationQueue<float> queue(1, 100);

    // Write samples at time 50
    std::array<float, 3> ch0_data = {10.0f, 20.0f, 30.0f};
    std::array<InputAudioBuffer<float>, 1> input = {InputAudioBuffer<float>{.sample = ch0_data}};

    size_t written = queue.write(50, input);
    EXPECT_EQ(written, 3);

    // Read first 50 samples (should be zeros)
    std::array<float, 50> out_zeros = {};
    std::array<OutputAudioBuffer<float>, 1> output_zeros = {OutputAudioBuffer<float>{.sample = out_zeros}};

    queue.read(output_zeros);
    for (size_t i = 0; i < 50; ++i) {
        EXPECT_EQ(out_zeros[i], 0.0f);
    }

    // Now read the written samples
    std::array<float, 3> out_data = {};
    std::array<OutputAudioBuffer<float>, 1> output_data = {OutputAudioBuffer<float>{.sample = out_data}};

    queue.read(output_data);
    EXPECT_EQ(out_data[0], 10.0f);
    EXPECT_EQ(out_data[1], 20.0f);
    EXPECT_EQ(out_data[2], 30.0f);
}

TEST(presentation_queue_write_read, write_sample_single)
{
    AudioPresentationQueue<float> queue(2, 100);

    // First write to slot 10 succeeds; slot transitions Empty -> Full.
    EXPECT_TRUE(queue.write_sample(10, 0, 42.0f));

    // Second write to the same slot fails — the per-frame lock is held
    // (Full state). Use write() to set multiple channels in one frame.
    EXPECT_FALSE(queue.write_sample(10, 1, 99.0f));

    // Channel 0 has the written value; channel 1 stays at its prior state
    // (zero from initial construction).
    EXPECT_EQ(queue.read_sample(10, 0), 42.0f);
    EXPECT_EQ(queue.read_sample(10, 1), 0.0f);
}

TEST(presentation_queue_write_read, write_sample_different_times)
{
    AudioPresentationQueue<float> queue(2, 100);

    // Writes to different time slots are independent.
    EXPECT_TRUE(queue.write_sample(10, 0, 42.0f));
    EXPECT_TRUE(queue.write_sample(11, 1, 99.0f));

    EXPECT_EQ(queue.read_sample(10, 0), 42.0f);
    EXPECT_EQ(queue.read_sample(11, 1), 99.0f);
}

// Test group: presentation_queue_out_of_order
TEST(presentation_queue_out_of_order, write_out_of_order)
{
    AudioPresentationQueue<float> queue(1, 100);

    // Write at t+10, then t+5, then t+2 (out of order)
    EXPECT_TRUE(queue.write_sample(10, 0, 100.0f));
    EXPECT_TRUE(queue.write_sample(5, 0, 50.0f));
    EXPECT_TRUE(queue.write_sample(2, 0, 20.0f));

    // Read in order - should get zeros, then data
    EXPECT_EQ(queue.read_sample(0, 0), 0.0f);
    EXPECT_EQ(queue.read_sample(1, 0), 0.0f);
    EXPECT_EQ(queue.read_sample(2, 0), 20.0f);
    EXPECT_EQ(queue.read_sample(3, 0), 0.0f);
    EXPECT_EQ(queue.read_sample(4, 0), 0.0f);
    EXPECT_EQ(queue.read_sample(5, 0), 50.0f);
    EXPECT_EQ(queue.read_sample(10, 0), 100.0f);
}

// Test group: presentation_queue_boundary
TEST(presentation_queue_boundary, write_at_exact_center)
{
    AudioPresentationQueue<float> queue(1, 100);

    // Writing at center_time (0) should work
    EXPECT_TRUE(queue.write_sample(0, 0, 1.0f));
    EXPECT_EQ(queue.read_sample(0, 0), 1.0f);
}

TEST(presentation_queue_boundary, write_in_past_rejected)
{
    AudioPresentationQueue<float> queue(1, 100);
    queue.set_center_time(50);

    // Writing at time 49 (in the past) should fail
    EXPECT_FALSE(queue.write_sample(49, 0, 1.0f));

    // Writing at time 50 (current center) should work
    EXPECT_TRUE(queue.write_sample(50, 0, 2.0f));
}

TEST(presentation_queue_boundary, write_too_far_ahead_rejected)
{
    AudioPresentationQueue<float> queue(1, 100);

    // Writing at capacity (100) should fail (valid range is 0-99)
    EXPECT_FALSE(queue.write_sample(100, 0, 1.0f));

    // Writing at capacity-1 (99) should work
    EXPECT_TRUE(queue.write_sample(99, 0, 2.0f));
}

TEST(presentation_queue_boundary, can_write_check)
{
    AudioPresentationQueue<float> queue(1, 100);

    EXPECT_TRUE(queue.can_write(0, 100));   // Exactly fits
    EXPECT_FALSE(queue.can_write(0, 101));  // One too many
    EXPECT_TRUE(queue.can_write(50, 50));   // Partial fit
    EXPECT_FALSE(queue.can_write(50, 51));  // Exceeds by one
}

// Test group: presentation_queue_gaps
TEST(presentation_queue_gaps, sparse_samples_zeros)
{
    AudioPresentationQueue<float> queue(1, 100);

    // Write only at positions 0, 5, 10
    queue.write_sample(0, 0, 1.0f);
    queue.write_sample(5, 0, 5.0f);
    queue.write_sample(10, 0, 10.0f);

    // Read 11 samples - gaps should be zeros
    std::array<float, 11> out = {};
    std::array<OutputAudioBuffer<float>, 1> output = {OutputAudioBuffer<float>{.sample = out}};

    queue.read(output);

    EXPECT_EQ(out[0], 1.0f);
    EXPECT_EQ(out[1], 0.0f);
    EXPECT_EQ(out[2], 0.0f);
    EXPECT_EQ(out[3], 0.0f);
    EXPECT_EQ(out[4], 0.0f);
    EXPECT_EQ(out[5], 5.0f);
    EXPECT_EQ(out[6], 0.0f);
    EXPECT_EQ(out[7], 0.0f);
    EXPECT_EQ(out[8], 0.0f);
    EXPECT_EQ(out[9], 0.0f);
    EXPECT_EQ(out[10], 10.0f);
}

// Test group: presentation_queue_advance
TEST(presentation_queue_advance, advance_zeros_samples)
{
    AudioPresentationQueue<float> queue(1, 100);

    // Write some data
    queue.write_sample(0, 0, 1.0f);
    queue.write_sample(1, 0, 2.0f);
    queue.write_sample(2, 0, 3.0f);

    // Advance by 2 (skips samples 0 and 1)
    queue.advance(2);

    EXPECT_EQ(queue.center_time(), 2);

    // Sample at new position 2 (old position 2) should still be there
    EXPECT_EQ(queue.read_sample(2, 0), 3.0f);

    // Old positions 0 and 1 are now out of range (in the past)
    EXPECT_EQ(queue.read_sample(0, 0), 0.0f);  // Out of range returns 0
    EXPECT_EQ(queue.read_sample(1, 0), 0.0f);  // Out of range returns 0
}

TEST(presentation_queue_advance, set_center_time_zeros_buffer)
{
    AudioPresentationQueue<float> queue(1, 100);

    // Write some data
    queue.write_sample(0, 0, 1.0f);
    queue.write_sample(50, 0, 50.0f);

    // Jump to time 1000
    queue.set_center_time(1000);

    EXPECT_EQ(queue.center_time(), 1000);
    EXPECT_EQ(queue.earliest_valid_time(), 1000);
    EXPECT_EQ(queue.latest_valid_time(), 1099);

    // Old data should be cleared (can write to same index positions)
    queue.write_sample(1000, 0, 999.0f);
    EXPECT_EQ(queue.read_sample(1000, 0), 999.0f);
}

// Test group: presentation_queue_multichannel
TEST(presentation_queue_multichannel, channel_isolation)
{
    AudioPresentationQueue<float> queue(4, 100);

    // Per-frame ownership means write_sample takes the whole frame lock,
    // so the multi-channel pattern is write() (one call sets all channels
    // of the frame), not consecutive write_sample calls.
    std::array<float, 1> c0{100.0f};
    std::array<float, 1> c1{200.0f};
    std::array<float, 1> c2{300.0f};
    std::array<float, 1> c3{400.0f};
    std::array<InputAudioBuffer<float>, 4> input = {
        InputAudioBuffer<float>{.sample = c0},
        InputAudioBuffer<float>{.sample = c1},
        InputAudioBuffer<float>{.sample = c2},
        InputAudioBuffer<float>{.sample = c3}};
    EXPECT_EQ(queue.write(10, input), 1);

    EXPECT_EQ(queue.read_sample(10, 0), 100.0f);
    EXPECT_EQ(queue.read_sample(10, 1), 200.0f);
    EXPECT_EQ(queue.read_sample(10, 2), 300.0f);
    EXPECT_EQ(queue.read_sample(10, 3), 400.0f);
}

TEST(presentation_queue_multichannel, invalid_channel)
{
    AudioPresentationQueue<float> queue(2, 100);

    // Writing to invalid channel should fail
    EXPECT_FALSE(queue.write_sample(0, 2, 1.0f));
    EXPECT_FALSE(queue.write_sample(0, 100, 1.0f));

    // Reading from invalid channel should return zero
    EXPECT_EQ(queue.read_sample(0, 2), 0.0f);
    EXPECT_EQ(queue.read_sample(0, 100), 0.0f);
}

// Test group: presentation_queue_zero_on_read
TEST(presentation_queue_zero_on_read, samples_zeroed_after_read)
{
    AudioPresentationQueue<float> queue(1, 100);

    queue.write_sample(0, 0, 42.0f);

    // Peek should not zero
    EXPECT_EQ(queue.read_sample(0, 0), 42.0f);

    // Read advances and zeros
    std::array<float, 1> out = {};
    std::array<OutputAudioBuffer<float>, 1> output = {OutputAudioBuffer<float>{.sample = out}};
    queue.read(output);

    EXPECT_EQ(out[0], 42.0f);

    // Write same value at new position (old index 0, which is now time 100)
    // After center_time advances by 1, new valid range is [1, 100]
    // But index 0 corresponds to times 0, 100, 200, etc.
    // Since center is now 1, time 100 maps to index 0
    queue.write_sample(100, 0, 123.0f);

    // Advance to time 100 and read - should get the new value
    queue.advance(99);  // Now at time 100
    EXPECT_EQ(queue.read_sample(100, 0), 123.0f);
}

TEST(presentation_queue_zero_on_read, re_read_returns_zeros)
{
    AudioPresentationQueue<float> queue(1, 100);

    // Write and read
    queue.write_sample(0, 0, 99.0f);

    std::array<float, 1> out1 = {};
    std::array<OutputAudioBuffer<float>, 1> output1 = {OutputAudioBuffer<float>{.sample = out1}};
    queue.read(output1);
    EXPECT_EQ(out1[0], 99.0f);

    // Write at same index (now time 100, which maps to index 0)
    // But don't write anything - just read from next position
    std::array<float, 1> out2 = {};
    std::array<OutputAudioBuffer<float>, 1> output2 = {OutputAudioBuffer<float>{.sample = out2}};
    queue.read(output2);

    // Should be zero (nothing was written at time 1)
    EXPECT_EQ(out2[0], 0.0f);
}

// Test group: presentation_queue_peek
TEST(presentation_queue_peek, peek_does_not_advance)
{
    AudioPresentationQueue<float> queue(1, 100);

    queue.write_sample(0, 0, 1.0f);
    queue.write_sample(1, 0, 2.0f);

    // Peek
    std::array<float, 2> out = {};
    std::array<OutputAudioBuffer<float>, 1> output = {OutputAudioBuffer<float>{.sample = out}};
    auto const peeked = queue.peek(output);

    EXPECT_EQ(peeked.frames_read, 2);
    EXPECT_EQ(peeked.frames_with_data, 2);
    EXPECT_EQ(out[0], 1.0f);
    EXPECT_EQ(out[1], 2.0f);

    // Center time unchanged
    EXPECT_EQ(queue.center_time(), 0);

    // Data still there
    EXPECT_EQ(queue.read_sample(0, 0), 1.0f);
    EXPECT_EQ(queue.read_sample(1, 0), 2.0f);
}

// Test group: presentation_queue_clear_reset
TEST(presentation_queue_clear_reset, clear_zeros_keeps_time)
{
    AudioPresentationQueue<float> queue(1, 100);
    queue.set_center_time(500);
    queue.write_sample(500, 0, 1.0f);

    queue.clear();

    EXPECT_EQ(queue.center_time(), 500);         // Time unchanged
    EXPECT_EQ(queue.read_sample(500, 0), 0.0f);  // Data cleared
}

TEST(presentation_queue_clear_reset, reset_zeros_and_resets_time)
{
    AudioPresentationQueue<float> queue(1, 100);
    queue.set_center_time(500);
    queue.write_sample(500, 0, 1.0f);

    queue.reset();

    EXPECT_EQ(queue.center_time(), 0);         // Time reset
    EXPECT_EQ(queue.read_sample(0, 0), 0.0f);  // Data cleared
}

// Test group: presentation_queue_empty_ops
TEST(presentation_queue_empty_ops, empty_write)
{
    AudioPresentationQueue<float> queue(1, 100);

    std::array<InputAudioBuffer<float>, 0> empty_input = {};
    size_t written = queue.write(0, empty_input);
    EXPECT_EQ(written, 0);
}

TEST(presentation_queue_empty_ops, empty_read)
{
    AudioPresentationQueue<float> queue(1, 100);

    std::array<OutputAudioBuffer<float>, 0> empty_output = {};
    auto const read_result = queue.read(empty_output);
    EXPECT_EQ(read_result.frames_read, 0);
    EXPECT_EQ(read_result.frames_with_data, 0);
}

// Test group: presentation_queue_simd
// Tests for AudioPresentationQueue with SIMD types (simd_float32x4)

TEST(presentation_queue_simd, construct_simd_queue)
{
    AudioPresentationQueue<simd_float32x4> queue(2, 100);

    EXPECT_EQ(queue.channels(), 2);
    EXPECT_EQ(queue.capacity(), 100);
    EXPECT_EQ(queue.center_time(), 0);
}

TEST(presentation_queue_simd, write_read_simd)
{
    AudioPresentationQueue<simd_float32x4> queue(1, 100);

    // Create SIMD samples
    simd_float32x4 sample1{1.0f, 2.0f, 3.0f, 4.0f};
    simd_float32x4 sample2{5.0f, 6.0f, 7.0f, 8.0f};

    // Write using write_sample
    queue.write_sample(0, 0, sample1);
    queue.write_sample(1, 0, sample2);

    // Read back
    auto read1 = queue.read_sample(0, 0);
    auto read2 = queue.read_sample(1, 0);

    // Verify all lanes
    EXPECT_EQ(read1[0], 1.0f);
    EXPECT_EQ(read1[1], 2.0f);
    EXPECT_EQ(read1[2], 3.0f);
    EXPECT_EQ(read1[3], 4.0f);

    EXPECT_EQ(read2[0], 5.0f);
    EXPECT_EQ(read2[1], 6.0f);
    EXPECT_EQ(read2[2], 7.0f);
    EXPECT_EQ(read2[3], 8.0f);
}

TEST(presentation_queue_simd, zero_on_read_simd)
{
    AudioPresentationQueue<simd_float32x4> queue(1, 100);

    simd_float32x4 sample{10.0f, 20.0f, 30.0f, 40.0f};
    queue.write_sample(0, 0, sample);

    // Advance past the sample (should zero it)
    queue.advance(1);

    // The sample at index 0 should now be zeroed
    // Write at time 100 which maps to same index
    auto read_after = queue.read_sample(100, 0);

    // All lanes should be zero (properly zeroed by advance)
    EXPECT_EQ(read_after[0], 0.0f);
    EXPECT_EQ(read_after[1], 0.0f);
    EXPECT_EQ(read_after[2], 0.0f);
    EXPECT_EQ(read_after[3], 0.0f);
}

TEST(presentation_queue_simd, clear_zeros_simd)
{
    AudioPresentationQueue<simd_float32x4> queue(1, 100);

    simd_float32x4 sample{99.0f, 88.0f, 77.0f, 66.0f};
    queue.write_sample(50, 0, sample);

    queue.clear();

    auto read_after = queue.read_sample(50, 0);

    // All lanes should be zero after clear
    EXPECT_EQ(read_after[0], 0.0f);
    EXPECT_EQ(read_after[1], 0.0f);
    EXPECT_EQ(read_after[2], 0.0f);
    EXPECT_EQ(read_after[3], 0.0f);
}

TEST(presentation_queue_simd, unwritten_is_zero_simd)
{
    AudioPresentationQueue<simd_float32x4> queue(1, 100);

    // Read unwritten sample - should be zero
    auto unwritten = queue.read_sample(50, 0);

    EXPECT_EQ(unwritten[0], 0.0f);
    EXPECT_EQ(unwritten[1], 0.0f);
    EXPECT_EQ(unwritten[2], 0.0f);
    EXPECT_EQ(unwritten[3], 0.0f);
}

// Test group: presentation_queue_read_result
// Tests for the ReadResult breakdown (frames_with_data vs frames_dropped_racing
// vs never-written silence)

TEST(presentation_queue_read_result, fresh_queue_reports_no_data)
{
    AudioPresentationQueue<float> queue(1, 100);

    std::array<float, 8> out{};
    std::array<OutputAudioBuffer<float>, 1> output = {OutputAudioBuffer<float>{.sample = out}};

    auto const r = queue.read(output);
    EXPECT_EQ(r.frames_read, 8);
    EXPECT_EQ(r.frames_with_data, 0);
    EXPECT_EQ(r.frames_dropped_racing, 0);
    for (auto v : out) {
        EXPECT_EQ(v, 0.0f);
    }
}

TEST(presentation_queue_read_result, partial_write_split_data_and_silence)
{
    AudioPresentationQueue<float> queue(1, 100);

    // Write 3 frames starting at time 2 (slots 2,3,4 Full; slots 0,1,5..7 Empty)
    std::array<float, 3> in{10.0f, 20.0f, 30.0f};
    std::array<InputAudioBuffer<float>, 1> input = {InputAudioBuffer<float>{.sample = in}};
    EXPECT_EQ(queue.write(2, input), 3);

    // Read 8 frames — expect 3 with data, 5 silent (never written)
    std::array<float, 8> out{};
    std::array<OutputAudioBuffer<float>, 1> output = {OutputAudioBuffer<float>{.sample = out}};
    auto const r = queue.read(output);
    EXPECT_EQ(r.frames_read, 8);
    EXPECT_EQ(r.frames_with_data, 3);
    EXPECT_EQ(r.frames_dropped_racing, 0);
    EXPECT_EQ(out[0], 0.0f);
    EXPECT_EQ(out[1], 0.0f);
    EXPECT_EQ(out[2], 10.0f);
    EXPECT_EQ(out[3], 20.0f);
    EXPECT_EQ(out[4], 30.0f);
    EXPECT_EQ(out[5], 0.0f);
    EXPECT_EQ(out[6], 0.0f);
    EXPECT_EQ(out[7], 0.0f);
}

TEST(presentation_queue_read_result, double_write_to_full_slot_is_rejected)
{
    AudioPresentationQueue<float> queue(1, 100);

    std::array<float, 2> in{1.0f, 2.0f};
    std::array<InputAudioBuffer<float>, 1> input = {InputAudioBuffer<float>{.sample = in}};

    // First write at time 0 succeeds for both frames.
    EXPECT_EQ(queue.write(0, input), 2);

    // Second write to the same time range — slots 0 and 1 are Full, so the
    // per-slot CAS fails and zero frames are written.
    std::array<float, 2> in2{99.0f, 99.0f};
    std::array<InputAudioBuffer<float>, 1> input2 = {InputAudioBuffer<float>{.sample = in2}};
    EXPECT_EQ(queue.write(0, input2), 0);

    // Read — original values come back, second write was dropped.
    std::array<float, 2> out{};
    std::array<OutputAudioBuffer<float>, 1> output = {OutputAudioBuffer<float>{.sample = out}};
    auto const r = queue.read(output);
    EXPECT_EQ(r.frames_with_data, 2);
    EXPECT_EQ(out[0], 1.0f);
    EXPECT_EQ(out[1], 2.0f);
}

TEST(presentation_queue_read_result, read_drains_slot_so_next_write_succeeds)
{
    AudioPresentationQueue<float> queue(1, 100);

    std::array<float, 1> in{42.0f};
    std::array<InputAudioBuffer<float>, 1> input = {InputAudioBuffer<float>{.sample = in}};

    // Write, read, then write again to the SAME slot index (different time
    // mapping to same slot via modulo on the 100-capacity ring).
    EXPECT_EQ(queue.write(0, input), 1);

    std::array<float, 1> out{};
    std::array<OutputAudioBuffer<float>, 1> output = {OutputAudioBuffer<float>{.sample = out}};
    EXPECT_EQ(queue.read(output).frames_with_data, 1);
    EXPECT_EQ(out[0], 42.0f);

    // After read, slot is Empty again. Time 100 maps to the same ring index;
    // center is now 1, so time 100 is in range. Write should succeed.
    std::array<float, 1> in2{77.0f};
    std::array<InputAudioBuffer<float>, 1> input2 = {InputAudioBuffer<float>{.sample = in2}};
    EXPECT_EQ(queue.write(100, input2), 1);
}

// Test runner
TEST_MAIN(statusbar_audio, audio_presentation_queue_test)