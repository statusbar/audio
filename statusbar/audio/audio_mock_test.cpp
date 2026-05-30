// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Unit tests for audio mock implementations
// Tests MockDeviceProvider, MockOutputStream, MockInputStream

#include "statusbar/audio/audio.hpp"
#include "statusbar/status/status.hpp"
#include "statusbar/test/test.hpp"

#include <atomic>
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
#include <vector>

using namespace statusbar::audio;

// MockDeviceProvider tests

TEST(mock_provider, empty_provider)
{
    MockDeviceProvider provider;

    auto devices = provider.enumerate_devices();
    EXPECT_EQ(devices.size(), 0);

    EXPECT_FALSE(provider.default_input_device().has_value());
    EXPECT_FALSE(provider.default_output_device().has_value());
    EXPECT_FALSE(provider.find_device("any", false).has_value());
}

TEST(mock_provider, add_device)
{
    MockDeviceProvider provider;

    auto device = make_mock_device("Test Device", "test-uid", 2, 8);
    provider.add_device(device);

    EXPECT_EQ(provider.device_count(), 1);

    auto devices = provider.enumerate_devices();
    EXPECT_EQ(devices.size(), 1);
    EXPECT_EQ(devices[0].name, "Test Device");
    EXPECT_EQ(devices[0].uid, "test-uid");
    EXPECT_EQ(devices[0].max_input_channels, 2);
    EXPECT_EQ(devices[0].max_output_channels, 8);
}

TEST(mock_provider, find_by_uid)
{
    MockDeviceProvider provider;
    provider.add_device(make_mock_device("Device A", "uid-a", 2, 2));
    provider.add_device(make_mock_device("Device B", "uid-b", 0, 8));  // Output only

    auto found = provider.find_device("uid-a", false);
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "Device A");

    found = provider.find_device("uid-b", false);
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "Device B");

    // uid-b has no input channels
    found = provider.find_device("uid-b", true);
    EXPECT_FALSE(found.has_value());
}

TEST(mock_provider, find_by_name)
{
    MockDeviceProvider provider;
    provider.add_device(make_mock_device("My Audio Device", "uid-1", 2, 2));

    auto found = provider.find_device("My Audio Device", false);
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->uid, "uid-1");
}

TEST(mock_provider, default_device)
{
    MockDeviceProvider provider;
    provider.add_device(make_mock_device("Device A", "uid-a", 2, 2));
    provider.add_device(make_mock_device("Device B", "uid-b", 2, 2));
    provider.set_default_output("uid-b");
    provider.set_default_input("uid-a");

    auto default_out = provider.default_output_device();
    EXPECT_TRUE(default_out.has_value());
    EXPECT_EQ(default_out->uid, "uid-b");

    auto default_in = provider.default_input_device();
    EXPECT_TRUE(default_in.has_value());
    EXPECT_EQ(default_in->uid, "uid-a");

    // "default" identifier should find the default device
    auto found = provider.find_device("default", false);
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->uid, "uid-b");

    found = provider.find_device("default", true);
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->uid, "uid-a");
}

TEST(mock_provider, fallback_default)
{
    MockDeviceProvider provider;
    provider.add_device(make_mock_device("Only Device", "only-uid", 2, 2));
    // No explicit default set - should fall back to first device

    auto default_out = provider.default_output_device();
    EXPECT_TRUE(default_out.has_value());
    EXPECT_EQ(default_out->uid, "only-uid");
}

// MockOutputStream tests

TEST(mock_output, create_and_config)
{
    auto device = make_mock_device("Test", "test", 0, 2);
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .buffer_frames = 256};

    MockOutputStream stream(device, config);

    EXPECT_FALSE(stream.is_running());
    EXPECT_EQ(stream.config().channels, 2);
    EXPECT_EQ(stream.device().uid, "test");
    EXPECT_EQ(stream.effective_sample_rate(), 48000.0);
}

TEST(mock_output, start_stop)
{
    auto device = make_mock_device("Test", "test", 0, 2);
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .buffer_frames = 256};
    MockOutputStream::MockConfig mock_cfg{.callback_frames = 128, .max_callbacks = 5};

    MockOutputStream stream(device, config, mock_cfg);

    std::atomic<int> callback_count{0};
    auto result = stream.start([&](AudioCallbackParamsFloat const& params) -> statusbar::Status {
        // Fill with silence
        for (auto& buf : params.output_buffers) {
            for (auto& sample : buf.sample) {
                sample = 0.0f;
            }
        }
        ++callback_count;
        return statusbar::success();
    });

    EXPECT_TRUE(result.has_value());

    while (stream.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(callback_count.load(), 5);
    EXPECT_EQ(stream.callback_count(), 5);
}

