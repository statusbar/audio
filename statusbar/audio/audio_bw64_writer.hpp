#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Minimal streaming writer for 32-bit float Broadcast Wave Format (BWF /
// BW64) files. Produces a plain RIFF/WAVE with a `bext` metadata chunk
// containing the origin date/time, a SMPTE-style TimeReference, and a
// description/coding-history string. Any DAW that reads plain WAV will
// read these files; DAWs that look at `bext` get the extra metadata.
//
// The writer is stream-oriented: `open()` writes all headers with
// placeholder sizes, `write_samples()` appends float32 samples in
// channel-interleaved layout, and `close()` seeks back to patch the
// RIFF and `data` chunk sizes. Files larger than 4 GiB are rejected —
// real RF64 `ds64` extension would be a later addition.

#include "statusbar/status/status.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>

namespace statusbar::audio {

class Bw64Writer
{
  public:
    struct Params
    {
        uint32_t sample_rate;
        uint16_t channel_count;
        std::string description;     // bext.Description (up to 256 chars)
        std::string originator;      // bext.Originator (up to 32 chars)
        std::string originator_ref;  // bext.OriginatorReference (up to 32 chars)
        std::string coding_history;  // bext.CodingHistory (free-form, CRLF-terminated)
        uint64_t time_reference{0};  // bext.TimeReference (64-bit sample offset from origin)
    };

    Bw64Writer();
    ~Bw64Writer();

    Bw64Writer(Bw64Writer const&) = delete;
    auto operator=(Bw64Writer const&) -> Bw64Writer& = delete;
    Bw64Writer(Bw64Writer&&) noexcept = default;
    auto operator=(Bw64Writer&&) noexcept -> Bw64Writer& = default;

    /// Open a BWF file for writing. Truncates any existing file.
    /// Writes RIFF, bext, fmt, and (empty) data chunk headers.
    [[nodiscard]] auto open(std::string const& path, Params const& params) -> Status;

    /// Append channel-interleaved float32 samples. Sample count must be a
    /// multiple of `channel_count`.
    [[nodiscard]] auto write_samples(std::span<float const> interleaved) -> Status;

    /// Seek back and patch RIFF/data chunk sizes, then close the file.
    /// Safe to call twice; second call is a no-op.
    [[nodiscard]] auto close() -> Status;

    [[nodiscard]] auto total_samples_written() const noexcept -> uint64_t { return total_sample_frames_; }
    [[nodiscard]] auto is_open() const noexcept -> bool { return file_ != nullptr; }

  private:
    std::unique_ptr<std::FILE, int (*)(std::FILE*)> file_;
    std::string path_;
    uint32_t sample_rate_{0};
    uint16_t channel_count_{0};
    uint64_t total_sample_frames_{0};  // frames = per-channel sample count
    long riff_size_offset_{0};
    long data_size_offset_{0};
};

}  // namespace statusbar::audio
