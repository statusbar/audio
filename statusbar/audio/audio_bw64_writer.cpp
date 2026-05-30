// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT
#include "statusbar/audio/audio_bw64_writer.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <system_error>

namespace statusbar::audio {

namespace {

constexpr size_t BEXT_FIXED_SIZE = 602;  // per EBU Tech 3285
constexpr size_t FMT_CHUNK_BODY = 16;    // PCM / IEEE_FLOAT WAVEFORMAT
constexpr uint16_t WAVE_FORMAT_IEEE_FLOAT = 0x0003;
constexpr uint16_t BEXT_VERSION = 2;

auto fwrite_exact(std::FILE* f, void const* data, size_t n) -> bool
{
    return n == 0 || std::fwrite(data, 1, n, f) == n;
}

auto write_u32_le(std::FILE* f, uint32_t v) -> bool
{
    std::array<uint8_t, 4> buf{
        static_cast<uint8_t>(v & 0xff),
        static_cast<uint8_t>((v >> 8) & 0xff),
        static_cast<uint8_t>((v >> 16) & 0xff),
        static_cast<uint8_t>((v >> 24) & 0xff),
    };
    return fwrite_exact(f, buf.data(), buf.size());
}

auto write_u16_le(std::FILE* f, uint16_t v) -> bool
{
    std::array<uint8_t, 2> buf{
        static_cast<uint8_t>(v & 0xff),
        static_cast<uint8_t>((v >> 8) & 0xff),
    };
    return fwrite_exact(f, buf.data(), buf.size());
}

auto write_fourcc(std::FILE* f, char const (&id)[5]) -> bool
{
    return fwrite_exact(f, id, 4);
}

// Write `src` into a fixed-size ASCII field, zero-padded. Truncates
// silently if too long.
auto write_padded_ascii(std::FILE* f, std::string const& src, size_t field_size) -> bool
{
    size_t const copy_len = src.size() < field_size ? src.size() : field_size;
    if (!fwrite_exact(f, src.data(), copy_len)) {
        return false;
    }
    size_t const pad = field_size - copy_len;
    if (pad == 0) {
        return true;
    }
    std::array<uint8_t, 256> zeros{};
    size_t remaining = pad;
    while (remaining > 0) {
        size_t const chunk = remaining < zeros.size() ? remaining : zeros.size();
        if (!fwrite_exact(f, zeros.data(), chunk)) {
            return false;
        }
        remaining -= chunk;
    }
    return true;
}

struct DateTime
{
    char date_buf[11];  // "YYYY-MM-DD\0"
    char time_buf[9];   // "HH:MM:SS\0"
};

auto current_datetime() -> DateTime
{
    DateTime dt{};
    auto const now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm_buf{};
    gmtime_r(&now, &tm_buf);
    std::strftime(dt.date_buf, sizeof(dt.date_buf), "%Y-%m-%d", &tm_buf);
    std::strftime(dt.time_buf, sizeof(dt.time_buf), "%H:%M:%S", &tm_buf);
    return dt;
}

}  // namespace

Bw64Writer::Bw64Writer()
    : file_(nullptr, [](std::FILE*) -> int { return 0; })
{}

Bw64Writer::~Bw64Writer() = default;

auto Bw64Writer::open(std::string const& path, Params const& params) -> Status
{
    if (params.channel_count == 0 || params.sample_rate == 0) {
        return failure(std::make_error_code(std::errc::invalid_argument));
    }

    auto deleter = +[](std::FILE* p) -> int { return p == nullptr ? 0 : std::fclose(p); };
    file_ = decltype(file_){std::fopen(path.c_str(), "wb"), deleter};
    if (!file_) {
        return failure(std::make_error_code(std::errc::permission_denied));
    }

    path_ = path;
    sample_rate_ = params.sample_rate;
    channel_count_ = params.channel_count;
    total_sample_frames_ = 0;

    // Size of bext chunk body: 602 fixed bytes + coding_history, padded to
    // even length (RIFF chunks are word-aligned).
    size_t const coding_len = params.coding_history.size();
    size_t const coding_padded = coding_len + (coding_len & 1U);
    uint32_t const bext_body_size = static_cast<uint32_t>(BEXT_FIXED_SIZE + coding_padded);

    // RIFF header
    if (!write_fourcc(file_.get(), "RIFF")) {
        return failure(std::make_error_code(std::errc::io_error));
    }
    riff_size_offset_ = std::ftell(file_.get());
    if (!write_u32_le(file_.get(), 0)) {  // placeholder
        return failure(std::make_error_code(std::errc::io_error));
    }
    if (!write_fourcc(file_.get(), "WAVE")) {
        return failure(std::make_error_code(std::errc::io_error));
    }

    // bext chunk
    if (!write_fourcc(file_.get(), "bext")) {
        return failure(std::make_error_code(std::errc::io_error));
    }
    if (!write_u32_le(file_.get(), bext_body_size)) {
        return failure(std::make_error_code(std::errc::io_error));
    }
    DateTime const dt = current_datetime();
    if (!write_padded_ascii(file_.get(), params.description, 256)) {
        return failure(std::make_error_code(std::errc::io_error));
    }
    if (!write_padded_ascii(file_.get(), params.originator, 32)) {
        return failure(std::make_error_code(std::errc::io_error));
    }
    if (!write_padded_ascii(file_.get(), params.originator_ref, 32)) {
        return failure(std::make_error_code(std::errc::io_error));
    }
    if (!fwrite_exact(file_.get(), dt.date_buf, 10)) {
        return failure(std::make_error_code(std::errc::io_error));
    }
    if (!fwrite_exact(file_.get(), dt.time_buf, 8)) {
        return failure(std::make_error_code(std::errc::io_error));
    }
    uint32_t const time_ref_low = static_cast<uint32_t>(params.time_reference & 0xFFFFFFFFU);
    uint32_t const time_ref_high = static_cast<uint32_t>((params.time_reference >> 32) & 0xFFFFFFFFU);
    if (!write_u32_le(file_.get(), time_ref_low) || !write_u32_le(file_.get(), time_ref_high)) {
        return failure(std::make_error_code(std::errc::io_error));
    }
    if (!write_u16_le(file_.get(), BEXT_VERSION)) {
        return failure(std::make_error_code(std::errc::io_error));
    }
    // UMID (64 bytes), LoudnessValue/Range/Peaks (5 x 2 bytes), Reserved (180 bytes)
    // all zero.
    std::array<uint8_t, 64 + 2 + 2 + 2 + 2 + 2 + 180> bext_trailing{};
    if (!fwrite_exact(file_.get(), bext_trailing.data(), bext_trailing.size())) {
        return failure(std::make_error_code(std::errc::io_error));
    }
    if (coding_len > 0) {
        if (!fwrite_exact(file_.get(), params.coding_history.data(), coding_len)) {
            return failure(std::make_error_code(std::errc::io_error));
        }
        if ((coding_len & 1U) != 0) {
            uint8_t const pad_byte = 0;
            if (!fwrite_exact(file_.get(), &pad_byte, 1)) {
                return failure(std::make_error_code(std::errc::io_error));
            }
        }
    }

    // fmt chunk (WAVE_FORMAT_IEEE_FLOAT, 16-byte body)
    if (!write_fourcc(file_.get(), "fmt ")) {
        return failure(std::make_error_code(std::errc::io_error));
    }
    if (!write_u32_le(file_.get(), FMT_CHUNK_BODY)) {
        return failure(std::make_error_code(std::errc::io_error));
    }
    uint32_t const byte_rate = static_cast<uint32_t>(sample_rate_) * channel_count_ * 4U;
    uint16_t const block_align = static_cast<uint16_t>(channel_count_ * 4U);
    if (!write_u16_le(file_.get(), WAVE_FORMAT_IEEE_FLOAT) || !write_u16_le(file_.get(), channel_count_) ||
        !write_u32_le(file_.get(), sample_rate_) || !write_u32_le(file_.get(), byte_rate) ||
        !write_u16_le(file_.get(), block_align) || !write_u16_le(file_.get(), 32)) {
        return failure(std::make_error_code(std::errc::io_error));
    }

    // data chunk header
    if (!write_fourcc(file_.get(), "data")) {
        return failure(std::make_error_code(std::errc::io_error));
    }
    data_size_offset_ = std::ftell(file_.get());
    if (!write_u32_le(file_.get(), 0)) {  // placeholder
        return failure(std::make_error_code(std::errc::io_error));
    }

    return success();
}

auto Bw64Writer::write_samples(std::span<float const> interleaved) -> Status
{
    if (!file_) {
        return failure(std::make_error_code(std::errc::bad_file_descriptor));
    }
    if (channel_count_ == 0 || (interleaved.size() % channel_count_) != 0) {
        return failure(std::make_error_code(std::errc::invalid_argument));
    }
    if (interleaved.empty()) {
        return success();
    }

    size_t const n_bytes = interleaved.size() * sizeof(float);
    if (!fwrite_exact(file_.get(), interleaved.data(), n_bytes)) {
        return failure(std::make_error_code(std::errc::io_error));
    }
    total_sample_frames_ += interleaved.size() / channel_count_;
    return success();
}

auto Bw64Writer::close() -> Status
{
    if (!file_) {
        return success();
    }

    long const end_pos = std::ftell(file_.get());
    if (end_pos < 0) {
        file_.reset();
        return failure(std::make_error_code(std::errc::io_error));
    }

    uint64_t const data_bytes = total_sample_frames_ * channel_count_ * 4ULL;
    uint64_t const riff_bytes = static_cast<uint64_t>(end_pos) - 8ULL;

    if (data_bytes > 0xFFFFFFFFULL || riff_bytes > 0xFFFFFFFFULL) {
        file_.reset();
        return failure(std::make_error_code(std::errc::file_too_large));
    }

    // Patch RIFF size
    if (std::fseek(file_.get(), riff_size_offset_, SEEK_SET) != 0 ||
        !write_u32_le(file_.get(), static_cast<uint32_t>(riff_bytes))) {
        file_.reset();
        return failure(std::make_error_code(std::errc::io_error));
    }

    // Patch data size
    if (std::fseek(file_.get(), data_size_offset_, SEEK_SET) != 0 ||
        !write_u32_le(file_.get(), static_cast<uint32_t>(data_bytes))) {
        file_.reset();
        return failure(std::make_error_code(std::errc::io_error));
    }

    file_.reset();
    return success();
}

}  // namespace statusbar::audio
