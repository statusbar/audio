// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Audio tool — record, play, and passthrough raw 32-bit float audio.
//
// Record mode: captures from an input device into a raw f32 file.
//   Audio callback pushes to an AudioRing owned by main thread.
//   A file I/O thread pops from the ring and writes to disk.
//
// Play mode: reads a raw f32 file and plays to an output device.
//   A file I/O thread reads from disk and pushes to an AudioRing owned by main thread.
//   Audio callback pops from the ring and writes to the output buffers.
//
// Passthrough mode: routes input device to output device through a processing stage.
//   Input audio callback pushes to capture ring.
//   Processing thread pops from capture ring, processes, pushes to playback ring.
//   Output audio callback pops from playback ring.

#include "statusbar/audio/audio.hpp"
#include "statusbar/config/config.hpp"
#include "statusbar/dsp/dsp.hpp"
#include "statusbar/status/status.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <print>
#include <string>
#include <thread>
#include <vector>

using namespace statusbar::audio;

namespace {

std::atomic<bool> g_running{true};
void signal_handler(int)
{
    g_running = false;
}

struct Config
{
    std::string mode{"record"};
    std::string device{"default"};
    std::string output_device;  // passthrough: output device (empty = same as device)
    std::string file{"audio.raw"};
    uint32_t sample_rate{48000};
    uint32_t channels{1};
    uint32_t buffer_frames{512};
    uint32_t ring_frames{0};  // 0 = auto (buffer_frames * 16)
    std::string filter{"none"};
    double cutoff{1000.0};
    double q{0.707};
    double gain_db{0.0};
    double duration{0.0};  // 0 = unlimited (until Ctrl-C)
};

auto build_arg_specs(Config& cfg) -> statusbar::args::ArgumentSpecs
{
    statusbar::args::ArgumentSpecs specs;

    specs.add_choice(
        "mode", "Operation mode", {"record", "play", "passthrough"}, "record", [&](auto v) { cfg.mode = std::string{v}; });
    specs.add_device("device", "Audio device (name or 'default')", "default", [&](auto v) { cfg.device = std::string{v}; });
    specs.add_device("output-device", "Output device for passthrough (default: same as --device)", "", [&](auto v) {
        cfg.output_device = std::string{v};
    });
    specs.add_file(
        "file", "Raw audio file path (32-bit float, native endian)", "audio.raw", [&](auto v) { cfg.file = std::string{v}; });
    specs.add_choice(
        "sample-rate",
        "Sample rate in Hz",
        {"8000", "11025", "16000", "22050", "32000", "44100", "48000", "88200", "96000", "192000"},
        "48000",
        [&](auto v) { cfg.sample_rate = static_cast<uint32_t>(std::stoul(std::string{v})); });
    specs.add<uint32_t>("channels", "Number of channels", 1, [&](auto v) { cfg.channels = v; });
    specs.add_choice(
        "buffer-frames", "Buffer size in frames", {"64", "128", "256", "512", "1024", "2048", "4096"}, "512", [&](auto v) {
            cfg.buffer_frames = static_cast<uint32_t>(std::stoul(std::string{v}));
        });
    specs.add_choice(
        "filter",
        "Filter type for passthrough mode",
        {"none", "lowpass", "highpass", "bandpass", "notch", "peak", "lowshelf", "highshelf", "gain"},
        "none",
        [&](auto v) { cfg.filter = std::string{v}; });
    specs.add<double>("cutoff", "Filter cutoff/center frequency in Hz", 1000.0, [&](auto v) { cfg.cutoff = v; });
    specs.add<double>("q", "Filter Q factor (0.707 = Butterworth)", 0.707, [&](auto v) { cfg.q = v; });
    specs.add<double>("gain-db", "Gain in dB (for peak, shelf, and gain filters)", 0.0, [&](auto v) { cfg.gain_db = v; });
    specs.add<uint32_t>(
        "ring-frames", "Ring buffer size in frames (0 = auto: buffer-frames * 16)", 0, [&](auto v) { cfg.ring_frames = v; });
    specs.add<double>("duration", "Recording duration in seconds (0 = until Ctrl-C)", 0.0, [&](auto v) { cfg.duration = v; });

    return specs;
}

void print_usage(char const* program_name, statusbar::args::ArgumentSpecs const& specs)
{
    std::println("Usage: {} [options]", program_name);
    std::println("");
    std::println("Record, play, or passthrough raw 32-bit float audio.");
    std::println("  --mode record      : Capture from input device to file");
    std::println("  --mode play        : Play file to output device");
    std::println("  --mode passthrough : Route input to output through processing");
    std::println("");
    std::println("Passthrough filters: --filter lowpass --cutoff 1000 --q 0.707");
    std::println("  none, lowpass, highpass, bandpass, notch, peak, lowshelf, highshelf, gain");
    std::println("");
    std::println("File format: raw 32-bit float, native endian, non-interleaved channels.");
    std::println("Press Ctrl-C to stop.");
    std::println("");
    statusbar::config::default_print_usage(program_name, specs);
}

auto to_sample_rate(uint32_t rate) -> SampleRate
{
    switch (rate) {
        case 44100:
            return SampleRate::Rate_44100;
        case 96000:
            return SampleRate::Rate_96000;
        case 192000:
            return SampleRate::Rate_192000;
        default:
            return SampleRate::Rate_48000;
    }
}

// ---------------------------------------------------------------------------
// Record mode
// ---------------------------------------------------------------------------

int run_record(Config const& cfg)
{
    auto in_dev = DeviceManager::find_device(cfg.device, true);
    if (!in_dev) {
        in_dev = DeviceManager::default_input_device();
    }
    if (!in_dev) {
        std::println(stderr, "Error: No input device found");
        return 1;
    }

    // Use device's native sample rate if user didn't specify
    uint32_t const sr = (!in_dev->supported_sample_rates.empty()) ? in_dev->supported_sample_rates[0] : cfg.sample_rate;

    AudioConfig audio_config{
        .sample_rate = to_sample_rate(sr),
        .channels = cfg.channels,
        .format = SampleFormat::Float32,
        .buffer_frames = cfg.buffer_frames,
        .non_interleaved = true,
    };

    auto stream_result = InputStream::create(in_dev->uid, audio_config);
    if (!stream_result) {
        std::println(stderr, "Error: Failed to create input stream: {}", stream_result.error().message());
        return 1;
    }
    auto& stream = *stream_result;
    double const actual_rate = stream->effective_sample_rate() > 0 ? stream->effective_sample_rate() : static_cast<double>(sr);

    std::println("Recording");
    std::println("  Device:      {} ({} ch, {:.0f} Hz)", in_dev->name, cfg.channels, actual_rate);
    std::println("  File:        {}", cfg.file);
    std::println("  Buffer:      {} frames ({:.1f} ms)", cfg.buffer_frames, 1000.0 * cfg.buffer_frames / actual_rate);
    if (cfg.duration > 0) {
        std::println("  Duration:    {:.1f} s", cfg.duration);
    }

    // Ring buffer owned by main thread — audio callback pushes, file thread pops
    AudioRing<float> ring(cfg.channels, cfg.ring_frames > 0 ? cfg.ring_frames : static_cast<size_t>(cfg.buffer_frames) * 16);

    // Audio callback: push input samples to ring
    std::atomic<uint64_t> audio_frames{0};
    auto callback = [&ring, &audio_frames](AudioCallbackParamsFloat const& params) -> statusbar::Status {
        ring.push(params.input_buffers);
        if (!params.input_buffers.empty()) {
            audio_frames.fetch_add(params.input_buffers[0].sample.size(), std::memory_order_relaxed);
        }
        return statusbar::success();
    };

    // Open file
    FILE* fp = std::fopen(cfg.file.c_str(), "wb");
    if (!fp) {
        std::println(stderr, "Error: Cannot open '{}' for writing", cfg.file);
        return 1;
    }

    // Start audio
    auto start_result = stream->start(callback);
    if (!start_result) {
        std::println(stderr, "Error: Failed to start input: {}", start_result.error().message());
        std::fclose(fp);
        return 1;
    }

    std::println("\nRecording... Press Ctrl-C to stop.\n");

    // File I/O thread: pop from ring and write to disk
    std::atomic<uint64_t> file_frames{0};
    std::atomic<bool> file_thread_running{true};

    std::thread file_thread([&]() {
        // Temporary buffers for popping from ring
        size_t const chunk = cfg.buffer_frames * 4;
        std::vector<std::vector<float>> channel_bufs(cfg.channels, std::vector<float>(chunk));
        std::vector<OutputAudioBuffer<float>> pop_bufs(cfg.channels);

        while (file_thread_running.load(std::memory_order_relaxed)) {
            // Setup pop buffers
            for (uint32_t c = 0; c < cfg.channels; ++c) {
                pop_bufs[c].sample = std::span<float>(channel_bufs[c]);
            }

            size_t const n = ring.pop(pop_bufs);
            if (n == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(500));
                continue;
            }

            // Write non-interleaved: all frames of ch0, then ch1, etc.
            for (uint32_t c = 0; c < cfg.channels; ++c) {
                std::fwrite(channel_bufs[c].data(), sizeof(float), n, fp);
            }
            file_frames.fetch_add(n, std::memory_order_relaxed);
        }

        // Drain remaining
        for (uint32_t c = 0; c < cfg.channels; ++c) {
            pop_bufs[c].sample = std::span<float>(channel_bufs[c]);
        }
        while (true) {
            size_t const n = ring.pop(pop_bufs);
            if (n == 0) {
                break;
            }
            for (uint32_t c = 0; c < cfg.channels; ++c) {
                std::fwrite(channel_bufs[c].data(), sizeof(float), n, fp);
            }
            file_frames.fetch_add(n, std::memory_order_relaxed);
        }
    });

