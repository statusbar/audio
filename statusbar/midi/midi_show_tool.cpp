// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// MIDI Show Tool — Read a MIDI file and print its contents as text

#include "statusbar/config/config.hpp"
#include "statusbar/midi/midi.hpp"
#include "statusbar/midi/midi_fileshow.hpp"
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
    std::string file;
};

auto build_arg_specs(Config& config) -> statusbar::args::ArgumentSpecs
{
    statusbar::args::ArgumentSpecs specs;
    specs.add_file("file", "MIDI file to display", "", [&](auto v) { config.file = std::string{v}; });
    return specs;
}

void print_usage(char const* program_name, statusbar::args::ArgumentSpecs const& specs)
{
    std::println(stderr, "Usage: {} --file=<path.mid>", program_name);
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

    auto cli_result = statusbar::config::parse_cli_args(argc, argv, specs, print_usage, "statusbar-midi-show");
    if (!cli_result) {
        return statusbar::config::handled_builtin_command(cli_result) ? 0 : 1;
    }

    if (config.file.empty()) {
        std::println(stderr, "Error: --file is required");
        print_usage(argv[0], specs);
        return EXIT_FAILURE;
    }

    // Read the file into memory
    std::error_code ec;
    auto const file_size = std::filesystem::file_size(config.file, ec);
    if (ec) {
        std::println(stderr, "Error: Cannot stat '{}': {}", config.file, ec.message());
        return EXIT_FAILURE;
    }
    if (file_size > 65536) {
        std::println(stderr, "Error: File too large ({} bytes, max 65536)", file_size);
        return EXIT_FAILURE;
    }

    std::array<uint8_t, 65536> file_buf{};
    FILE* fp = std::fopen(config.file.c_str(), "rb");
    if (!fp) {
        std::println(stderr, "Error: Cannot open '{}'", config.file);
        return EXIT_FAILURE;
    }
    auto const bytes_read = std::fread(file_buf.data(), 1, file_size, fp);
    std::fclose(fp);

    if (bytes_read != file_size) {
        std::println(stderr, "Error: Read {} bytes, expected {}", bytes_read, file_size);
        return EXIT_FAILURE;
    }

    auto const data = std::span<uint8_t const>(file_buf.data(), bytes_read);
    auto cb = statusbar::midi::make_fileshow_callbacks(stdout);
    auto const s = statusbar::midi::read_midi_file(data, cb);

    if (statusbar::is_failure(s)) {
        std::println(stderr, "Error: {}", s.error().message());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