TEST(mock_output, capture_output)
{
    auto device = make_mock_device("Test", "test", 0, 2);
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .buffer_frames = 256};
    MockOutputStream::MockConfig mock_cfg{.callback_frames = 64, .max_callbacks = 4, .capture_output = true};

    MockOutputStream stream(device, config, mock_cfg);

    auto result = stream.start([](AudioCallbackParamsFloat const& params) -> statusbar::Status {
        // Generate test pattern: 0.5 on channel 0, -0.5 on channel 1
        for (auto& sample : params.output_buffers[0].sample) {
            sample = 0.5f;
        }
        for (auto& sample : params.output_buffers[1].sample) {
            sample = -0.5f;
        }
        return statusbar::success();
    });

    EXPECT_TRUE(result.has_value());

    while (stream.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Verify captured output
    auto const& ch0 = stream.get_output_channel(0);
    auto const& ch1 = stream.get_output_channel(1);

    EXPECT_EQ(ch0.size(), 64 * 4);  // 4 callbacks * 64 frames
    EXPECT_EQ(ch1.size(), 64 * 4);

    EXPECT_TRUE(stream.has_output());
    EXPECT_TRUE(std::fabs(stream.get_peak_level(0) - 0.5f) < 0.001f);
    EXPECT_TRUE(std::fabs(stream.get_peak_level(1) - 0.5f) < 0.001f);

    // Check actual values
    for (float sample : ch0) {
        EXPECT_TRUE(std::fabs(sample - 0.5f) < 0.001f);
    }
    for (float sample : ch1) {
        EXPECT_TRUE(std::fabs(sample - (-0.5f)) < 0.001f);
    }
}

TEST(mock_output, error_on_start)
{
    auto device = make_mock_device("Test", "test", 0, 2);
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .buffer_frames = 256};
    MockOutputStream::MockConfig mock_cfg{.error_on_start = AudioError::DeviceBusy};

    MockOutputStream stream(device, config, mock_cfg);

    auto result = stream.start([](AudioCallbackParamsFloat const&) -> statusbar::Status { return statusbar::success(); });

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), make_error_code(AudioError::DeviceBusy));
    EXPECT_FALSE(stream.is_running());
}

TEST(mock_output, already_running)
{
    auto device = make_mock_device("Test", "test", 0, 2);
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .buffer_frames = 256};
    MockOutputStream::MockConfig mock_cfg{.max_callbacks = 1000};  // Long running

    MockOutputStream stream(device, config, mock_cfg);

    auto result = stream.start([](AudioCallbackParamsFloat const&) -> statusbar::Status { return statusbar::success(); });
    EXPECT_TRUE(result.has_value());

    // Try to start again
    result = stream.start([](AudioCallbackParamsFloat const&) -> statusbar::Status { return statusbar::success(); });
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), make_error_code(AudioError::AlreadyRunning));

    stream.stop();
}

TEST(mock_output, callback_negative_return_stops)
{
    auto device = make_mock_device("Test", "test", 0, 2);
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .buffer_frames = 256};
    MockOutputStream::MockConfig mock_cfg{.callback_frames = 64, .max_callbacks = 100};

    MockOutputStream stream(device, config, mock_cfg);

    std::atomic<int> count{0};
    auto result = stream.start([&](AudioCallbackParamsFloat const&) -> statusbar::Status {
        ++count;
        if (count >= 3) {
            return statusbar::failure(AudioError::StreamNotRunning);  // Signal to stop
        }
        return statusbar::success();
    });

    EXPECT_TRUE(result.has_value());

    while (stream.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(count.load(), 3);
}

// MockInputStream tests

TEST(mock_input, create_and_config)
{
    auto device = make_mock_device("Test", "test", 2, 0);  // Input only
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .buffer_frames = 256};

    MockInputStream stream(device, config);

    EXPECT_FALSE(stream.is_running());
    EXPECT_EQ(stream.config().channels, 2);
    EXPECT_EQ(stream.device().uid, "test");
}