    // Main thread: display progress
    auto const start_time = std::chrono::steady_clock::now();
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto const ff = file_frames.load(std::memory_order_relaxed);
        auto const af = audio_frames.load(std::memory_order_relaxed);
        std::print(
            "  audio: {} file: {} ring: {}/{} ({:.1f}s)    \r",
            af,
            ff,
            ring.available_read(),
            ring.capacity(),
            static_cast<double>(ff) / actual_rate);

        if (cfg.duration > 0) {
            auto const elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
            if (elapsed >= cfg.duration) {
                break;
            }
        }
    }

    // Stop
    stream->stop();
    file_thread_running.store(false, std::memory_order_relaxed);
    file_thread.join();
    std::fclose(fp);

    auto const ff = file_frames.load(std::memory_order_relaxed);
    std::println("\n\nRecorded {} frames ({:.2f} s) to '{}'", ff, static_cast<double>(ff) / actual_rate, cfg.file);
    return 0;
}

// ---------------------------------------------------------------------------
// Play mode
// ---------------------------------------------------------------------------

int run_play(Config const& cfg)
{
    auto out_dev = DeviceManager::find_device(cfg.device, false);
    if (!out_dev) {
        out_dev = DeviceManager::default_output_device();
    }
    if (!out_dev) {
        std::println(stderr, "Error: No output device found");
        return 1;
    }

    uint32_t const out_channels = std::max(out_dev->max_output_channels, cfg.channels);
    uint32_t const sr = (!out_dev->supported_sample_rates.empty()) ? out_dev->supported_sample_rates[0] : cfg.sample_rate;

    AudioConfig audio_config{
        .sample_rate = to_sample_rate(sr),
        .channels = out_channels,
        .format = SampleFormat::Float32,
        .buffer_frames = cfg.buffer_frames,
        .non_interleaved = true,
    };

    auto stream_result = OutputStream::create(out_dev->uid, audio_config);
    if (!stream_result) {
        std::println(stderr, "Error: Failed to create output stream: {}", stream_result.error().message());
        return 1;
    }
    auto& stream = *stream_result;
    double const actual_rate =
        stream->effective_sample_rate() > 0 ? stream->effective_sample_rate() : static_cast<double>(cfg.sample_rate);

    // Open file
    FILE* fp = std::fopen(cfg.file.c_str(), "rb");
    if (!fp) {
        std::println(stderr, "Error: Cannot open '{}' for reading", cfg.file);
        return 1;
    }

    // Get file size
    std::fseek(fp, 0, SEEK_END);
    long const file_size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    uint64_t const total_file_frames = static_cast<uint64_t>(file_size) / (sizeof(float) * cfg.channels);

    std::println("Playing");
    std::println("  Device:      {} ({} ch, {:.0f} Hz)", out_dev->name, out_channels, actual_rate);
    std::println(
        "  File:        {} ({} frames, {:.2f} s)",
        cfg.file,
        total_file_frames,
        static_cast<double>(total_file_frames) / actual_rate);
    std::println("  Buffer:      {} frames ({:.1f} ms)", cfg.buffer_frames, 1000.0 * cfg.buffer_frames / actual_rate);

    // Ring buffer owned by main thread — file thread pushes, audio callback pops
    AudioRing<float> ring(cfg.channels, cfg.ring_frames > 0 ? cfg.ring_frames : static_cast<size_t>(cfg.buffer_frames) * 16);

    // Audio callback: pop from ring into output buffers
    std::atomic<uint64_t> audio_frames{0};
    std::atomic<bool> playback_done{false};

    auto callback = [&ring, &audio_frames, &playback_done, out_channels, file_channels = cfg.channels](
                        AudioCallbackParamsFloat const& params) -> statusbar::Status {
        if (params.output_buffers.empty()) {
            return statusbar::success();
        }

        size_t const frames = params.output_buffers[0].sample.size();

        // Pop file channels from ring
        size_t n = ring.pop(params.output_buffers);

        // Zero remaining frames (underrun or end of file)
        for (uint32_t c = 0; c < params.output_buffers.size(); ++c) {
            auto out = params.output_buffers[c].sample;
            for (size_t i = n; i < out.size(); ++i) {
                out[i] = 0.0f;
            }
        }

        // If file has fewer channels than output, duplicate ch0 to remaining
        if (file_channels < out_channels && n > 0) {
            auto ch0 = params.output_buffers[0].sample;
            for (uint32_t c = file_channels; c < out_channels && c < params.output_buffers.size(); ++c) {
                auto out = params.output_buffers[c].sample;
                for (size_t i = 0; i < n; ++i) {
                    out[i] = ch0[i];
                }
            }
        }

        audio_frames.fetch_add(frames, std::memory_order_relaxed);

        if (n == 0 && playback_done.load(std::memory_order_relaxed)) {
            g_running.store(false, std::memory_order_relaxed);
        }

        return statusbar::success();
    };

    // File I/O thread: read from disk and push to ring
    std::atomic<uint64_t> file_frames{0};
    std::atomic<bool> file_thread_running{true};

    std::thread file_thread([&]() {
        size_t const chunk = cfg.buffer_frames;
        std::vector<std::vector<float>> channel_bufs(cfg.channels, std::vector<float>(chunk));
        std::vector<InputAudioBuffer<float>> push_bufs(cfg.channels);

        while (file_thread_running.load(std::memory_order_relaxed)) {
            // Wait if ring is mostly full
            if (ring.available_write() < chunk) {
                std::this_thread::sleep_for(std::chrono::microseconds(250));
                continue;
            }

            // Read non-interleaved: all frames of ch0, then ch1, etc.
            size_t frames_read = 0;
            for (uint32_t c = 0; c < cfg.channels; ++c) {
                size_t const n = std::fread(channel_bufs[c].data(), sizeof(float), chunk, fp);
                if (c == 0) {
                    frames_read = n;
                }
            }

            if (frames_read == 0) {
                playback_done.store(true, std::memory_order_release);
                break;
            }

            // Push to ring
            for (uint32_t c = 0; c < cfg.channels; ++c) {
                push_bufs[c].sample = std::span<float const>(channel_bufs[c].data(), frames_read);
            }
            ring.push(push_bufs);
            file_frames.fetch_add(frames_read, std::memory_order_relaxed);
        }
    });

    // Pre-fill ring before starting audio
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto start_result = stream->start(callback);
    if (!start_result) {
        std::println(stderr, "Error: Failed to start output: {}", start_result.error().message());
        file_thread_running.store(false, std::memory_order_relaxed);
        file_thread.join();
        std::fclose(fp);
        return 1;
    }

    std::println("\nPlaying... Press Ctrl-C to stop.\n");

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto const ff = file_frames.load(std::memory_order_relaxed);
        auto const af = audio_frames.load(std::memory_order_relaxed);
        std::print(
            "  file: {} audio: {} ring: {}/{} ({:.1f}s / {:.1f}s)    \r",
            ff,
            af,
            ring.available_read(),
            ring.capacity(),
            static_cast<double>(af) / actual_rate,
            static_cast<double>(total_file_frames) / actual_rate);
    }

    stream->stop();
    file_thread_running.store(false, std::memory_order_relaxed);
    file_thread.join();
    std::fclose(fp);

    std::println("\n\nDone.");
    return 0;
}

