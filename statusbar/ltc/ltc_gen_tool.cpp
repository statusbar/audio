// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// LTC Generator Command-Line Tool
// Generates SMPTE LTC audio samples to raw binary or WAV file
// Supports multiple frame rates including drop frame

#include "statusbar/audio/audio.hpp"
#include "statusbar/buffer/buffer.hpp"
#include "statusbar/buffer/stream_utils.hpp"
#include "statusbar/config/config.hpp"
#include "statusbar/ltc/ltc.hpp"
#include "statusbar/status/status.hpp"
#include "statusbar/status/throw_or_abort.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace {

/// Output format for generated audio
enum class OutputFormat
{
    RawFloat32,  ///< Raw 32-bit float samples (little-endian)
    Wav16        ///< WAV file with 16-bit PCM, mono
};

struct Config
{
    std::string start_timecode;
    uint32_t sample_rate{48000};
    double duration_seconds{10.0};
    std::string output_file;  ///< Empty means use default based on format
    double rise_time_us{40.0};
    bool play_audio{false};
    std::string audio_device{"default"};
    bool use_realtime_clock{false};
    statusbar::ltc::FrameRate frame_rate{statusbar::ltc::FrameRate::Rate_30_NDF};
    std::string time_offset;  ///< Time-of-day offset HH:MM:SS that maps to SMPTE 00:00:00:00
    OutputFormat format{OutputFormat::RawFloat32};
    bool quiet{false};  ///< Suppress generation info output
};

/// Write a minimal WAV file header for 16-bit mono PCM
/// @param out Output stream (must be seekable)
/// @param sample_rate Sample rate in Hz
/// @param num_samples Number of samples (will be written to header)
void write_wav_header(std::ostream& out, uint32_t sample_rate, uint32_t num_samples)
{
    // WAV format constants
    uint16_t constexpr num_channels = 1;
    uint16_t constexpr bits_per_sample = 16;
    uint32_t constexpr bytes_per_sample = bits_per_sample / 8;
    uint32_t const byte_rate = sample_rate * num_channels * bytes_per_sample;
    uint16_t constexpr block_align = num_channels * bytes_per_sample;
    uint32_t const data_size = num_samples * bytes_per_sample;
    uint32_t const file_size = 36 + data_size;  // RIFF header size - 8 + data_size
    uint32_t constexpr fmt_size = 16;           // PCM format size
    uint16_t constexpr audio_format = 1;        // PCM

    // Build 44-byte WAV header using buffer serializer
    // WAV format uses little-endian, which is native on x86/ARM
    statusbar::BufferSerializerBuilderWithStorage<44> header;

    // RIFF header (12 bytes)
    header.append(std::array<char, 4>{'R', 'I', 'F', 'F'});
    header.append(file_size);
    header.append(std::array<char, 4>{'W', 'A', 'V', 'E'});

    // fmt subchunk (24 bytes)
    header.append(std::array<char, 4>{'f', 'm', 't', ' '});
    header.append(fmt_size);
    header.append(audio_format);
    header.append(num_channels);
    header.append(sample_rate);
    header.append(byte_rate);
    header.append(block_align);
    header.append(bits_per_sample);

    // data subchunk header (8 bytes)
    header.append(std::array<char, 4>{'d', 'a', 't', 'a'});
    header.append(data_size);

    // Write the complete header in one call
    auto const span = header.get_span();
    statusbar::stream_write(out, span);
}

/// Convert float sample [-1.0, 1.0] to 16-bit signed integer
int16_t float_to_int16(float sample)
{
    // Clamp to [-1.0, 1.0]
    float clamped = std::clamp(sample, -1.0f, 1.0f);
    // Scale to 16-bit range
    return static_cast<int16_t>(clamped * 32767.0f);
}

