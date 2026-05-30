// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/audio/audio_bw64_writer.hpp"

#include "statusbar/test/test.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace statusbar;
using namespace statusbar::audio;

namespace {

auto read_file(std::string const& path) -> std::vector<uint8_t>
{
    auto* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return {};
    }
    std::fseek(f, 0, SEEK_END);
    auto const size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    (void)std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    return buf;
}

[[nodiscard]] auto read_u32_le(std::span<uint8_t const> buf, size_t offset) -> uint32_t
{
    return static_cast<uint32_t>(buf[offset]) | (static_cast<uint32_t>(buf[offset + 1]) << 8) |
        (static_cast<uint32_t>(buf[offset + 2]) << 16) | (static_cast<uint32_t>(buf[offset + 3]) << 24);
}

[[nodiscard]] auto read_u16_le(std::span<uint8_t const> buf, size_t offset) -> uint16_t
{
    return static_cast<uint16_t>(buf[offset]) | (static_cast<uint16_t>(buf[offset + 1]) << 8);
}

[[nodiscard]] auto find_chunk(std::span<uint8_t const> buf, char const* fourcc) -> size_t
{
    // Search starts after the 12-byte RIFF/WAVE header.
    for (size_t i = 12; i + 8 <= buf.size();) {
        if (std::memcmp(&buf[i], fourcc, 4) == 0) {
            return i;
        }
        auto const chunk_size = read_u32_le(buf, i + 4);
        i += 8 + chunk_size + (chunk_size & 1U);  // word-align
    }
    return std::string::npos;
}

}  // namespace

TEST(bw64_writer, empty_file_has_valid_headers)
{
    std::string const path = "/tmp/statusbar_bw64_empty.wav";
    std::remove(path.c_str());

    {
        Bw64Writer w;
        Bw64Writer::Params p{};
        p.sample_rate = 48000;
        p.channel_count = 2;
        p.originator = "tester";
        EXPECT_TRUE(bool(w.open(path, p)));
        EXPECT_TRUE(bool(w.close()));
    }

    auto const buf = read_file(path);
    EXPECT_TRUE(buf.size() > 0);
    EXPECT_EQ(std::memcmp(&buf[0], "RIFF", 4), 0);
    EXPECT_EQ(std::memcmp(&buf[8], "WAVE", 4), 0);

    auto const riff_size = read_u32_le(buf, 4);
    EXPECT_EQ(riff_size + size_t{8}, buf.size());

    auto const bext_off = find_chunk(buf, "bext");
    EXPECT_NE(bext_off, std::string::npos);

    auto const fmt_off = find_chunk(buf, "fmt ");
    EXPECT_NE(fmt_off, std::string::npos);
    EXPECT_EQ(read_u32_le(buf, fmt_off + 4), uint32_t{16});
    EXPECT_EQ(read_u16_le(buf, fmt_off + 8), uint16_t{3});           // WAVE_FORMAT_IEEE_FLOAT
    EXPECT_EQ(read_u16_le(buf, fmt_off + 10), uint16_t{2});          // channels
    EXPECT_EQ(read_u32_le(buf, fmt_off + 12), uint32_t{48000});      // sample_rate
    EXPECT_EQ(read_u32_le(buf, fmt_off + 16), uint32_t{48000 * 8});  // byte_rate
    EXPECT_EQ(read_u16_le(buf, fmt_off + 20), uint16_t{8});          // block_align
    EXPECT_EQ(read_u16_le(buf, fmt_off + 22), uint16_t{32});         // bits_per_sample

    auto const data_off = find_chunk(buf, "data");
    EXPECT_NE(data_off, std::string::npos);
    EXPECT_EQ(read_u32_le(buf, data_off + 4), uint32_t{0});

    std::remove(path.c_str());
}

TEST(bw64_writer, samples_are_appended_and_sizes_patched)
{
    std::string const path = "/tmp/statusbar_bw64_samples.wav";
    std::remove(path.c_str());

    std::array<float, 8> samples{0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f};  // 4 stereo frames

    {
        Bw64Writer w;
        Bw64Writer::Params p{};
        p.sample_rate = 48000;
        p.channel_count = 2;
        EXPECT_TRUE(bool(w.open(path, p)));
        EXPECT_TRUE(bool(w.write_samples(std::span<float const>(samples))));
        EXPECT_EQ(w.total_samples_written(), uint64_t{4});
        EXPECT_TRUE(bool(w.close()));
    }

    auto const buf = read_file(path);
    auto const data_off = find_chunk(buf, "data");
    EXPECT_NE(data_off, std::string::npos);
    EXPECT_EQ(read_u32_le(buf, data_off + 4), uint32_t{4 * 2 * 4});  // 4 frames × 2ch × 4 bytes

    // Read back the sample bytes and confirm they match.
    for (size_t i = 0; i < samples.size(); ++i) {
        float decoded = 0.0f;
        std::memcpy(&decoded, &buf[data_off + 8 + (i * 4)], 4);
        EXPECT_TRUE(decoded == samples[i]);
    }

    std::remove(path.c_str());
}

TEST(bw64_writer, bad_sample_count_rejected)
{
    std::string const path = "/tmp/statusbar_bw64_bad.wav";
    std::remove(path.c_str());

    Bw64Writer w;
    Bw64Writer::Params p{};
    p.sample_rate = 48000;
    p.channel_count = 2;
    EXPECT_TRUE(bool(w.open(path, p)));

    // Odd count (3) is not divisible by channel_count=2.
    std::array<float, 3> bad{0.0f, 0.1f, 0.2f};
    EXPECT_FALSE(bool(w.write_samples(std::span<float const>(bad))));
    (void)w.close();

    std::remove(path.c_str());
}

TEST(bw64_writer, open_invalid_params)
{
    Bw64Writer w;
    Bw64Writer::Params p{};
    p.sample_rate = 0;  // invalid
    p.channel_count = 2;
    EXPECT_FALSE(bool(w.open("/tmp/unreachable.wav", p)));

    p.sample_rate = 48000;
    p.channel_count = 0;  // invalid
    EXPECT_FALSE(bool(w.open("/tmp/unreachable.wav", p)));
}

TEST(bw64_writer, close_is_idempotent)
{
    std::string const path = "/tmp/statusbar_bw64_idem.wav";
    std::remove(path.c_str());

    Bw64Writer w;
    Bw64Writer::Params p{};
    p.sample_rate = 48000;
    p.channel_count = 1;
    EXPECT_TRUE(bool(w.open(path, p)));
    EXPECT_TRUE(bool(w.close()));
    EXPECT_TRUE(bool(w.close()));  // second call returns success silently

    std::remove(path.c_str());
}

TEST_MAIN(statusbar_audio, audio_bw64_writer_test)
