// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// MIDI Copy Tool — Read a MIDI file, process events, write the result
// Default: identity processor (exact copy)

#include "statusbar/config/config.hpp"
#include "statusbar/midi/midi.hpp"
#include "statusbar/midi/midi_processor.hpp"
#include "statusbar/status/status.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <print>
#include <string>
#include <string_view>

namespace {

struct Config
{
    std::string input;
    std::string output;
};

auto build_arg_specs(Config& config) -> statusbar::args::ArgumentSpecs
{
    statusbar::args::ArgumentSpecs specs;
    specs.add_file("input", "Input MIDI file", "", [&](auto v) { config.input = std::string{v}; });
    specs.add_file("output", "Output MIDI file", "", [&](auto v) { config.output = std::string{v}; });
    return specs;
}

void print_usage(char const* program_name, statusbar::args::ArgumentSpecs const& specs)
{
    std::println(stderr, "Usage: {} --input=<in.mid> --output=<out.mid>", program_name);
    std::println(stderr, "\nOptions:");
    std::string help;
    specs.format_help_to(std::back_inserter(help));
    std::print(stderr, "{}", help);
}

}  // namespace

int main(int argc, char** argv)
{
    Config config;
    auto specs = build_arg_specs(config);

    auto cli_result = statusbar::config::parse_cli_args(argc, argv, specs, print_usage, "statusbar-midi-copy");
    if (!cli_result) {
        return statusbar::config::handled_builtin_command(cli_result) ? 0 : 1;
    }

    if (config.input.empty() || config.output.empty()) {
        std::println(stderr, "Error: --input and --output are required");
        print_usage(argv[0], specs);
        return EXIT_FAILURE;
    }

    // Read input file
    std::error_code ec;
    auto const file_size = std::filesystem::file_size(config.input, ec);
    if (ec) {
        std::println(stderr, "Error: Cannot stat '{}': {}", config.input, ec.message());
        return EXIT_FAILURE;
    }
    if (file_size > 65536) {
        std::println(stderr, "Error: File too large ({} bytes, max 65536)", file_size);
        return EXIT_FAILURE;
    }

    std::array<uint8_t, 65536> input_buf{};
    FILE* fp = std::fopen(config.input.c_str(), "rb");
    if (fp == nullptr) {
        std::println(stderr, "Error: Cannot open '{}'", config.input);
        return EXIT_FAILURE;
    }
    auto const bytes_read = std::fread(input_buf.data(), 1, file_size, fp);
    std::fclose(fp);

    if (bytes_read != file_size) {
        std::println(stderr, "Error: Read {} bytes, expected {}", bytes_read, file_size);
        return EXIT_FAILURE;
    }

    auto const input_data = std::span<uint8_t const>(input_buf.data(), bytes_read);

    // Process: identity (default processor)
    statusbar::midi::MidiFileProcessor processor;

    // Write output
    std::array<uint8_t, 65536> output_buf{};
    statusbar::MutableBuffer output_mbuf(output_buf);
    statusbar::midi::MidiFileWriter writer(output_mbuf);

    auto const s = statusbar::midi::process_midi_file(input_data, processor, writer);
    if (statusbar::is_failure(s)) {
        std::println(stderr, "Error: {}", s.error().message());
        return EXIT_FAILURE;
    }

    // Write to disk
    FILE* out_fp = std::fopen(config.output.c_str(), "wb");
    if (out_fp == nullptr) {
        std::println(stderr, "Error: Cannot open '{}' for writing", config.output);
        return EXIT_FAILURE;
    }
    auto const output_span = output_mbuf.get_span();
    auto const written = std::fwrite(output_span.data(), 1, output_span.size(), out_fp);
    std::fclose(out_fp);

    if (written != output_span.size()) {
        std::println(stderr, "Error: Wrote {} bytes, expected {}", written, output_span.size());
        return EXIT_FAILURE;
    }

    std::println("Copied {} bytes: {} -> {}", output_span.size(), config.input, config.output);
    return EXIT_SUCCESS;
}