TEST(mock_input, receive_data)
{
    auto device = make_mock_device("Test", "test", 2, 0);
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .buffer_frames = 256};
    MockInputStream::MockConfig mock_cfg{.callback_frames = 64, .max_callbacks = 4};

    MockInputStream stream(device, config, mock_cfg);

    // Pre-configure input data
    std::vector<float> input_ch0(256, 0.25f);
    std::vector<float> input_ch1(256, -0.25f);
    stream.set_input_data(0, input_ch0);
    stream.set_input_data(1, input_ch1);

    std::atomic<float> peak_ch0{0.0f};
    std::atomic<float> peak_ch1{0.0f};

    auto result = stream.start([&](AudioCallbackParamsFloat const& params) -> statusbar::Status {
        for (auto sample : params.input_buffers[0].sample) {
            float abs0 = std::fabs(sample);
            if (abs0 > peak_ch0) {
                peak_ch0 = abs0;
            }
        }
        for (auto sample : params.input_buffers[1].sample) {
            float abs1 = std::fabs(sample);
            if (abs1 > peak_ch1) {
                peak_ch1 = abs1;
            }
        }
        return statusbar::success();
    });

    EXPECT_TRUE(result.has_value());

    while (stream.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(std::fabs(peak_ch0.load() - 0.25f) < 0.001f);
    EXPECT_TRUE(std::fabs(peak_ch1.load() - 0.25f) < 0.001f);
}

TEST(mock_input, sine_wave_generator)
{
    auto device = make_mock_device("Test", "test", 1, 0);
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 1, .buffer_frames = 256};
    MockInputStream::MockConfig mock_cfg{.callback_frames = 256, .max_callbacks = 10};

    MockInputStream stream(device, config, mock_cfg);
    stream.generate_sine_wave(440.0f, 0.8f, 4800);  // 100ms of 440Hz

    std::atomic<float> peak{0.0f};
    auto result = stream.start([&](AudioCallbackParamsFloat const& params) -> statusbar::Status {
        for (auto sample : params.input_buffers[0].sample) {
            float abs = std::fabs(sample);
            if (abs > peak) {
                peak = abs;
            }
        }
        return statusbar::success();
    });

    EXPECT_TRUE(result.has_value());

    while (stream.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Peak should be close to amplitude (0.8)
    EXPECT_TRUE(peak.load() > 0.7f && peak.load() <= 0.8f);
}

// MockStreamFactory tests

TEST(mock_factory, create_streams)
{
    MockStreamFactory factory;

    auto device = make_mock_device("Test", "test", 2, 2);
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .buffer_frames = 256};

    auto out_result = factory.create_output_stream(device, config);
    EXPECT_TRUE(out_result.has_value());
    EXPECT_NE(factory.last_output_stream(), nullptr);

    auto in_result = factory.create_input_stream(device, config);
    EXPECT_TRUE(in_result.has_value());
    EXPECT_NE(factory.last_input_stream(), nullptr);
}

TEST(mock_factory, inject_error)
{
    MockStreamFactory factory;
    factory.inject_output_create_error(AudioError::DeviceNotFound);
    factory.inject_input_create_error(AudioError::InvalidConfig);

    auto device = make_mock_device("Test", "test", 2, 2);
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .buffer_frames = 256};

    auto out_result = factory.create_output_stream(device, config);
    EXPECT_FALSE(out_result.has_value());
    EXPECT_EQ(out_result.error(), make_error_code(AudioError::DeviceNotFound));

    auto in_result = factory.create_input_stream(device, config);
    EXPECT_FALSE(in_result.has_value());
    EXPECT_EQ(in_result.error(), make_error_code(AudioError::InvalidConfig));
}

// Helper function tests

TEST(mock_helpers, make_mock_device)
{
    auto device = make_mock_device("My Device", "my-uid", 4, 8);

    EXPECT_EQ(device.name, "My Device");
    EXPECT_EQ(device.uid, "my-uid");
    EXPECT_EQ(device.max_input_channels, 4);
    EXPECT_EQ(device.max_output_channels, 8);
    EXPECT_EQ(device.supported_sample_rates.size(), 3);
}

TEST(mock_helpers, make_default_mock_provider)
{
    auto provider = make_default_mock_provider();

    EXPECT_EQ(provider->device_count(), 1);

    auto default_out = provider->default_output_device();
    EXPECT_TRUE(default_out.has_value());
    EXPECT_EQ(default_out->uid, "mock-default");

    auto default_in = provider->default_input_device();
    EXPECT_TRUE(default_in.has_value());
}

// Accessor coverage tests

TEST(mock_output, stream_time_accessor)
{
    auto device = make_mock_device("Test", "test", 0, 2);
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .buffer_frames = 256};
    MockOutputStream::MockConfig mock_cfg{.callback_frames = 48, .max_callbacks = 10};

    MockOutputStream stream(device, config, mock_cfg);

    // Before running, stream_time should be 0
    EXPECT_EQ(stream.stream_time(), 0.0);

    auto result = stream.start([](AudioCallbackParamsFloat const&) -> statusbar::Status { return statusbar::success(); });
    EXPECT_TRUE(result.has_value());

    while (stream.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // After callbacks, stream_time should have advanced
    // 10 callbacks * 48 frames / 48000 Hz = 0.01 seconds
    EXPECT_TRUE(stream.stream_time() > 0.0);
}

