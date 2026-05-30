// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Unit tests for audio module
// These tests do NOT use hardware audio drivers.
// For hardware integration tests, run the audio_hw group explicitly.

#include "statusbar/audio/audio.hpp"

#include "statusbar/dsp/dsp.hpp"
#include "statusbar/status/status.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <print>
#include <span>
#include <system_error>
#include <thread>

using namespace statusbar::audio;
using namespace statusbar::dsp;

// Unit Tests(no hardware) =====

// Test AudioConfig struct
TEST(audio_types, audio_config_defaults)
{
    AudioConfig config{};
    EXPECT_EQ(static_cast<uint32_t>(config.sample_rate), 48000U);
    EXPECT_EQ(config.channels, 2U);
    EXPECT_EQ(static_cast<int>(config.format), static_cast<int>(SampleFormat::Float32));
    EXPECT_EQ(config.buffer_frames, 512U);
    EXPECT_TRUE(config.non_interleaved);
}

// Test DeviceInfo struct
TEST(audio_types, device_info_defaults)
{
    DeviceInfo info{};
    EXPECT_TRUE(info.name.empty());
    EXPECT_TRUE(info.uid.empty());
    EXPECT_EQ(info.max_input_channels, 0U);
    EXPECT_EQ(info.max_output_channels, 0U);
    EXPECT_FALSE(info.is_default_input);
    EXPECT_FALSE(info.is_default_output);
}

// Test InputAudioBuffer and OutputAudioBuffer
TEST(audio_types, audio_buffers)
{
    std::vector<float> data(512, 0.5f);

    InputAudioBuffer input_buf{std::span<float const>(data)};
    EXPECT_EQ(input_buf.sample.size(), 512U);
    EXPECT_EQ(input_buf.sample[0], 0.5f);

    OutputAudioBuffer output_buf{std::span<float>(data)};
    EXPECT_EQ(output_buf.sample.size(), 512U);
    output_buf.sample[0] = 1.0f;
    EXPECT_EQ(data[0], 1.0f);
}

// Test AudioCallbackParams
TEST(audio_types, callback_params)
{
    std::vector<float> input_data(512, 0.25f);
    std::vector<float> output_data(512, 0.0f);

    InputAudioBuffer input_buf{std::span<float const>(input_data)};
    OutputAudioBuffer output_buf{std::span<float>(output_data)};

    std::array<InputAudioBufferFloat, 1> input_bufs{input_buf};
    std::array<OutputAudioBufferFloat, 1> output_bufs{output_buf};

    AudioCallbackParamsFloat params{.input_buffers = input_bufs, .output_buffers = output_bufs, .stream_time = 1.5};

    EXPECT_EQ(params.input_buffers.size(), 1U);
    EXPECT_EQ(params.output_buffers.size(), 1U);
    EXPECT_EQ(params.stream_time, 1.5);
}

// Test MockDeviceProvider
TEST(audio_mock, device_provider_empty)
{
    MockDeviceProvider provider;
    EXPECT_EQ(provider.device_count(), 0U);

    auto devices = provider.enumerate_devices();
    EXPECT_TRUE(devices.empty());

    EXPECT_FALSE(provider.default_input_device().has_value());
    EXPECT_FALSE(provider.default_output_device().has_value());
}

TEST(audio_mock, device_provider_with_devices)
{
    MockDeviceProvider provider;

    DeviceInfo output_dev{
        .name = "Mock Output",
        .uid = "mock-output-1",
        .max_input_channels = 0,
        .max_output_channels = 2,
        .is_default_input = false,
        .is_default_output = true};

    DeviceInfo input_dev{
        .name = "Mock Input",
        .uid = "mock-input-1",
        .max_input_channels = 1,
        .max_output_channels = 0,
        .is_default_input = true,
        .is_default_output = false};

    provider.add_device(output_dev);
    provider.add_device(input_dev);

    EXPECT_EQ(provider.device_count(), 2U);

    auto devices = provider.enumerate_devices();
    EXPECT_EQ(devices.size(), 2U);

    // Test default device selection
    provider.set_default_output("mock-output-1");
    provider.set_default_input("mock-input-1");

    auto default_out = provider.default_output_device();
    EXPECT_TRUE(default_out.has_value());
    EXPECT_EQ(default_out->uid, "mock-output-1");

    auto default_in = provider.default_input_device();
    EXPECT_TRUE(default_in.has_value());
    EXPECT_EQ(default_in->uid, "mock-input-1");

    // Test find_device
    auto found = provider.find_device("mock-output-1", false);
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "Mock Output");

    auto found_default = provider.find_device("default", false);
    EXPECT_TRUE(found_default.has_value());
    EXPECT_EQ(found_default->uid, "mock-output-1");
}

// Test MockOutputStream
TEST(audio_mock, output_stream_create)
{
    DeviceInfo device{.name = "Mock Output", .uid = "mock-output", .max_output_channels = 2};

    AudioConfig config{
        .sample_rate = SampleRate::Rate_48000,
        .channels = 2,
        .format = SampleFormat::Float32,
        .buffer_frames = 512,
        .non_interleaved = true};

    MockOutputStream stream(device, config);

    EXPECT_FALSE(stream.is_running());
    EXPECT_EQ(stream.device().uid, "mock-output");
    EXPECT_EQ(stream.config().channels, 2U);
    EXPECT_EQ(stream.callback_count(), 0U);
}

