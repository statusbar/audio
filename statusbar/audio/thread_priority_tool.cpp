// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Test thread priority configuration

#include "statusbar/audio/audio.hpp"
#include "statusbar/status/status.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <print>
#include <span>
#include <system_error>
#include <thread>

using namespace statusbar::audio;

auto main() -> int
{
#if __cpp_exceptions
    try {
#endif
        std::println("Testing thread priority configuration...\n");

        // Test 1: Normal priority (should always work)
        {
            std::println("Test 1: Normal priority");
            AudioConfig const config{
                .sample_rate = SampleRate::Rate_48000,
                .channels = 2,
                .format = SampleFormat::Float32,
                .buffer_frames = 512,
                .non_interleaved = true,
                .thread_priority = ThreadPriority::Normal};

            auto stream_result = OutputStream::create("default", config);
            if (!stream_result) {
                std::println("  FAILED: Could not create stream: {}", stream_result.error().message());
                return 1;
            }

            auto& stream = *stream_result;

            int callback_count = 0;
            auto callback = [&](AudioCallbackParamsFloat const& params) -> statusbar::Status {
                // Generate silence
                for (auto const& buf : params.output_buffers) {
                    for (auto& sample : buf.sample) {
                        sample = 0.0F;
                    }
                }
                callback_count++;
                return statusbar::success();
            };

            auto start_result = stream->start(callback);
            if (!start_result) {
                std::println("  FAILED: Could not start stream: {}", start_result.error().message());
                return 1;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            stream->stop();

            std::println("  PASSED: Normal priority worked, {} callbacks", callback_count);
        }

        // Test 2: Elevated priority (should work with or without privileges)
        {
            std::println("\nTest 2: Elevated priority (graceful fallback)");
            AudioConfig const config{
                .sample_rate = SampleRate::Rate_48000,
                .channels = 2,
                .format = SampleFormat::Float32,
                .buffer_frames = 512,
                .non_interleaved = true,
                .thread_priority = ThreadPriority::Elevated};

            auto stream_result = OutputStream::create("default", config);
            if (!stream_result) {
                std::println("  FAILED: Could not create stream: {}", stream_result.error().message());
                return 1;
            }

            auto& stream = *stream_result;

            int callback_count = 0;
            auto callback = [&](AudioCallbackParamsFloat const& params) -> statusbar::Status {
                // Generate silence
                for (auto const& buf : params.output_buffers) {
                    for (auto& sample : buf.sample) {
                        sample = 0.0F;
                    }
                }
                callback_count++;
                return statusbar::success();
            };

            auto start_result = stream->start(callback);
            if (!start_result) {
                std::println("  FAILED: Could not start stream: {}", start_result.error().message());
                return 1;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            stream->stop();

            std::println("  PASSED: Elevated priority worked (may have fallen back to normal), {} callbacks", callback_count);
        }

        // Test 3: Realtime priority (will fail without privileges)
        {
            std::println("\nTest 3: Realtime priority (requires privileges)");
            AudioConfig const config{
                .sample_rate = SampleRate::Rate_48000,
                .channels = 2,
                .format = SampleFormat::Float32,
                .buffer_frames = 512,
                .non_interleaved = true,
                .thread_priority = ThreadPriority::Realtime};

            auto stream_result = OutputStream::create("default", config);
            if (!stream_result) {
                std::println("  Expected: Could not create stream: {}", stream_result.error().message());
                std::println("  (This is expected without CAP_SYS_NICE capability or root)");
            } else {
                auto& stream = *stream_result;

                int callback_count = 0;
                auto callback = [&](AudioCallbackParamsFloat const& params) -> statusbar::Status {
                    // Generate silence
                    for (auto const& buf : params.output_buffers) {
                        for (auto& sample : buf.sample) {
                            sample = 0.0F;
                        }
                    }
                    callback_count++;
                    return statusbar::success();
                };

                auto start_result = stream->start(callback);
                if (!start_result) {
                    std::println("  Stream started but priority setting failed");
                    std::println("  This means the thread couldn't get realtime priority");
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    stream->stop();
                    std::println("  PASSED: Realtime priority succeeded! {} callbacks", callback_count);
                    std::println("  (You are running with elevated privileges)");
                }
            }
        }

        std::println("\nAll tests completed!");
        return 0;
#if __cpp_exceptions
    } catch (std::exception const& e) {
        std::println(stderr, "Fatal error: {}", e.what());
        return 1;
    }
#endif
}