// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Minimal test: output a 2600 Hz sine wave to the default output device.

#include "statusbar/audio/audio.hpp"
#include "statusbar/dsp/dsp.hpp"
#include "statusbar/status/status.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <print>
#include <thread>

using namespace statusbar::audio;
using namespace statusbar::dsp;

static std::atomic<bool> g_running{true};
static void sig(int)
{
    g_running = false;
}

int main()
{
    std::signal(SIGINT, sig);

    // List devices
    auto devices = DeviceManager::enumerate_devices();
    std::println("Devices:");
    for (auto const& d : devices) {
        std::println(
            "  {} (in:{} out:{}{}{})",
            d.name,
            d.max_input_channels,
            d.max_output_channels,
            d.is_default_input ? " *in*" : "",
            d.is_default_output ? " *out*" : "");
    }

    auto out_dev = DeviceManager::default_output_device();
    if (!out_dev) {
        std::println(stderr, "No default output device");
        return 1;
    }

    uint32_t const sr = (!out_dev->supported_sample_rates.empty()) ? out_dev->supported_sample_rates[0] : 44100;
    uint32_t const channels = std::max(out_dev->max_output_channels, 1u);

    std::println("\nOutput: {} ({} ch, {} Hz)", out_dev->name, channels, sr);

    auto to_sr = [](uint32_t r) {
        switch (r) {
            case 44100:
                return SampleRate::Rate_44100;
            case 96000:
                return SampleRate::Rate_96000;
            case 192000:
                return SampleRate::Rate_192000;
            default:
                return SampleRate::Rate_48000;
        }
    };

    AudioConfig config{
        .sample_rate = to_sr(sr),
        .channels = channels,
        .format = SampleFormat::Float32,
        .buffer_frames = 512,
        .non_interleaved = true,
    };

    auto result = OutputStream::create(out_dev->uid, config);
    if (!result) {
        std::println(stderr, "Failed to create output stream: {}", result.error().message());
        return 1;
    }

    auto& stream = *result;
    std::atomic<uint64_t> total_frames{0};

    // Setup oscillator at 2600 Hz, -12 dB
    Oscillator<float> osc;
    osc.coeffs_.set_amplitude(0.25f, 0);
    osc.state_.set_frequency(
        FrequencyParameters<double>{.sample_rate_recip = 1.0 / sr, .frequency = 2600.0, .phase_in_radians = 0.0});

    auto callback = [&](AudioCallbackParamsFloat const& params) -> statusbar::Status {
        if (params.output_buffers.empty()) {
            return statusbar::success();
        }

        auto ch0 = params.output_buffers[0].sample;
        size_t const n = ch0.size();

        for (size_t i = 0; i < n; ++i) {
            ch0[i] = osc(0.0f);
        }

        // Copy to other channels
        for (size_t c = 1; c < params.output_buffers.size(); ++c) {
            auto out = params.output_buffers[c].sample;
            for (size_t i = 0; i < std::min(n, out.size()); ++i) {
                out[i] = ch0[i];
            }
        }

        total_frames.fetch_add(n, std::memory_order_relaxed);
        return statusbar::success();
    };

    auto start = stream->start(callback);
    if (!start) {
        std::println(stderr, "Failed to start: {}", start.error().message());
        return 1;
    }

    std::println("Playing 2600 Hz tone... Ctrl-C to stop.\n");

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto f = total_frames.load(std::memory_order_relaxed);
        std::print("  {} frames ({:.1f}s)\r", f, static_cast<double>(f) / sr);
    }

    stream->stop();
    std::println("\nStopped.");
    return 0;
}