TEST(mock_output, effective_frames_accessors)
{
    auto device = make_mock_device("Test", "test", 0, 2);
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .buffer_frames = 256};
    MockOutputStream::MockConfig mock_cfg{.callback_frames = 128};

    MockOutputStream stream(device, config, mock_cfg);

    EXPECT_EQ(stream.effective_period_frames(), 128);
    EXPECT_EQ(stream.effective_buffer_frames(), 256);
}

TEST(mock_output, total_frames_accessor)
{
    auto device = make_mock_device("Test", "test", 0, 2);
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .buffer_frames = 256};
    MockOutputStream::MockConfig mock_cfg{.callback_frames = 64, .max_callbacks = 5};

    MockOutputStream stream(device, config, mock_cfg);

    EXPECT_EQ(stream.total_frames(), 0);

    auto result = stream.start([](AudioCallbackParamsFloat const&) -> statusbar::Status { return statusbar::success(); });
    EXPECT_TRUE(result.has_value());

    while (stream.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // 5 callbacks * 64 frames = 320 total frames
    EXPECT_EQ(stream.total_frames(), 320);
}

TEST(mock_input, stream_time_accessor)
{
    auto device = make_mock_device("Test", "test", 2, 0);
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .buffer_frames = 256};
    MockInputStream::MockConfig mock_cfg{.callback_frames = 48, .max_callbacks = 10};

    MockInputStream stream(device, config, mock_cfg);

    EXPECT_EQ(stream.stream_time(), 0.0);

    auto result = stream.start([](AudioCallbackParamsFloat const&) -> statusbar::Status { return statusbar::success(); });
    EXPECT_TRUE(result.has_value());

    while (stream.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(stream.stream_time() > 0.0);
}

TEST(mock_input, effective_sample_rate_accessor)
{
    auto device = make_mock_device("Test", "test", 2, 0);
    AudioConfig config{.sample_rate = SampleRate::Rate_96000, .channels = 2, .buffer_frames = 256};

    MockInputStream stream(device, config);

    EXPECT_EQ(stream.effective_sample_rate(), 96000.0);
}

TEST(mock_input, effective_frames_accessors)
{
    auto device = make_mock_device("Test", "test", 2, 0);
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .buffer_frames = 512};
    MockInputStream::MockConfig mock_cfg{.callback_frames = 256};

    MockInputStream stream(device, config, mock_cfg);

    EXPECT_EQ(stream.effective_period_frames(), 256);
    EXPECT_EQ(stream.effective_buffer_frames(), 512);
}

TEST(mock_input, callback_count_accessor)
{
    auto device = make_mock_device("Test", "test", 2, 0);
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .buffer_frames = 256};
    MockInputStream::MockConfig mock_cfg{.callback_frames = 64, .max_callbacks = 7};

    MockInputStream stream(device, config, mock_cfg);

    EXPECT_EQ(stream.callback_count(), 0);

    auto result = stream.start([](AudioCallbackParamsFloat const&) -> statusbar::Status { return statusbar::success(); });
    EXPECT_TRUE(result.has_value());

    while (stream.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(stream.callback_count(), 7);
}

TEST(mock_input, total_frames_accessor)
{
    auto device = make_mock_device("Test", "test", 2, 0);
    AudioConfig config{.sample_rate = SampleRate::Rate_48000, .channels = 2, .buffer_frames = 256};
    MockInputStream::MockConfig mock_cfg{.callback_frames = 64, .max_callbacks = 4};

    MockInputStream stream(device, config, mock_cfg);

    EXPECT_EQ(stream.total_frames(), 0);

    auto result = stream.start([](AudioCallbackParamsFloat const&) -> statusbar::Status { return statusbar::success(); });
    EXPECT_TRUE(result.has_value());

    while (stream.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // 4 callbacks * 64 frames = 256 total frames
    EXPECT_EQ(stream.total_frames(), 256);
}

TEST(mock_provider, clear_devices)
{
    MockDeviceProvider provider;
    provider.add_device(make_mock_device("Device A", "uid-a", 2, 2));
    provider.add_device(make_mock_device("Device B", "uid-b", 2, 2));

    EXPECT_EQ(provider.device_count(), 2);

    provider.clear_devices();

    EXPECT_EQ(provider.device_count(), 0);
    auto devices = provider.enumerate_devices();
    EXPECT_EQ(devices.size(), 0);
}

// Test runner

TEST_MAIN(statusbar_audio, audio_mock_test)