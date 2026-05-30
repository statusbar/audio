// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Audio input example - demonstrates audio capture using InputStream

#include "statusbar/audio/audio.hpp"
#include "statusbar/status/status.hpp"

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

int main()
{
    std::println("Audio Input Capture Example\n");

    // Find default input device
    auto device = DeviceManager::default_input_device();
    if (!device) {
        std::println(stderr, "Error: No default input device found");
        return 1;
    }

    std::println("Using input device: {}", device->name);
    std::println("  Channels: {}", device->max_input_channels);
    std::println("  Sample rates: {} Hz", device->supported_sample_rates.size());

    // Configure audio input - use 1 channel to match device capability
    AudioConfig config{
        .sample_rate = SampleRate::Rate_48000,
        .channels = 1,
        .format = SampleFormat::Float32,
        .buffer_frames = 512,
        .non_interleaved = true};

    // Create input stream
    auto stream_result = InputStream::create(device->uid, config);
    if (!stream_result) {
        std::println(stderr, "Error: Failed to create input stream: {}", stream_result.error().message());
        return 1;
    }

    auto& stream = *stream_result;

    // Setup capture state
    struct CaptureState
    {
        uint64_t total_frames{0};
        float peak_level{0.0f};
        float rms_sum{0.0f};
        uint64_t rms_count{0};
    };

    CaptureState state;

    // Audio callback that analyzes input
    auto callback = [&state](AudioCallbackParamsFloat const& params) -> statusbar::Status {
        // Analyze input signal (1 channel)
        for (auto sample : params.input_buffers[0].sample) {
            float abs_val = std::abs(sample);

            // Update peak
            if (abs_val > state.peak_level) {
                state.peak_level = abs_val;
            }

            // Update RMS
            state.rms_sum += sample * sample;
            state.rms_count++;
        }

        state.total_frames += params.input_buffers[0].sample.size();

        // Print status every 100ms
        if (state.total_frames % 4800 == 0) {
            float rms = std::sqrt(state.rms_sum / state.rms_count);
            float peak_db = 20.0f * std::log10(state.peak_level + 1e-10f);
            float rms_db = 20.0f * std::log10(rms + 1e-10f);

            std::println("  [{:.1f}s] Peak: {:6.2f} dB  RMS: {:6.2f} dB", params.stream_time, peak_db, rms_db);

            // Reset for next interval
            state.peak_level = 0.0f;
            state.rms_sum = 0.0f;
            state.rms_count = 0;
        }

        return statusbar::success();  // Continue
    };

    // Start capture
    auto start_result = stream->start(callback);
    if (!start_result) {
        std::println(stderr, "Error: Failed to start input stream: {}", start_result.error().message());
        return 1;
    }

    std::println("\nCapturing audio input... (will run for 5 seconds)\n");

    // Run for 5 seconds
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Stop capture
    stream->stop();

    std::println("\nCapture complete!");
    std::println("  Total frames captured: {}", state.total_frames);
    std::println("  Duration: {:.2f} seconds", state.total_frames / 48000.0);

    return 0;
}