/// Build argument specifications with bindings to populate Config struct
auto build_arg_specs(Config& config) -> statusbar::args::ArgumentSpecs
{
    statusbar::args::ArgumentSpecs specs;

    // String: timecode
    specs.add<std::string_view>(
        "timecode", "Start timecode in HH:MM:SS:FF format", "00:00:00:00", [&](auto v) { config.start_timecode = std::string{v}; });

    // Choice: frame rate (needs custom parsing)
    specs.add_choice("fps", "Frame rate", {"23.976", "24", "25", "29.97df", "29.97", "30df", "30ndf"}, "30ndf", [&](auto v) {
        auto rate = statusbar::ltc::parse_frame_rate(v);
        if (!rate) {
            std::println(stderr, "Error: Invalid frame rate '{}'. Use: 23.976, 24, 25, 29.97df, 29.97, 30df, or 30ndf", v);
            statusbar::throw_or_abort(std::errc::invalid_argument);
        }
        config.frame_rate = *rate;
    });

    // Choice: sample rate (parsed as integer from string choice)
    specs.add_choice("rate", "Audio sample rate", {"44100", "48000", "96000"}, "48000", [&](auto v) {
        config.sample_rate = static_cast<uint32_t>(std::stoul(std::string{v}));
    });

    // Float: duration
    specs.add<double>("duration", "Duration in seconds", 10.0, [&](auto v) { config.duration_seconds = v; });

    // File: output
    specs.add_file("output", "Output filename", "ltc_output.raw", [&](auto v) { config.output_file = std::string{v}; });

    // Choice: output format
    specs.add_choice("format", "Output format", {"raw", "wav"}, "raw", [&](auto v) {
        if (v == "raw") {
            config.format = OutputFormat::RawFloat32;
        } else if (v == "wav") {
            config.format = OutputFormat::Wav16;
        } else {
            std::println(stderr, "Error: Invalid output format '{}'. Use: raw, wav", v);
            statusbar::throw_or_abort(std::errc::invalid_argument);
        }
    });

    // Float: rise time
    specs.add<double>("rise-time", "Rise/fall time in microseconds (25-250)", 40.0, [&](auto v) { config.rise_time_us = v; });

    // Flag: play audio
    specs.add_flag("play", "Play audio through speakers", [&](auto v) { config.play_audio = v; });

    // Device: audio device
    specs.add_device("device", "Audio device name", "default", [&](auto v) { config.audio_device = std::string{v}; });

    // Flag: realtime clock
    specs.add_flag(
        "realtime-clock", "Use CLOCK_REALTIME for timecode (requires --play)", [&](auto v) { config.use_realtime_clock = v; });

    // String: time offset
    specs.add<std::string_view>("time-offset", "Time-of-day offset HH:MM:SS mapping to SMPTE 00:00:00:00", "", [&](auto v) {
        config.time_offset = std::string{v};
    });

    // Flag: quiet
    specs.add_flag("quiet", "Suppress generation info output", [&](auto v) { config.quiet = v; });

    return specs;
}

void print_usage(char const* program_name, statusbar::args::ArgumentSpecs const& specs)
{
    std::println("Usage: {} [options]", program_name);
    std::println("\nOptions:");

    std::string help;
    specs.format_help_to(std::back_inserter(help));
    std::print("{}", help);

    std::println("\nExamples:");
    std::println("  {} --timecode=01:30:45:15 --fps=29.97df --rate=48000 --duration=5.0 --output=ltc.raw", program_name);
    std::println("  {} --timecode=00:00:00:00 --fps=25 --duration=10.0 --format=wav --output=ltc.wav", program_name);
    std::println("  {} --fps=25 --duration=10.0 --play", program_name);
    std::println("  {} --play --realtime-clock --fps=29.97df", program_name);
    std::println("  {} --play --realtime-clock --time-offset=09:00:00 --fps=30ndf", program_name);
    std::println("  {} --play --device=\"MacBook Pro Speakers\" --fps=24", program_name);
    std::println("  {} --config=my_settings.toml --output=override.wav", program_name);
    std::println("\nConfiguration File:");
    std::println("  Settings can be stored in a TOML file and loaded with --config=FILE.");
    std::println("  CLI arguments override config file values. Example config:");
    std::println("    timecode = \"00:00:00:00\"");
    std::println("    fps = \"30ndf\"");
    std::println("    rate = 48000");
    std::println("    duration = 10.0");
    std::println("    format = \"raw\"");
    std::println("    play = false");
    std::println("\nOutput Formats:");
    std::println("  raw - Raw 32-bit float samples (little-endian) suitable for");
    std::println("        import into Audacity: File > Import > Raw Data, 32-bit float, 1 channel.");
    std::println("  wav - Standard WAV file with 16-bit PCM mono audio. Can be opened directly");
    std::println("        by most audio software without manual import settings.");
    std::println("\nDrop Frame Note:");
    std::println("  Drop frame timecodes use semicolon separator (01:00:00;00) and skip");
    std::println("  frames 0 and 1 at the start of each minute except 0, 10, 20, 30, 40, 50.");
    std::println("\nRealtime Clock Mode:");
    std::println("  When --realtime-clock is used, the timecode is derived from CLOCK_REALTIME.");
    std::println("  The servo mechanism compensates for drift between the system clock and audio clock.");
    std::println("  Use --time-offset to specify a time-of-day that maps to SMPTE 00:00:00:00.");
    std::println("  For example, --time-offset=09:00:00 means 9:00 AM = TC 00:00:00:00.");
}