TEST(audio_mock, output_stream_start_stop)
{
    DeviceInfo device{.name = "Mock Output", .uid = "mock-output", .max_output_channels = 2};

    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .buffer_frames = 256, .non_interleaved = true};

    MockOutputStream::MockConfig mock_config{.callback_frames = 256, .max_callbacks = 5, .simulate_latency = false};

    MockOutputStream stream(device, config, mock_config);

    int callback_count = 0;
    auto callback = [&](AudioCallbackParamsFloat const& params) -> statusbar::Status {
        // Fill with silence
        for (auto const& buf : params.output_buffers) {
            for (size_t i = 0; i < buf.sample.size(); ++i) {
                buf.sample[i] = 0.0f;
            }
        }
        callback_count++;
        return statusbar::success();
    };

    auto start_result = stream.start(std::move(callback));
    EXPECT_TRUE(start_result.has_value());

    // Wait for stream to auto-stop (max_callbacks = 5) with timeout
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (stream.is_running() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    // Stream should have auto-stopped due to max_callbacks limit
    EXPECT_FALSE(stream.is_running());

    // Should have been called at least once, up to max_callbacks
    EXPECT_TRUE(callback_count > 0);
    EXPECT_TRUE(callback_count <= 5);
}

TEST(audio_mock, output_stream_error_injection)
{
    DeviceInfo device{.name = "Mock Output", .uid = "mock-output", .max_output_channels = 2};
    AudioConfig config{.channels = 2};

    MockOutputStream::MockConfig mock_config{.error_on_start = AudioError::InitializationFailed};

    MockOutputStream stream(device, config, mock_config);

    auto callback = [](AudioCallbackParamsFloat const&) { return statusbar::success(); };

    auto result = stream.start(std::move(callback));
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(stream.is_running());
}

// Test MockInputStream
TEST(audio_mock, input_stream_create)
{
    DeviceInfo device{.name = "Mock Input", .uid = "mock-input", .max_input_channels = 1};

    AudioConfig config{
        .sample_rate = SampleRate::Rate_48000,
        .channels = 1,
        .format = SampleFormat::Float32,
        .buffer_frames = 512,
        .non_interleaved = true};

    MockInputStream stream(device, config);

    EXPECT_FALSE(stream.is_running());
    EXPECT_EQ(stream.device().uid, "mock-input");
    EXPECT_EQ(stream.config().channels, 1U);
}