// ---------------------------------------------------------------------------
// Passthrough mode
// ---------------------------------------------------------------------------

int run_passthrough(Config const& cfg)
{
    // --- Resolve input device ---
    auto in_dev = DeviceManager::find_device(cfg.device, true);
    if (!in_dev) {
        in_dev = DeviceManager::default_input_device();
    }
    if (!in_dev) {
        std::println(stderr, "Error: No input device found");
        return 1;
    }

    // --- Resolve output device ---
    std::string const out_uid = cfg.output_device.empty() ? cfg.device : cfg.output_device;
    auto out_dev = DeviceManager::find_device(out_uid, false);
    if (!out_dev) {
        out_dev = DeviceManager::default_output_device();
    }
    if (!out_dev) {
        std::println(stderr, "Error: No output device found");
        return 1;
    }

    uint32_t const sr = (!in_dev->supported_sample_rates.empty()) ? in_dev->supported_sample_rates[0] : cfg.sample_rate;
    uint32_t const out_channels = cfg.channels;

    AudioConfig in_config{
        .sample_rate = to_sample_rate(sr),
        .channels = cfg.channels,
        .format = SampleFormat::Float32,
        .buffer_frames = cfg.buffer_frames,
        .non_interleaved = true,
    };

    AudioConfig out_config{
        .sample_rate = to_sample_rate(sr),
        .channels = out_channels,
        .format = SampleFormat::Float32,
        .buffer_frames = cfg.buffer_frames,
        .non_interleaved = true,
    };

    auto in_stream_result = InputStream::create(in_dev->uid, in_config);
    if (!in_stream_result) {
        std::println(stderr, "Error: Failed to create input stream: {}", in_stream_result.error().message());
        return 1;
    }
    auto& in_stream = *in_stream_result;

    auto out_stream_result = OutputStream::create(out_dev->uid, out_config);
    if (!out_stream_result) {
        std::println(stderr, "Error: Failed to create output stream: {}", out_stream_result.error().message());
        return 1;
    }
    auto& out_stream = *out_stream_result;

    double const actual_rate =
        in_stream->effective_sample_rate() > 0 ? in_stream->effective_sample_rate() : static_cast<double>(sr);

    // Two rings: capture -> process -> playback
    size_t const ring_frames = cfg.ring_frames > 0 ? cfg.ring_frames : static_cast<size_t>(cfg.buffer_frames) * 16;

    std::println("Passthrough");
    std::println("  Input:       {} ({} ch, {:.0f} Hz)", in_dev->name, cfg.channels, actual_rate);
    std::println("  Output:      {} ({} ch, {:.0f} Hz)", out_dev->name, out_channels, actual_rate);
    std::println("  Buffer:      {} frames ({:.1f} ms)", cfg.buffer_frames, 1000.0 * cfg.buffer_frames / actual_rate);
    std::println("  Ring:        {} frames ({:.1f} ms)", ring_frames, 1000.0 * static_cast<double>(ring_frames) / actual_rate);
    std::println("  Filter:      {}", cfg.filter);
    if (cfg.filter != "none" && cfg.filter != "gain") {
        std::println("  Cutoff:      {:.1f} Hz, Q: {:.3f}", cfg.cutoff, cfg.q);
    }
    auto const is_gain_filter_type = [](std::string_view f) {
        return f == "peak" || f == "lowshelf" || f == "highshelf" || f == "gain";
    };
    if (is_gain_filter_type(cfg.filter)) {
        std::println("  Gain:        {:.1f} dB", cfg.gain_db);
    }
    if (cfg.duration > 0) {
        std::println("  Duration:    {:.1f} s", cfg.duration);
    }
    AudioRing<float> capture_ring(cfg.channels, ring_frames);
    AudioRing<float> playback_ring(out_channels, ring_frames);

    // --- Input audio callback: push captured audio to capture_ring ---
    std::atomic<uint64_t> in_frames{0};
    auto in_callback = [&capture_ring, &in_frames](AudioCallbackParamsFloat const& params) -> statusbar::Status {
        capture_ring.push(params.input_buffers);
        if (!params.input_buffers.empty()) {
            in_frames.fetch_add(params.input_buffers[0].sample.size(), std::memory_order_relaxed);
        }
        return statusbar::success();
    };

    // --- Output audio callback: pop from playback_ring to output ---
    std::atomic<uint64_t> out_frames{0};
    auto out_callback = [&playback_ring, &out_frames, out_channels](AudioCallbackParamsFloat const& params) -> statusbar::Status {
        if (params.output_buffers.empty()) {
            return statusbar::success();
        }

        size_t const frames = params.output_buffers[0].sample.size();
        size_t const n = playback_ring.pop(params.output_buffers);

        // Zero remaining frames on underrun
        for (uint32_t c = 0; c < params.output_buffers.size(); ++c) {
            auto out = params.output_buffers[c].sample;
            for (size_t i = n; i < out.size(); ++i) {
                out[i] = 0.0f;
            }
        }

        out_frames.fetch_add(frames, std::memory_order_relaxed);
        return statusbar::success();
    };

    // --- Set up DSP filter (one per output channel) ---
    using statusbar::dsp::BiQuad;
    using statusbar::dsp::FilterParams;
    using statusbar::dsp::Gain;

    std::vector<BiQuad<float>> biquads(out_channels);
    std::vector<Gain<float>> gains(out_channels);
    bool const use_biquad = (cfg.filter != "none" && cfg.filter != "gain");
    bool const use_gain = (cfg.filter == "gain");

    FilterParams<> const fp{
        .sample_rate_recip = 1.0 / actual_rate,
        .frequency = cfg.cutoff,
        .q = cfg.q,
        .gain_db = cfg.gain_db,
    };

    for (uint32_t c = 0; c < out_channels; ++c) {
        if (cfg.filter == "lowpass") {
            biquads[c].coeffs.calculate_lowpass(fp);
        } else if (cfg.filter == "highpass") {
            biquads[c].coeffs.calculate_highpass(fp);
        } else if (cfg.filter == "bandpass") {
            biquads[c].coeffs.calculate_bandpass(fp);
        } else if (cfg.filter == "notch") {
            biquads[c].coeffs.calculate_notch(fp);
        } else if (cfg.filter == "peak") {
            biquads[c].coeffs.calculate_peak(fp);
        } else if (cfg.filter == "lowshelf") {
            biquads[c].coeffs.calculate_lowshelf(fp);
        } else if (cfg.filter == "highshelf") {
            biquads[c].coeffs.calculate_highshelf(fp);
        } else {
            biquads[c].coeffs.set_bypass();
        }

        if (use_gain) {
            gains[c].coeffs.set_amplitude_db(cfg.gain_db);
            gains[c].coeffs.set_time_constant(actual_rate, 0.01);
            gains[c].state.snap_to_target(gains[c].coeffs);
        }
    }

    // --- Processing thread: capture_ring -> process -> playback_ring ---
    std::atomic<bool> process_running{true};
    std::atomic<uint64_t> proc_frames{0};

    auto process_one_chunk = [&](std::vector<std::vector<float>>& pop_bufs,
                                 std::vector<std::vector<float>>& push_bufs,
                                 std::vector<InputAudioBuffer<float>>& push_spans,
                                 size_t n) {
        // Process: copy input to output, then apply filter in-place
        for (uint32_t c = 0; c < out_channels; ++c) {
            uint32_t const src_ch = (c < cfg.channels) ? c : 0;
            std::copy_n(pop_bufs[src_ch].data(), n, push_bufs[c].data());

            auto samples = std::span<float>(push_bufs[c].data(), n);
            if (use_biquad) {
                statusbar::dsp::process_in_place(biquads[c], samples);
            } else if (use_gain) {
                statusbar::dsp::process_in_place(gains[c], samples);
            }
        }

        // Wait for space in playback ring
        while (process_running.load(std::memory_order_relaxed) && playback_ring.available_write() < n) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        // Push to playback ring
        for (uint32_t c = 0; c < out_channels; ++c) {
            push_spans[c].sample = std::span<float const>(push_bufs[c].data(), n);
        }
        playback_ring.push(push_spans);
        proc_frames.fetch_add(n, std::memory_order_relaxed);
    };

    std::thread process_thread([&]() {
        size_t const chunk = cfg.buffer_frames;
        std::vector<std::vector<float>> pop_bufs(cfg.channels, std::vector<float>(chunk));
        std::vector<OutputAudioBuffer<float>> pop_spans(cfg.channels);
        std::vector<std::vector<float>> push_bufs(out_channels, std::vector<float>(chunk));
        std::vector<InputAudioBuffer<float>> push_spans(out_channels);

        while (process_running.load(std::memory_order_relaxed)) {
            for (uint32_t c = 0; c < cfg.channels; ++c) {
                pop_spans[c].sample = std::span<float>(pop_bufs[c]);
            }
            size_t const n = capture_ring.pop(pop_spans);
            if (n == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(250));
                continue;
            }
            process_one_chunk(pop_bufs, push_bufs, push_spans, n);
        }
    });

    // Start output first (it will output silence until process thread fills the ring)
    auto out_start = out_stream->start(out_callback);
    if (!out_start) {
        std::println(stderr, "Error: Failed to start output: {}", out_start.error().message());
        process_running.store(false, std::memory_order_relaxed);
        process_thread.join();
        return 1;
    }

    // Start input
    auto in_start = in_stream->start(in_callback);
    if (!in_start) {
        std::println(stderr, "Error: Failed to start input: {}", in_start.error().message());
        out_stream->stop();
        process_running.store(false, std::memory_order_relaxed);
        process_thread.join();
        return 1;
    }

    std::println("\nPassthrough running... Press Ctrl-C to stop.\n");

    auto const start_time = std::chrono::steady_clock::now();
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto const inf = in_frames.load(std::memory_order_relaxed);
        auto const pf = proc_frames.load(std::memory_order_relaxed);
        auto const outf = out_frames.load(std::memory_order_relaxed);
        std::print(
            "  in: {} proc: {} out: {} cap_ring: {}/{} play_ring: {}/{} ({:.1f}s)    \r",
            inf,
            pf,
            outf,
            capture_ring.available_read(),
            capture_ring.capacity(),
            playback_ring.available_read(),
            playback_ring.capacity(),
            static_cast<double>(outf) / actual_rate);

        if (cfg.duration > 0) {
            auto const elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
            if (elapsed >= cfg.duration) {
                break;
            }
        }
    }

    in_stream->stop();
    out_stream->stop();
    process_running.store(false, std::memory_order_relaxed);
    process_thread.join();

    auto const outf = out_frames.load(std::memory_order_relaxed);
    std::println("\n\nPassthrough stopped after {:.2f} s", static_cast<double>(outf) / actual_rate);
    return 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    Config cfg;
    auto specs = build_arg_specs(cfg);
    auto cli_result = statusbar::config::parse_cli_args(argc, argv, specs, print_usage, "statusbar-audio-tool");
    if (!cli_result) {
        return statusbar::config::handled_builtin_command(cli_result) ? 0 : 1;
    }

    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // no SA_RESTART
    sigaction(SIGINT, &sa, nullptr);

    // Show relevant devices
    auto devices = DeviceManager::enumerate_devices();
    bool const show_input = (cfg.mode == "record" || cfg.mode == "passthrough");
    bool const show_output = (cfg.mode == "play" || cfg.mode == "passthrough");
    if (show_input) {
        std::println("Available input devices:");
        for (auto const& d : devices) {
            if (d.max_input_channels > 0) {
                std::println("  [{}] {} ({} ch{})", d.uid, d.name, d.max_input_channels, d.is_default_input ? " *default*" : "");
            }
        }
    }
    if (show_output) {
        std::println("Available output devices:");
        for (auto const& d : devices) {
            if (d.max_output_channels > 0) {
                std::println("  [{}] {} ({} ch{})", d.uid, d.name, d.max_output_channels, d.is_default_output ? " *default*" : "");
            }
        }
    }
    std::println("");

    if (cfg.mode == "record") {
        return run_record(cfg);
    }
    if (cfg.mode == "passthrough") {
        return run_passthrough(cfg);
    }
    return run_play(cfg);
}