// Generate LTC to file
int generate_to_file(Config const& config, statusbar::ltc::Generator& gen, statusbar::ltc::Timecode tc, size_t total_frames)
{
    std::ofstream output(config.output_file, std::ios::binary);
    if (!output) {
        std::println(stderr, "Error: Cannot open output file: {}", config.output_file);
        return 1;
    }

    bool const is_wav = config.format == OutputFormat::Wav16;
    size_t const samples_per_frame = gen.samples_per_frame();
    size_t const total_samples = total_frames * samples_per_frame;

    // Write WAV header if needed (will write placeholder, then update at end)
    if (is_wav) {
        write_wav_header(output, config.sample_rate, static_cast<uint32_t>(total_samples));
    }

    // Generate samples frame by frame
    std::vector<float> frame_samples;
    frame_samples.reserve(samples_per_frame);

    // Buffer for 16-bit samples when writing WAV
    std::vector<int16_t> int16_samples;
    if (is_wav) {
        int16_samples.reserve(samples_per_frame);
    }

    auto write_frame_samples = [&](std::vector<float> const& samples) {
        if (is_wav) {
            int16_samples.clear();
            for (float sample : samples) {
                int16_samples.push_back(float_to_int16(sample));
            }
            statusbar::stream_write(output, int16_samples);
        } else {
            statusbar::stream_write(output, samples);
        }
    };

    for (size_t frame_idx = 0; frame_idx < total_frames; ++frame_idx) {
        gen.generate_frame(tc, frame_samples);
        write_frame_samples(frame_samples);
        tc.increment_frame();

        uint8_t fps = statusbar::ltc::frames_per_second(config.frame_rate);
        if ((frame_idx + 1) % fps == 0) {
            double progress = (frame_idx + 1) * 100.0 / total_frames;
            std::println("  Progress: {:.1f}%", progress);
        }
    }

    output.close();

    // Report file size
    auto file_size = std::filesystem::file_size(config.output_file);
    std::println("\nGeneration complete!");
    std::println("  File size: {} bytes ({:.2f} MB)", file_size, file_size / (1024.0 * 1024.0));

    if (is_wav) {
        std::println("\nWAV file ready!");
        std::println("  Format: 16-bit PCM, mono, {} Hz", config.sample_rate);
        std::println("  Open directly in any audio software.");
    } else {
        std::println("\nTo import into Audacity:");
        std::println("  1. File > Import > Raw Data...");
        std::println("  2. Select: 32-bit float, Little-endian, 1 channel, {} Hz", config.sample_rate);
    }

    return 0;
}

// Play LTC audio with realtime clock and servo
int play_with_realtime_clock(
    Config const& config, statusbar::ltc::Generator::SampleRate rate, statusbar::audio::OutputStream& stream)
{
    std::println("Mode: Realtime clock with servo compensation");

    // Parse time offset if provided
    int32_t time_offset_seconds = 0;
    if (!config.time_offset.empty()) {
        auto offset = statusbar::ltc::parse_time_offset(config.time_offset);
        if (!offset) {
            std::println(stderr, "Error: Invalid time offset format. Use HH:MM:SS (e.g., 09:00:00)");
            return 1;
        }
        time_offset_seconds = *offset;
        std::println("Time offset: {} (maps to SMPTE 00:00:00:00)", config.time_offset);
    }

    // Create servo generator
    statusbar::ltc::ServoGenerator servo_gen(rate, config.frame_rate, config.rise_time_us);

    // Initialize with current realtime (with offset if specified)
    auto time_info = statusbar::ltc::get_local_time_info();
    auto initial_tc =
        statusbar::ltc::Timecode::from_realtime(config.frame_rate, time_info.local_time, time_info.millis, time_offset_seconds);
    servo_gen.set_timecode(initial_tc, time_info.epoch_seconds);

    std::println("Initial timecode: {} (from CLOCK_REALTIME)", initial_tc.to_string());

    // Setup state
    statusbar::ltc::ServoPlaybackState state(
        &servo_gen, config.sample_rate, config.frame_rate, time_info.epoch_seconds, time_offset_seconds);
    state.sample_buffer.reserve(512);

    // Start audio playback
    auto start_result = stream.start([&state](statusbar::audio::AudioCallbackParamsFloat const& params) {
        auto ti = statusbar::ltc::get_local_time_info();
        return statusbar::ltc::process_servo_callback(state, ti.local_time, ti.millis, ti.epoch_seconds, params);
    });
    if (!start_result) {
        std::println(stderr, "Error: Failed to start audio stream: {}", start_result.error().message());
        return 1;
    }

    std::println("Playing LTC audio from realtime clock... (press Ctrl+C to stop)");

    // Monitor playback
    auto start_monotonic = std::chrono::steady_clock::now();
    while (state.is_running() && stream.is_running()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto elapsed = std::chrono::steady_clock::now() - start_monotonic;
        auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

        // Status update every second
        auto current_tc = servo_gen.current_timecode();
        double phase_adj = servo_gen.phase_adjustment();
        std::println(
            "  [{:3}s] TC: {}  Servo: {:.6f}  Samples: {}",
            elapsed_sec,
            current_tc.to_string(),
            phase_adj,
            state.total_samples_generated);

        // Check duration
        if (elapsed_sec >= static_cast<int64_t>(config.duration_seconds)) {
            state.set_running(false);
            break;
        }
    }

    stream.stop();
    std::println("\nPlayback complete!");
    return 0;
}