TEST(audio_mock, input_stream_with_data)
{
    DeviceInfo device{.name = "Mock Input", .uid = "mock-input", .max_input_channels = 1};
    AudioConfig config{.channels = 1, .buffer_frames = 256};

    MockInputStream::MockConfig mock_config{.callback_frames = 256, .max_callbacks = 3, .simulate_latency = false};

    MockInputStream stream(device, config, mock_config);

    // Set up input data (sine wave)
    std::vector<float> input_data(1024);
    for (size_t i = 0; i < 1024; ++i) {
        input_data[i] = std::sin(static_cast<float>(i) * 0.1f);
    }
    stream.set_input_data(0, input_data);

    float peak = 0.0f;
    int callback_count = 0;
    auto callback = [&](AudioCallbackParamsFloat const& params) -> statusbar::Status {
        for (auto sample : params.input_buffers[0].sample) {
            float abs_val = std::abs(sample);
            if (abs_val > peak) {
                peak = abs_val;
            }
        }
        callback_count++;
        return statusbar::success();
    };

    auto start_result = stream.start(std::move(callback));
    EXPECT_TRUE(start_result.has_value());

    // Wait for stream to auto-stop (max_callbacks = 3) with timeout
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (stream.is_running() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    EXPECT_TRUE(callback_count > 0);
    EXPECT_TRUE(peak > 0.0f);  // Should have received some non-zero data
}

// Test AudioRing buffer
TEST(audio_ring, basic_push_pop)
{
    AudioRing ring(2, 1024);  // 2 channels, 1024 frames

    EXPECT_EQ(ring.channels(), 2U);
    EXPECT_EQ(ring.capacity(), 1024U);
    EXPECT_EQ(ring.available_read(), 0U);
    EXPECT_EQ(ring.available_write(), 1024U);

    // Create input data
    std::vector<float> ch0(256, 0.5f);
    std::vector<float> ch1(256, -0.5f);

    InputAudioBufferFloat input0{std::span<float const>(ch0)};
    InputAudioBufferFloat input1{std::span<float const>(ch1)};
    std::array<InputAudioBufferFloat, 2> inputs{input0, input1};

    // Push data
    size_t pushed = ring.push(inputs);
    EXPECT_EQ(pushed, 256U);
    EXPECT_EQ(ring.available_read(), 256U);
    EXPECT_EQ(ring.available_write(), 768U);

    // Pop data
    std::vector<float> out0(256);
    std::vector<float> out1(256);
    OutputAudioBufferFloat output0{std::span<float>(out0)};
    OutputAudioBufferFloat output1{std::span<float>(out1)};
    std::array<OutputAudioBufferFloat, 2> outputs{output0, output1};

    size_t popped = ring.pop(outputs);
    EXPECT_EQ(popped, 256U);
    EXPECT_EQ(ring.available_read(), 0U);

    // Verify data
    EXPECT_EQ(out0[0], 0.5f);
    EXPECT_EQ(out1[0], -0.5f);
}

TEST(audio_ring, wraparound)
{
    AudioRing ring(1, 100);  // Small buffer to test wraparound

    std::vector<float> data(50, 1.0f);
    InputAudioBufferFloat input{std::span<float const>(data)};
    std::array<InputAudioBufferFloat, 1> inputs{input};

    // Fill buffer
    ring.push(inputs);
    ring.push(inputs);  // Now at 100 frames

    EXPECT_EQ(ring.available_write(), 0U);

    // Pop half
    std::vector<float> out(50);
    OutputAudioBufferFloat output{std::span<float>(out)};
    std::array<OutputAudioBufferFloat, 1> outputs{output};
    ring.pop(outputs);

    EXPECT_EQ(ring.available_read(), 50U);
    EXPECT_EQ(ring.available_write(), 50U);

    // Push more (should wrap around)
    ring.push(inputs);
    EXPECT_EQ(ring.available_read(), 100U);
}

TEST(audio_ring, silence_fill)
{
    AudioRing ring(1, 100);

    // Push only 30 frames
    std::vector<float> data(30, 1.0f);
    InputAudioBufferFloat input{std::span<float const>(data)};
    std::array<InputAudioBufferFloat, 1> inputs{input};
    ring.push(inputs);

    // Try to pop 50 frames
    std::vector<float> out(50, -1.0f);  // Pre-fill with -1
    OutputAudioBufferFloat output{std::span<float>(out)};
    std::array<OutputAudioBufferFloat, 1> outputs{output};

    size_t popped = ring.pop(outputs);
    EXPECT_EQ(popped, 30U);

    // First 30 should be 1.0, rest should be silence (0.0)
    EXPECT_EQ(out[0], 1.0f);
    EXPECT_EQ(out[29], 1.0f);
    EXPECT_EQ(out[30], 0.0f);
    EXPECT_EQ(out[49], 0.0f);
}

TEST(audio_ring, clear)
{
    AudioRing ring(1, 100);

    std::vector<float> data(50, 1.0f);
    InputAudioBufferFloat input{std::span<float const>(data)};
    std::array<InputAudioBufferFloat, 1> inputs{input};
    ring.push(inputs);

    EXPECT_EQ(ring.available_read(), 50U);

    ring.clear();

    EXPECT_EQ(ring.available_read(), 0U);
    EXPECT_EQ(ring.available_write(), 100U);
}

TEST(audio_ring, skip)
{
    AudioRing ring(1, 100);

    std::vector<float> data(50, 1.0f);
    InputAudioBufferFloat input{std::span<float const>(data)};
    std::array<InputAudioBufferFloat, 1> inputs{input};
    ring.push(inputs);

    size_t skipped = ring.skip(20);
    EXPECT_EQ(skipped, 20U);
    EXPECT_EQ(ring.available_read(), 30U);

    // Skip more than available
    skipped = ring.skip(100);
    EXPECT_EQ(skipped, 30U);
    EXPECT_EQ(ring.available_read(), 0U);
}

TEST(audio_ring, can_push)
{
    AudioRing ring(1, 100);

    // Empty buffer - can push up to capacity
    std::vector<float> data50(50, 1.0f);
    InputAudioBufferFloat input50{std::span<float const>(data50)};
    std::array<InputAudioBufferFloat, 1> inputs50{input50};
    EXPECT_TRUE(ring.can_push(inputs50));

    std::vector<float> data100(100, 1.0f);
    InputAudioBufferFloat input100{std::span<float const>(data100)};
    std::array<InputAudioBufferFloat, 1> inputs100{input100};
    EXPECT_TRUE(ring.can_push(inputs100));

    // Cannot push more than capacity
    std::vector<float> data101(101, 1.0f);
    InputAudioBufferFloat input101{std::span<float const>(data101)};
    std::array<InputAudioBufferFloat, 1> inputs101{input101};
    EXPECT_FALSE(ring.can_push(inputs101));

    // Push some data
    ring.push(inputs50);
    EXPECT_EQ(ring.available_write(), 50U);

    // Can still push 50 more
    EXPECT_TRUE(ring.can_push(inputs50));

    // Cannot push 51
    std::vector<float> data51(51, 1.0f);
    InputAudioBufferFloat input51{std::span<float const>(data51)};
    std::array<InputAudioBufferFloat, 1> inputs51{input51};
    EXPECT_FALSE(ring.can_push(inputs51));

    // Empty span always returns true
    std::array<InputAudioBufferFloat, 0> empty{};
    EXPECT_TRUE(ring.can_push(empty));
}

TEST(audio_ring, can_pop)
{
    AudioRing ring(1, 100);

    // Empty buffer - cannot pop any frames
    std::vector<float> out50(50);
    OutputAudioBufferFloat output50{std::span<float>(out50)};
    std::array<OutputAudioBufferFloat, 1> outputs50{output50};
    EXPECT_FALSE(ring.can_pop(outputs50));

    // Push 30 frames
    std::vector<float> data30(30, 1.0f);
    InputAudioBufferFloat input30{std::span<float const>(data30)};
    std::array<InputAudioBufferFloat, 1> inputs30{input30};
    ring.push(inputs30);

    // Can pop up to 30
    std::vector<float> out30(30);
    OutputAudioBufferFloat output30{std::span<float>(out30)};
    std::array<OutputAudioBufferFloat, 1> outputs30{output30};
    EXPECT_TRUE(ring.can_pop(outputs30));

    // Cannot pop more than 30
    EXPECT_FALSE(ring.can_pop(outputs50));

    // Empty span always returns true
    std::array<OutputAudioBufferFloat, 0> empty{};
    EXPECT_TRUE(ring.can_pop(empty));
}

TEST(audio_ring, can_peek)
{
    AudioRing ring(1, 100);

    // Empty buffer - cannot peek any frames
    std::vector<float> out50(50);
    OutputAudioBufferFloat output50{std::span<float>(out50)};
    std::array<OutputAudioBufferFloat, 1> outputs50{output50};
    EXPECT_FALSE(ring.can_peek(outputs50));

    // Push 40 frames
    std::vector<float> data40(40, 1.0f);
    InputAudioBufferFloat input40{std::span<float const>(data40)};
    std::array<InputAudioBufferFloat, 1> inputs40{input40};
    ring.push(inputs40);

    // Can peek up to 40
    std::vector<float> out40(40);
    OutputAudioBufferFloat output40{std::span<float>(out40)};
    std::array<OutputAudioBufferFloat, 1> outputs40{output40};
    EXPECT_TRUE(ring.can_peek(outputs40));

    // Cannot peek more than 40
    EXPECT_FALSE(ring.can_peek(outputs50));

    // Peeking doesn't consume - can_peek should still return true
    ring.peek(outputs40);
    EXPECT_TRUE(ring.can_peek(outputs40));

    // Empty span always returns true
    std::array<OutputAudioBufferFloat, 0> empty{};
    EXPECT_TRUE(ring.can_peek(empty));
}

TEST(audio_ring, can_skip)
{
    AudioRing ring(1, 100);

    // Empty buffer - cannot skip any frames (except 0)
    EXPECT_TRUE(ring.can_skip(0));
    EXPECT_FALSE(ring.can_skip(1));
    EXPECT_FALSE(ring.can_skip(50));

    // Push 60 frames
    std::vector<float> data60(60, 1.0f);
    InputAudioBufferFloat input60{std::span<float const>(data60)};
    std::array<InputAudioBufferFloat, 1> inputs60{input60};
    ring.push(inputs60);

    // Can skip up to 60
    EXPECT_TRUE(ring.can_skip(0));
    EXPECT_TRUE(ring.can_skip(30));
    EXPECT_TRUE(ring.can_skip(60));
    EXPECT_FALSE(ring.can_skip(61));
    EXPECT_FALSE(ring.can_skip(100));

    // Skip 20, now only 40 available
    ring.skip(20);
    EXPECT_TRUE(ring.can_skip(40));
    EXPECT_FALSE(ring.can_skip(41));
}

// ===== AudioRing<simd_float32x4> SIMD tests =====

TEST(audio_ring_simd, basic_push_pop)
{
    AudioRing<simd_float32x4> ring(2, 256);  // 2 channels, 256 frames of simd_float32x4

    EXPECT_EQ(ring.channels(), 2U);
    EXPECT_EQ(ring.capacity(), 256U);
    EXPECT_EQ(ring.available_read(), 0U);
    EXPECT_EQ(ring.available_write(), 256U);

    // Create input data - each simd_float32x4 contains 4 samples
    std::vector<simd_float32x4> ch0(64);  // 64 vectors = 64 frames in the ring
    std::vector<simd_float32x4> ch1(64);
    for (size_t i = 0; i < 64; ++i) {
        ch0[i] = simd_float32x4::splat(0.5f);
        ch1[i] = simd_float32x4::splat(-0.5f);
    }

    InputAudioBuffer<simd_float32x4> input0{std::span<simd_float32x4 const>(ch0)};
    InputAudioBuffer<simd_float32x4> input1{std::span<simd_float32x4 const>(ch1)};
    std::array<InputAudioBuffer<simd_float32x4>, 2> inputs{input0, input1};

    // Push data
    size_t pushed = ring.push(inputs);
    EXPECT_EQ(pushed, 64U);
    EXPECT_EQ(ring.available_read(), 64U);
    EXPECT_EQ(ring.available_write(), 192U);

    // Pop data
    std::vector<simd_float32x4> out0(64);
    std::vector<simd_float32x4> out1(64);
    OutputAudioBuffer<simd_float32x4> output0{std::span<simd_float32x4>(out0)};
    OutputAudioBuffer<simd_float32x4> output1{std::span<simd_float32x4>(out1)};
    std::array<OutputAudioBuffer<simd_float32x4>, 2> outputs{output0, output1};

    size_t popped = ring.pop(outputs);
    EXPECT_EQ(popped, 64U);
    EXPECT_EQ(ring.available_read(), 0U);

    // Verify data - check all 4 lanes of first SIMD vector
    EXPECT_EQ(out0[0][0], 0.5f);
    EXPECT_EQ(out0[0][1], 0.5f);
    EXPECT_EQ(out0[0][2], 0.5f);
    EXPECT_EQ(out0[0][3], 0.5f);
    EXPECT_EQ(out1[0][0], -0.5f);
    EXPECT_EQ(out1[0][1], -0.5f);
    EXPECT_EQ(out1[0][2], -0.5f);
    EXPECT_EQ(out1[0][3], -0.5f);
}

TEST(audio_ring_simd, wraparound)
{
    AudioRing<simd_float32x4> ring(1, 100);

    std::vector<simd_float32x4> data(50);
    for (auto& v : data) {
        v = simd_float32x4::splat(1.0f);
    }
    InputAudioBuffer<simd_float32x4> input{std::span<simd_float32x4 const>(data)};
    std::array<InputAudioBuffer<simd_float32x4>, 1> inputs{input};

    // Fill buffer
    ring.push(inputs);
    ring.push(inputs);  // Now at 100 frames

    EXPECT_EQ(ring.available_write(), 0U);

    // Pop half
    std::vector<simd_float32x4> out(50);
    OutputAudioBuffer<simd_float32x4> output{std::span<simd_float32x4>(out)};
    std::array<OutputAudioBuffer<simd_float32x4>, 1> outputs{output};
    ring.pop(outputs);

    EXPECT_EQ(ring.available_read(), 50U);
    EXPECT_EQ(ring.available_write(), 50U);

    // Push more (should wrap around)
    ring.push(inputs);
    EXPECT_EQ(ring.available_read(), 100U);
}

TEST(audio_ring_simd, silence_fill)
{
    AudioRing<simd_float32x4> ring(1, 100);

    // Push only 30 frames
    std::vector<simd_float32x4> data(30);
    for (auto& v : data) {
        v = simd_float32x4::splat(1.0f);
    }
    InputAudioBuffer<simd_float32x4> input{std::span<simd_float32x4 const>(data)};
    std::array<InputAudioBuffer<simd_float32x4>, 1> inputs{input};
    ring.push(inputs);

    // Try to pop 50 frames - pre-fill with -1
    std::vector<simd_float32x4> out(50);
    for (auto& v : out) {
        v = simd_float32x4::splat(-1.0f);
    }
    OutputAudioBuffer<simd_float32x4> output{std::span<simd_float32x4>(out)};
    std::array<OutputAudioBuffer<simd_float32x4>, 1> outputs{output};

    size_t popped = ring.pop(outputs);
    EXPECT_EQ(popped, 30U);

    // First 30 should be 1.0 (all lanes), rest should be silence (0.0)
    EXPECT_EQ(out[0][0], 1.0f);
    EXPECT_EQ(out[29][0], 1.0f);
    EXPECT_EQ(out[30][0], 0.0f);
    EXPECT_EQ(out[49][0], 0.0f);
}

TEST(audio_ring_simd, can_push)
{
    AudioRing<simd_float32x4> ring(1, 100);

    // Empty buffer - can push up to capacity
    std::vector<simd_float32x4> data50(50);
    InputAudioBuffer<simd_float32x4> input50{std::span<simd_float32x4 const>(data50)};
    std::array<InputAudioBuffer<simd_float32x4>, 1> inputs50{input50};
    EXPECT_TRUE(ring.can_push(inputs50));

    std::vector<simd_float32x4> data100(100);
    InputAudioBuffer<simd_float32x4> input100{std::span<simd_float32x4 const>(data100)};
    std::array<InputAudioBuffer<simd_float32x4>, 1> inputs100{input100};
    EXPECT_TRUE(ring.can_push(inputs100));

    // Cannot push more than capacity
    std::vector<simd_float32x4> data101(101);
    InputAudioBuffer<simd_float32x4> input101{std::span<simd_float32x4 const>(data101)};
    std::array<InputAudioBuffer<simd_float32x4>, 1> inputs101{input101};
    EXPECT_FALSE(ring.can_push(inputs101));

    // Push some data
    ring.push(inputs50);
    EXPECT_EQ(ring.available_write(), 50U);

    // Can still push 50 more
    EXPECT_TRUE(ring.can_push(inputs50));

    // Cannot push 51
    std::vector<simd_float32x4> data51(51);
    InputAudioBuffer<simd_float32x4> input51{std::span<simd_float32x4 const>(data51)};
    std::array<InputAudioBuffer<simd_float32x4>, 1> inputs51{input51};
    EXPECT_FALSE(ring.can_push(inputs51));
}

TEST(audio_ring_simd, can_pop)
{
    AudioRing<simd_float32x4> ring(1, 100);

    // Empty buffer - cannot pop any frames
    std::vector<simd_float32x4> out50(50);
    OutputAudioBuffer<simd_float32x4> output50{std::span<simd_float32x4>(out50)};
    std::array<OutputAudioBuffer<simd_float32x4>, 1> outputs50{output50};
    EXPECT_FALSE(ring.can_pop(outputs50));

    // Push 30 frames
    std::vector<simd_float32x4> data30(30);
    InputAudioBuffer<simd_float32x4> input30{std::span<simd_float32x4 const>(data30)};
    std::array<InputAudioBuffer<simd_float32x4>, 1> inputs30{input30};
    ring.push(inputs30);

    // Can pop up to 30
    std::vector<simd_float32x4> out30(30);
    OutputAudioBuffer<simd_float32x4> output30{std::span<simd_float32x4>(out30)};
    std::array<OutputAudioBuffer<simd_float32x4>, 1> outputs30{output30};
    EXPECT_TRUE(ring.can_pop(outputs30));

    // Cannot pop more than 30
    EXPECT_FALSE(ring.can_pop(outputs50));
}

TEST(audio_ring_simd, peek_and_skip)
{
    AudioRing<simd_float32x4> ring(1, 100);

    // Push 60 frames with distinct values
    std::vector<simd_float32x4> data60(60);
    for (size_t i = 0; i < 60; ++i) {
        data60[i] = simd_float32x4::splat(static_cast<float>(i));
    }
    InputAudioBuffer<simd_float32x4> input60{std::span<simd_float32x4 const>(data60)};
    std::array<InputAudioBuffer<simd_float32x4>, 1> inputs60{input60};
    ring.push(inputs60);

    // Peek at first 20
    std::vector<simd_float32x4> peek_out(20);
    OutputAudioBuffer<simd_float32x4> peek_buf{std::span<simd_float32x4>(peek_out)};
    std::array<OutputAudioBuffer<simd_float32x4>, 1> peek_outputs{peek_buf};
    size_t peeked = ring.peek(peek_outputs);
    EXPECT_EQ(peeked, 20U);
    EXPECT_EQ(peek_out[0][0], 0.0f);
    EXPECT_EQ(peek_out[19][0], 19.0f);

    // Available should still be 60 (peek doesn't consume)
    EXPECT_EQ(ring.available_read(), 60U);

    // Skip 20, now only 40 available
    EXPECT_TRUE(ring.can_skip(20));
    size_t skipped = ring.skip(20);
    EXPECT_EQ(skipped, 20U);
    EXPECT_EQ(ring.available_read(), 40U);

    // Peek again - should now start at frame 20
    peeked = ring.peek(peek_outputs);
    EXPECT_EQ(peeked, 20U);
    EXPECT_EQ(peek_out[0][0], 20.0f);
    EXPECT_EQ(peek_out[19][0], 39.0f);
}

TEST(audio_ring_simd, multichannel)
{
    // Test with 4 channels of SIMD data
    AudioRing<simd_float32x4> ring(4, 128);

    EXPECT_EQ(ring.channels(), 4U);
    EXPECT_EQ(ring.capacity(), 128U);

    // Create input for all 4 channels with distinct values
    std::vector<simd_float32x4> ch0(32), ch1(32), ch2(32), ch3(32);
    for (size_t i = 0; i < 32; ++i) {
        ch0[i] = simd_float32x4::splat(1.0f);
        ch1[i] = simd_float32x4::splat(2.0f);
        ch2[i] = simd_float32x4::splat(3.0f);
        ch3[i] = simd_float32x4::splat(4.0f);
    }

    InputAudioBuffer<simd_float32x4> in0{std::span<simd_float32x4 const>(ch0)};
    InputAudioBuffer<simd_float32x4> in1{std::span<simd_float32x4 const>(ch1)};
    InputAudioBuffer<simd_float32x4> in2{std::span<simd_float32x4 const>(ch2)};
    InputAudioBuffer<simd_float32x4> in3{std::span<simd_float32x4 const>(ch3)};
    std::array<InputAudioBuffer<simd_float32x4>, 4> inputs{in0, in1, in2, in3};

    size_t pushed = ring.push(inputs);
    EXPECT_EQ(pushed, 32U);
    EXPECT_EQ(ring.available_read(), 32U);

    // Pop and verify each channel
    std::vector<simd_float32x4> out0(32), out1(32), out2(32), out3(32);
    OutputAudioBuffer<simd_float32x4> o0{std::span<simd_float32x4>(out0)};
    OutputAudioBuffer<simd_float32x4> o1{std::span<simd_float32x4>(out1)};
    OutputAudioBuffer<simd_float32x4> o2{std::span<simd_float32x4>(out2)};
    OutputAudioBuffer<simd_float32x4> o3{std::span<simd_float32x4>(out3)};
    std::array<OutputAudioBuffer<simd_float32x4>, 4> outputs{o0, o1, o2, o3};

    size_t popped = ring.pop(outputs);
    EXPECT_EQ(popped, 32U);

    // Check each channel has correct value (all lanes)
    for (size_t lane = 0; lane < 4; ++lane) {
        EXPECT_EQ(out0[0][lane], 1.0f);
        EXPECT_EQ(out1[0][lane], 2.0f);
        EXPECT_EQ(out2[0][lane], 3.0f);
        EXPECT_EQ(out3[0][lane], 4.0f);
    }
}

// Edge case tests

TEST(audio_types_edge, sample_format_values)
{
    // Verify SampleFormat enum values are correct
    EXPECT_EQ(static_cast<uint8_t>(SampleFormat::Float32), 0);
    EXPECT_EQ(static_cast<uint8_t>(SampleFormat::Int16), 1);
    EXPECT_EQ(static_cast<uint8_t>(SampleFormat::Int32), 2);
}

TEST(audio_types_edge, sample_rate_values)
{
    // Verify SampleRate enum values are correct
    EXPECT_EQ(static_cast<uint32_t>(SampleRate::Rate_44100), 44100);
    EXPECT_EQ(static_cast<uint32_t>(SampleRate::Rate_48000), 48000);
    EXPECT_EQ(static_cast<uint32_t>(SampleRate::Rate_96000), 96000);
    EXPECT_EQ(static_cast<uint32_t>(SampleRate::Rate_192000), 192000);
}

TEST(audio_ring_edge, single_channel)
{
    AudioRing ring(1, 100);
    EXPECT_EQ(ring.channels(), 1U);

    std::vector<float> data(50, 1.0f);
    InputAudioBufferFloat input{std::span<float const>(data)};
    std::array<InputAudioBufferFloat, 1> inputs{input};

    ring.push(inputs);
    EXPECT_EQ(ring.available_read(), 50U);
}

TEST(audio_ring_edge, many_channels)
{
    AudioRing ring(8, 256);  // 8 channels
    EXPECT_EQ(ring.channels(), 8U);
    EXPECT_EQ(ring.capacity(), 256U);
}

TEST(audio_ring_edge, push_more_than_capacity)
{
    AudioRing ring(1, 50);  // Small capacity

    std::vector<float> data(100, 1.0f);  // More than capacity
    InputAudioBufferFloat input{std::span<float const>(data)};
    std::array<InputAudioBufferFloat, 1> inputs{input};

    size_t pushed = ring.push(inputs);
    EXPECT_EQ(pushed, 50U);  // Only capacity was pushed
    EXPECT_EQ(ring.available_read(), 50U);
    EXPECT_EQ(ring.available_write(), 0U);
}

TEST(audio_ring_edge, default_constructor)
{
    AudioRing<float> ring;  // Default constructor
    EXPECT_EQ(ring.channels(), 0U);
    EXPECT_EQ(ring.capacity(), 0U);

    // Init after default construction
    ring.init(2, 512);
    EXPECT_EQ(ring.channels(), 2U);
    EXPECT_EQ(ring.capacity(), 512U);
}

TEST(audio_ring_edge, reinit)
{
    AudioRing ring(2, 100);

    std::vector<float> data(50, 1.0f);
    InputAudioBufferFloat input{std::span<float const>(data)};
    std::array<InputAudioBufferFloat, 1> inputs{input};
    ring.push(inputs);

    EXPECT_EQ(ring.available_read(), 50U);

    // Reinit clears the buffer
    ring.init(4, 200);
    EXPECT_EQ(ring.channels(), 4U);
    EXPECT_EQ(ring.capacity(), 200U);
    EXPECT_EQ(ring.available_read(), 0U);
}

TEST(audio_ring_edge, peek_does_not_consume)
{
    AudioRing ring(1, 100);

    std::vector<float> data(30, 0.5f);
    InputAudioBufferFloat input{std::span<float const>(data)};
    std::array<InputAudioBufferFloat, 1> inputs{input};
    ring.push(inputs);

    // Peek multiple times
    std::vector<float> out(20);
    OutputAudioBufferFloat output{std::span<float>(out)};
    std::array<OutputAudioBufferFloat, 1> outputs{output};

    for (int i = 0; i < 3; ++i) {
        size_t peeked = ring.peek(outputs);
        EXPECT_EQ(peeked, 20U);
        EXPECT_EQ(ring.available_read(), 30U);  // Still 30 available
    }
}

TEST(audio_ring_edge, skip_all)
{
    AudioRing ring(1, 100);

    std::vector<float> data(50, 1.0f);
    InputAudioBufferFloat input{std::span<float const>(data)};
    std::array<InputAudioBufferFloat, 1> inputs{input};
    ring.push(inputs);

    // Skip exactly available
    size_t skipped = ring.skip(50);
    EXPECT_EQ(skipped, 50U);
    EXPECT_EQ(ring.available_read(), 0U);
}

//
// Exhaustive error message coverage
//

TEST(audio_error, all_errors_have_messages)
{
    // Verify every AudioError code produces a non-"Unknown" message
    auto check = [](AudioError e) {
        std::error_code ec = e;
        EXPECT_TRUE(!ec.message().empty());
        EXPECT_TRUE(ec.message().find("Unknown") == std::string::npos);
    };
    check(AudioError::DeviceNotFound);
    check(AudioError::InvalidConfig);
    check(AudioError::DeviceBusy);
    check(AudioError::HardwareError);
    check(AudioError::CallbackError);
    check(AudioError::BufferUnderrun);
    check(AudioError::BufferOverrun);
    check(AudioError::UnsupportedFormat);
    check(AudioError::UnsupportedSampleRate);
    check(AudioError::StreamNotRunning);
    check(AudioError::AlreadyRunning);
    check(AudioError::InitializationFailed);
}

TEST(audio_error, category_name)
{
    EXPECT_EQ(std::string_view{audio_error_category().name()}, "statusbar.audio");
}
// ─────────────────────────────────────────────────────────────────────────────
// Concurrent SPSC stress tests for AudioRing — exercise the producer/consumer
// memory ordering across two real threads. Single-threaded tests above can't
// catch acquire/release pairing bugs that only surface under contention,
// especially on weakly-ordered ARM. Run via `make test-asan` for race
// detection coverage.
// ─────────────────────────────────────────────────────────────────────────────

TEST(audio_ring_concurrent, spsc_preserves_order_under_contention)
{
    constexpr size_t iterations = 100'000;
    constexpr size_t ring_frames = 64;
    constexpr size_t batch_frames = 8;

    AudioRing<float> ring(1, ring_frames);
    std::atomic<bool> mismatch_seen{false};

    std::thread producer{[&] {
        std::array<float, batch_frames> batch{};
        InputAudioBufferFloat input{std::span<float const>(batch)};
        std::array<InputAudioBufferFloat, 1> inputs{input};
        for (size_t i = 0; i < iterations; ++i) {
            // Encode the iteration counter into every sample of this batch
            // so the consumer can detect both ordering and torn writes.
            float const v = static_cast<float>(i);
            std::fill(batch.begin(), batch.end(), v);
            while (ring.push(inputs) == 0) {
                std::this_thread::yield();
            }
        }
    }};

    std::thread consumer{[&] {
        std::array<float, batch_frames> batch{};
        OutputAudioBufferFloat output{std::span<float>(batch)};
        std::array<OutputAudioBufferFloat, 1> outputs{output};
        for (size_t i = 0; i < iterations; ++i) {
            while (ring.pop(outputs) == 0) {
                std::this_thread::yield();
            }
            float const expected = static_cast<float>(i);
            for (float const sample : batch) {
                if (sample != expected) {
                    mismatch_seen.store(true, std::memory_order_relaxed);
                }
            }
        }
    }};

    producer.join();
    consumer.join();

    EXPECT_FALSE(mismatch_seen.load(std::memory_order_relaxed));
    EXPECT_EQ(ring.available_read(), 0U);
}

TEST(audio_ring_concurrent, multichannel_lanes_stay_aligned)
{
    constexpr size_t iterations = 50'000;
    constexpr size_t num_channels = 4;
    constexpr size_t ring_frames = 64;
    constexpr size_t batch_frames = 8;

    AudioRing<float> ring(num_channels, ring_frames);
    std::atomic<bool> cross_channel_mismatch{false};

    std::thread producer{[&] {
        std::array<std::array<float, batch_frames>, num_channels> batches{};
        std::array<InputAudioBufferFloat, num_channels> inputs;
        for (size_t c = 0; c < num_channels; ++c) {
            inputs[c] = InputAudioBufferFloat{std::span<float const>(batches[c])};
        }
        for (size_t i = 0; i < iterations; ++i) {
            // Each channel encodes (iteration * num_channels + channel_index) so
            // any cross-channel data scrambling is visible on the consumer.
            for (size_t c = 0; c < num_channels; ++c) {
                float const v = static_cast<float>((i * num_channels) + c);
                std::fill(batches[c].begin(), batches[c].end(), v);
            }
            while (ring.push(inputs) == 0) {
                std::this_thread::yield();
            }
        }
    }};

    std::thread consumer{[&] {
        std::array<std::array<float, batch_frames>, num_channels> batches{};
        std::array<OutputAudioBufferFloat, num_channels> outputs;
        for (size_t c = 0; c < num_channels; ++c) {
            outputs[c] = OutputAudioBufferFloat{std::span<float>(batches[c])};
        }
        for (size_t i = 0; i < iterations; ++i) {
            while (ring.pop(outputs) == 0) {
                std::this_thread::yield();
            }
            for (size_t c = 0; c < num_channels; ++c) {
                float const expected = static_cast<float>((i * num_channels) + c);
                for (float const sample : batches[c]) {
                    if (sample != expected) {
                        cross_channel_mismatch.store(true, std::memory_order_relaxed);
                    }
                }
            }
        }
    }};

    producer.join();
    consumer.join();

    EXPECT_FALSE(cross_channel_mismatch.load(std::memory_order_relaxed));
}

// Main test runner function required by create_test_sourcelist
TEST_MAIN(statusbar_audio, audio_test)