// Play LTC audio with fixed timecode
int play_with_fixed_timecode(
    Config const& config,
    statusbar::ltc::Generator& gen,
    statusbar::ltc::Timecode tc,
    size_t total_frames,
    size_t samples_per_frame,
    statusbar::audio::OutputStream& stream)
{
    // Setup state for audio callback
    statusbar::ltc::FixedPlaybackState state(&gen, tc, total_frames);
    state.frame_buffer.reserve(samples_per_frame);

    // Start audio playback
    auto start_result = stream.start([&state](statusbar::audio::AudioCallbackParamsFloat const& params) {
        return statusbar::ltc::process_fixed_callback(state, params);
    });
    if (!start_result) {
        std::println(stderr, "Error: Failed to start audio stream: {}", start_result.error().message());
        return 1;
    }

    std::println("Playing LTC audio... (press Ctrl+C to stop)");

    // Wait for playback to finish
    uint8_t fps = statusbar::ltc::frames_per_second(gen.frame_rate());
    while (!state.is_finished() && stream.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Progress indicator
        size_t current_frames = state.frames_generated;
        if (current_frames > 0 && current_frames % fps == 0) {
            double progress = static_cast<double>(current_frames) * 100.0 / total_frames;
            std::println("  Progress: {:.1f}% (TC: {})", progress, state.current_tc.to_string());
        }
    }

    stream.stop();
    std::println("\nPlayback complete!");
    return 0;
}

/// Validation result containing parsed/validated values
struct ValidationResult
{
    statusbar::ltc::Timecode timecode;
    statusbar::ltc::Generator::SampleRate sample_rate;
};

/// Generation info calculated from config and generator
struct GenerationInfo
{
    size_t samples_per_frame;
    size_t total_frames;
    size_t total_samples;
};

/// Calculate generation info from config and generator
GenerationInfo calculate_generation_info(Config const& config, statusbar::ltc::Generator const& gen)
{
    GenerationInfo info;
    info.samples_per_frame = gen.samples_per_frame();
    double fps = statusbar::ltc::actual_frame_rate(config.frame_rate);
    info.total_frames = static_cast<size_t>(config.duration_seconds * fps + 0.5);
    info.total_samples = info.total_frames * info.samples_per_frame;
    return info;
}

/// Validate configuration and parse derived values
/// @param config Configuration to validate
/// @param result Output for parsed/validated values
/// @return true if valid, false if validation failed (error already printed)
bool validate_config(Config& config, ValidationResult& result)
{
    // Set default output filename based on format if not specified
    if (config.output_file.empty()) {
        config.output_file = (config.format == OutputFormat::Wav16) ? "ltc_output.wav" : "ltc_output.raw";
    }

    // Validate realtime clock mode
    if (config.use_realtime_clock && !config.play_audio) {
        std::println(stderr, "Error: --realtime-clock requires --play (audio playback mode)");
        return false;
    }

    // Validate time offset (requires realtime clock mode)
    if (!config.time_offset.empty() && !config.use_realtime_clock) {
        std::println(stderr, "Error: --time-offset requires --realtime-clock");
        return false;
    }

    // Parse and validate timecode
    result.timecode = statusbar::ltc::Timecode{};
    result.timecode.rate = config.frame_rate;

    if (!config.start_timecode.empty()) {
        auto parsed = statusbar::ltc::Timecode::from_string(config.start_timecode, config.frame_rate);
        if (!parsed) {
            std::println(stderr, "Error: Invalid timecode format. Use HH:MM:SS:FF or HH:MM:SS;FF (e.g., 01:30:45:15)");
            return false;
        }
        result.timecode = *parsed;

        if (!result.timecode.is_valid()) {
            std::println(
                stderr,
                "Error: Invalid timecode {} for frame rate {}",
                result.timecode.to_string(),
                statusbar::ltc::frame_rate_name(config.frame_rate));
            if (statusbar::ltc::is_drop_frame(config.frame_rate)) {
                std::println(stderr, "Note: Drop frame skips frames 0-1 at non-10th minute boundaries");
            }
            return false;
        }
    }

    // Validate sample rate
    if (config.sample_rate == 44100) {
        result.sample_rate = statusbar::ltc::Generator::SampleRate::Rate_44100;
    } else if (config.sample_rate == 48000) {
        result.sample_rate = statusbar::ltc::Generator::SampleRate::Rate_48000;
    } else if (config.sample_rate == 96000) {
        result.sample_rate = statusbar::ltc::Generator::SampleRate::Rate_96000;
    } else {
        std::println(stderr, "Error: Invalid sample rate. Must be 44100, 48000, or 96000");
        return false;
    }

    // Validate rise time
    if (config.rise_time_us < 25.0 || config.rise_time_us > 250.0) {
        std::println(stderr, "Error: Rise time must be between 25 and 250 microseconds");
        return false;
    }

    // Validate duration
    if (config.duration_seconds <= 0.0) {
        std::println(stderr, "Error: Duration must be positive");
        return false;
    }

    return true;
}

/// Print generation info to stdout
void print_generation_info(Config const& config, ValidationResult const& validated, GenerationInfo const& gen_info)
{
    std::println("Generating LTC audio:");
    std::println("  Frame rate: {}", statusbar::ltc::frame_rate_name(config.frame_rate));
    std::println("  Start timecode: {}", validated.timecode.to_string());
    std::println("  Audio sample rate: {} Hz", config.sample_rate);
    std::println("  Duration: {:.2f} seconds ({} frames)", config.duration_seconds, gen_info.total_frames);
    std::println("  Rise/fall time: {:.1f} us", config.rise_time_us);
    std::println("  Total samples: {}", gen_info.total_samples);
    if (config.play_audio) {
        std::println("  Audio output: {} (device)", config.audio_device);
    } else {
        std::println("  Output file: {}", config.output_file);
        std::println("  Output format: {}", config.format == OutputFormat::Wav16 ? "WAV (16-bit PCM mono)" : "Raw (32-bit float)");
    }
}

/// Play LTC audio through speakers
int play_audio(
    Config const& config, ValidationResult const& validated, GenerationInfo const& gen_info, statusbar::ltc::Generator& gen)
{
    using namespace statusbar::audio;

    // Create audio stream
    AudioConfig audio_config{
        .sample_rate = static_cast<SampleRate>(config.sample_rate),
        .channels = 1,  // LTC is mono
        .format = SampleFormat::Float32,
        .buffer_frames = 512,
        .non_interleaved = true};

    auto stream_result = OutputStream::create(config.audio_device, audio_config);
    if (!stream_result) {
        std::println(stderr, "Error: Failed to create audio stream: {}", stream_result.error().message());
        return 1;
    }

    auto& stream = *stream_result;
    std::println("\nAudio device: {}", stream->device().name);

    if (config.use_realtime_clock) {
        return play_with_realtime_clock(config, validated.sample_rate, *stream);
    } else {
        return play_with_fixed_timecode(
            config, gen, validated.timecode, gen_info.total_frames, gen_info.samples_per_frame, *stream);
    }
}

}  // namespace

int main(int argc, char* argv[])
{
    // Build config and specs with bindings
    Config config;
    auto specs = build_arg_specs(config);

    // Parse CLI arguments, handle --completion, --help, --config-load, --config-save, apply bindings
    auto cli_result = statusbar::config::parse_cli_args(argc, argv, specs, print_usage, "statusbar-ltc-gen");
    if (!cli_result) {
        return statusbar::config::handled_builtin_command(cli_result) ? 0 : 1;
    }

    ValidationResult validated;
    if (!validate_config(config, validated)) {
        return 1;
    }

    // Create generator with frame rate
    statusbar::ltc::Generator gen(validated.sample_rate, config.frame_rate, config.rise_time_us);

    // Calculate generation info
    auto gen_info = calculate_generation_info(config, gen);

    if (!config.quiet) {
        print_generation_info(config, validated, gen_info);
    }

    if (config.play_audio) {
        return play_audio(config, validated, gen_info, gen);
    } else {
        return generate_to_file(config, gen, validated.timecode, gen_info.total_frames);
    }
}