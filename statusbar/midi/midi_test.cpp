// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/midi/midi.hpp"

#include "statusbar/buffer/buffer.hpp"
#include "statusbar/status/status.hpp"
#include "statusbar/test/test.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace statusbar::midi;
using namespace statusbar;

// Compile-time verification
static_assert(status::note_on == 0x90);
static_assert(status::note_off == 0x80);
static_assert(status::control_change == 0xB0);
static_assert(status::program_change == 0xC0);
static_assert(status::pitch_bend == 0xE0);
static_assert(status::sysex_start == 0xF0);
static_assert(status::sysex_end == 0xF7);
static_assert(meta::end_of_track == 0x2F);
static_assert(meta::tempo == 0x51);

static_assert(MidiMessage::note_on(0, 60, 100).status() == 0x90);
static_assert(MidiMessage::note_on(0, 60, 100).channel() == 0);
static_assert(MidiMessage::note_on(0, 60, 100).note() == 60);
static_assert(MidiMessage::note_on(0, 60, 100).velocity() == 100);
static_assert(MidiMessage::note_on(0, 60, 100).type() == status::note_on);
static_assert(MidiMessage::note_on(0, 60, 100).is_note_on());
static_assert(MidiMessage::note_on(0, 60, 0).is_note_off());
static_assert(MidiMessage::note_off(3, 64, 0).is_note_off());
static_assert(MidiMessage::note_off(3, 64, 0).channel() == 3);
static_assert(MidiMessage::control_change(1, 7, 100).controller() == 7);
static_assert(MidiMessage::control_change(1, 7, 100).controller_value() == 100);
static_assert(MidiMessage::program_change(0, 42).program() == 42);
static_assert(MidiMessage::program_change(0, 42).message_length() == 2);
static_assert(MidiMessage::note_on(0, 60, 100).message_length() == 3);
static_assert(MidiMessage::note_on(0, 60, 100).is_channel_message());
static_assert(!MidiMessage::note_on(0, 60, 100).is_system_message());

TEST(midi_msg, factory_note_on)
{
    auto const msg = MidiMessage::note_on(2, 60, 127);
    EXPECT_EQ(msg.status(), uint8_t{0x92});
    EXPECT_EQ(msg.channel(), uint8_t{2});
    EXPECT_EQ(msg.note(), uint8_t{60});
    EXPECT_EQ(msg.velocity(), uint8_t{127});
    EXPECT_TRUE(msg.is_note_on());
    EXPECT_TRUE(msg.is_channel_message());
    EXPECT_FALSE(msg.is_system_message());
}

TEST(midi_msg, factory_note_off)
{
    auto const msg = MidiMessage::note_off(5, 72, 64);
    EXPECT_EQ(msg.status(), uint8_t{0x85});
    EXPECT_TRUE(msg.is_note_off());
}

TEST(midi_msg, factory_control_change)
{
    auto const msg = MidiMessage::control_change(0, 64, 127);
    EXPECT_EQ(msg.type(), status::control_change);
    EXPECT_EQ(msg.controller(), uint8_t{64});
    EXPECT_EQ(msg.controller_value(), uint8_t{127});
    EXPECT_EQ(msg.message_length(), 3);
}

TEST(midi_msg, factory_program_change)
{
    auto const msg = MidiMessage::program_change(9, 0);
    EXPECT_EQ(msg.type(), status::program_change);
    EXPECT_EQ(msg.program(), uint8_t{0});
    EXPECT_EQ(msg.message_length(), 2);
}

TEST(midi_msg, factory_pitch_bend)
{
    auto const msg = MidiMessage::pitch_bend(0, 0);
    EXPECT_EQ(msg.type(), status::pitch_bend);
    EXPECT_EQ(msg.pitch_bend(), int16_t{0});

    auto const up = MidiMessage::pitch_bend(0, 8191);
    EXPECT_EQ(up.pitch_bend(), int16_t{8191});

    auto const down = MidiMessage::pitch_bend(0, -8192);
    EXPECT_EQ(down.pitch_bend(), int16_t{-8192});
}

TEST(midi_msg, factory_channel_pressure)
{
    auto const msg = MidiMessage::channel_pressure(3, 100);
    EXPECT_EQ(msg.type(), status::channel_pressure);
    EXPECT_EQ(msg.data1(), uint8_t{100});
    EXPECT_EQ(msg.message_length(), 2);
}

TEST(midi_msg, factory_poly_pressure)
{
    auto const msg = MidiMessage::poly_pressure(1, 60, 80);
    EXPECT_EQ(msg.type(), status::poly_pressure);
    EXPECT_EQ(msg.note(), uint8_t{60});
    EXPECT_EQ(msg.data2(), uint8_t{80});
    EXPECT_EQ(msg.message_length(), 3);
}

TEST(midi_msg, note_on_zero_velocity_is_note_off)
{
    auto const msg = MidiMessage::note_on(0, 60, 0);
    EXPECT_TRUE(msg.is_note_off());
    EXPECT_FALSE(msg.is_note_on());
}

TEST(midi_msg, realtime_messages)
{
    auto const msg = MidiMessage(status::timing_clock);
    EXPECT_TRUE(msg.is_realtime());
    EXPECT_TRUE(msg.is_system_message());
    EXPECT_FALSE(msg.is_channel_message());
    EXPECT_EQ(msg.message_length(), 1);
}

TEST(midi_sysex, empty_initial_state)
{
    SysexMessage<64> sysex;
    EXPECT_EQ(sysex.length(), size_t{0});
    EXPECT_EQ(sysex.capacity(), size_t{64});
    EXPECT_FALSE(sysex.is_full());
}

TEST(midi_sysex, put_bytes_and_read_back)
{
    SysexMessage<64> sysex;
    EXPECT_TRUE(is_success(sysex.put_byte(0xF0)));
    EXPECT_TRUE(is_success(sysex.put_byte(0x7E)));
    EXPECT_TRUE(is_success(sysex.put_byte(0x01)));
    EXPECT_TRUE(is_success(sysex.put_byte(0xF7)));
    EXPECT_EQ(sysex.length(), size_t{4});
    auto const d = sysex.data();
    EXPECT_EQ(d[0], uint8_t{0xF0});
    EXPECT_EQ(d[1], uint8_t{0x7E});
    EXPECT_EQ(d[2], uint8_t{0x01});
    EXPECT_EQ(d[3], uint8_t{0xF7});
}

TEST(midi_sysex, overflow_returns_error)
{
    SysexMessage<4> sysex;
    EXPECT_TRUE(is_success(sysex.put_byte(0xF0)));
    EXPECT_TRUE(is_success(sysex.put_byte(0x01)));
    EXPECT_TRUE(is_success(sysex.put_byte(0x02)));
    EXPECT_TRUE(is_success(sysex.put_byte(0xF7)));
    EXPECT_TRUE(sysex.is_full());
    EXPECT_TRUE(is_failure(sysex.put_byte(0x00)));
}

TEST(midi_sysex, checksum)
{
    SysexMessage<64> sysex;
    EXPECT_TRUE(is_success(sysex.put_byte(0x01)));
    EXPECT_TRUE(is_success(sysex.put_byte(0x02)));
    EXPECT_TRUE(is_success(sysex.put_byte(0x03)));
    EXPECT_EQ(sysex.checksum(), uint8_t{0x00});
}

TEST(midi_sysex, clear_resets)
{
    SysexMessage<64> sysex;
    EXPECT_TRUE(is_success(sysex.put_byte(0xF0)));
    EXPECT_TRUE(is_success(sysex.put_byte(0x01)));
    sysex.clear();
    EXPECT_EQ(sysex.length(), size_t{0});
    EXPECT_FALSE(sysex.is_full());
}

TEST(midi_sysex, default_template_size)
{
    SysexMessage<> sysex;
    EXPECT_EQ(sysex.capacity(), size_t{1024});
}

TEST(midi_vlq, read_single_byte)
{
    uint8_t const data[] = {0x00};
    size_t offset = 0;
    auto const result = detail::read_vlq(std::span<uint8_t const>(data), offset);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), uint32_t{0});
    EXPECT_EQ(offset, size_t{1});
}

TEST(midi_vlq, read_two_byte)
{
    uint8_t const data[] = {0x81, 0x00};
    size_t offset = 0;
    auto const result = detail::read_vlq(std::span<uint8_t const>(data), offset);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), uint32_t{128});
    EXPECT_EQ(offset, size_t{2});
}

TEST(midi_vlq, read_max_value)
{
    uint8_t const data[] = {0xFF, 0xFF, 0xFF, 0x7F};
    size_t offset = 0;
    auto const result = detail::read_vlq(std::span<uint8_t const>(data), offset);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), uint32_t{0x0FFFFFFFu});
    EXPECT_EQ(offset, size_t{4});
}

TEST(midi_vlq, read_truncated)
{
    uint8_t const data[] = {0x81};
    size_t offset = 0;
    auto const result = detail::read_vlq(std::span<uint8_t const>(data), offset);
    EXPECT_FALSE(result.has_value());
}

TEST(midi_vlq, read_127)
{
    uint8_t const data[] = {0x7F};
    size_t offset = 0;
    auto const result = detail::read_vlq(std::span<uint8_t const>(data), offset);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), uint32_t{127});
}

TEST(midi_vlq, write_zero)
{
    std::array<uint8_t, 64> storage{};
    MutableBuffer buf(storage);
    EXPECT_TRUE(is_success(detail::write_vlq(buf, 0)));
    EXPECT_EQ(buf.size(), size_t{1});
    EXPECT_EQ(buf.get_span()[0], uint8_t{0x00});
}

TEST(midi_vlq, write_128)
{
    std::array<uint8_t, 64> storage{};
    MutableBuffer buf(storage);
    EXPECT_TRUE(is_success(detail::write_vlq(buf, 128)));
    EXPECT_EQ(buf.size(), size_t{2});
    EXPECT_EQ(buf.get_span()[0], uint8_t{0x81});
    EXPECT_EQ(buf.get_span()[1], uint8_t{0x00});
}

TEST(midi_vlq, write_max)
{
    std::array<uint8_t, 64> storage{};
    MutableBuffer buf(storage);
    EXPECT_TRUE(is_success(detail::write_vlq(buf, 0x0FFFFFFFu)));
    EXPECT_EQ(buf.size(), size_t{4});
    EXPECT_EQ(buf.get_span()[0], uint8_t{0xFF});
    EXPECT_EQ(buf.get_span()[1], uint8_t{0xFF});
    EXPECT_EQ(buf.get_span()[2], uint8_t{0xFF});
    EXPECT_EQ(buf.get_span()[3], uint8_t{0x7F});
}

TEST(midi_vlq, write_rejects_over_28_bits)
{
    std::array<uint8_t, 64> storage{};
    MutableBuffer buf(storage);
    EXPECT_TRUE(is_failure(detail::write_vlq(buf, 0x10000000u)));
    EXPECT_TRUE(is_failure(detail::write_vlq(buf, 0xFFFFFFFFu)));
    EXPECT_EQ(buf.size(), size_t{0});
}

TEST(midi_vlq, roundtrip)
{
    uint32_t const values[] = {0, 1, 127, 128, 255, 16383, 16384, 0x0FFFFFFFu};
    for (auto const val : values) {
        std::array<uint8_t, 64> storage{};
        MutableBuffer buf(storage);
        EXPECT_TRUE(is_success(detail::write_vlq(buf, val)));
        size_t offset = 0;
        auto const result = detail::read_vlq(buf.get_span(), offset);
        EXPECT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), val);
    }
}

TEST(midi_meta, tempo_interpretation)
{
    uint8_t const data[] = {0x07, 0xA1, 0x20};
    MetaEvent ev{.type = meta::tempo, .data = std::span<uint8_t const>(data)};
    auto const us = ev.tempo_us_per_beat();
    EXPECT_TRUE(us.has_value());
    EXPECT_EQ(us.value(), uint32_t{500000});
    auto const bpm = ev.tempo_bpm();
    EXPECT_TRUE(bpm.has_value());
    EXPECT_TRUE(bpm.value() > 119.9f && bpm.value() < 120.1f);
}

TEST(midi_meta, tempo_wrong_size)
{
    uint8_t const data[] = {0x07, 0xA1};
    MetaEvent ev{.type = meta::tempo, .data = std::span<uint8_t const>(data)};
    EXPECT_FALSE(ev.tempo_us_per_beat().has_value());
}

TEST(midi_meta, text_view)
{
    uint8_t const data[] = {'H', 'e', 'l', 'l', 'o'};
    MetaEvent ev{.type = meta::text, .data = std::span<uint8_t const>(data)};
    EXPECT_EQ(ev.text(), std::string_view("Hello"));
}

TEST(midi_meta, time_signature)
{
    uint8_t const data[] = {0x06, 0x03, 0x24, 0x08};
    MetaEvent ev{.type = meta::time_signature, .data = std::span<uint8_t const>(data)};
    auto const ts = ev.time_signature();
    EXPECT_TRUE(ts.has_value());
    EXPECT_EQ(ts.value()[0], uint8_t{0x06});
    EXPECT_EQ(ts.value()[1], uint8_t{0x03});
}

TEST(midi_meta, key_signature)
{
    uint8_t const data[] = {0xFD, 0x01};
    MetaEvent ev{.type = meta::key_signature, .data = std::span<uint8_t const>(data)};
    auto const ks = ev.key_signature();
    EXPECT_TRUE(ks.has_value());
    EXPECT_EQ(ks.value()[0], int8_t{-3});
    EXPECT_EQ(ks.value()[1], int8_t{1});
}

TEST(midi_parser, note_on_complete)
{
    MidiMessage received;
    bool got_message = false;

    MidiParser<64> parser(
        [&](MidiMessage const& msg) {
            received = msg;
            got_message = true;
        },
        [&](SysexMessage<64> const&) {},
        [&](MidiError) {});

    parser.parse(0x90);
    EXPECT_FALSE(got_message);
    parser.parse(60);
    EXPECT_FALSE(got_message);
    parser.parse(100);
    EXPECT_TRUE(got_message);
    EXPECT_EQ(received.status(), uint8_t{0x90});
    EXPECT_EQ(received.note(), uint8_t{60});
    EXPECT_EQ(received.velocity(), uint8_t{100});
}

TEST(midi_parser, running_status)
{
    int count = 0;
    MidiMessage last;

    MidiParser<64> parser([&](MidiMessage const& msg) {
        last = msg;
        ++count;
    });

    parser.parse(0x91);
    parser.parse(60);
    parser.parse(100);
    EXPECT_EQ(count, 1);

    parser.parse(64);
    parser.parse(80);
    EXPECT_EQ(count, 2);
    EXPECT_EQ(last.status(), uint8_t{0x91});
    EXPECT_EQ(last.note(), uint8_t{64});
    EXPECT_EQ(last.velocity(), uint8_t{80});
}

TEST(midi_parser, system_common_cancels_running_status)
{
    int count = 0;
    MidiMessage last;
    MidiParser<64> parser([&](MidiMessage const& msg) {
        last = msg;
        ++count;
    });

    // Song Select (0xF3) is a one-data-byte system-common message.
    parser.parse(0xF3);
    parser.parse(0x10);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(last.status(), uint8_t{0xF3});

    // A following bare data byte must NOT reuse 0xF3 as running status — system
    // common cancels running status, so no message is emitted.
    parser.parse(0x20);
    EXPECT_EQ(count, 1);
}

TEST(midi_parser, program_change_two_byte)
{
    MidiMessage received;
    bool got_message = false;

    MidiParser<64> parser([&](MidiMessage const& msg) {
        received = msg;
        got_message = true;
    });

    parser.parse(0xC0);
    EXPECT_FALSE(got_message);
    parser.parse(42);
    EXPECT_TRUE(got_message);
    EXPECT_EQ(received.program(), uint8_t{42});
}

TEST(midi_parser, sysex_complete)
{
    bool got_sysex = false;
    size_t sysex_len = 0;

    MidiParser<64> parser(
        [&](MidiMessage const&) {},
        [&](SysexMessage<64> const& sx) {
            got_sysex = true;
            sysex_len = sx.length();
        });

    parser.parse(0xF0);
    parser.parse(0x7E);
    parser.parse(0x01);
    parser.parse(0x02);
    EXPECT_FALSE(got_sysex);
    parser.parse(0xF7);
    EXPECT_TRUE(got_sysex);
    EXPECT_EQ(sysex_len, size_t{5});
}

TEST(midi_parser, sysex_overflow)
{
    MidiError received_error{};
    bool got_error = false;

    MidiParser<4> parser(
        [&](MidiMessage const&) {},
        [&](SysexMessage<4> const&) {},
        [&](MidiError e) {
            received_error = e;
            got_error = true;
        });

    parser.parse(0xF0);
    parser.parse(0x01);
    parser.parse(0x02);
    parser.parse(0x03);
    EXPECT_FALSE(got_error);
    parser.parse(0x04);
    EXPECT_TRUE(got_error);
    EXPECT_EQ(received_error, MidiError::buffer_overflow);
}

TEST(midi_parser, realtime_interleaved)
{
    MidiMessage received;
    bool got_message = false;
    MidiMessage realtime_msg;
    int realtime_count = 0;

    MidiParser<64> parser([&](MidiMessage const& msg) {
        if (msg.is_realtime()) {
            realtime_msg = msg;
            ++realtime_count;
        } else {
            received = msg;
            got_message = true;
        }
    });

    parser.parse(0x90);
    parser.parse(60);
    parser.parse(0xF8);
    EXPECT_EQ(realtime_count, 1);
    EXPECT_EQ(realtime_msg.status(), uint8_t{0xF8});
    EXPECT_FALSE(got_message);
    parser.parse(100);
    EXPECT_TRUE(got_message);
    EXPECT_EQ(received.note(), uint8_t{60});
    EXPECT_EQ(received.velocity(), uint8_t{100});
}

TEST(midi_parser, reset_clears_state)
{
    int count = 0;
    MidiParser<64> parser([&](MidiMessage const&) { ++count; });

    parser.parse(0x90);
    parser.parse(60);
    parser.reset();
    parser.parse(100);
    EXPECT_EQ(count, 0);
}

TEST(midi_parser, unset_callbacks_are_noop)
{
    MidiParser<64> parser;
    parser.parse(0x90);
    parser.parse(60);
    parser.parse(100);
    parser.parse(0xF0);
    parser.parse(0x01);
    parser.parse(0xF7);
    EXPECT_TRUE(true);
}

// Minimal Type 0 MIDI file: header + one track with note-on + end-of-track
static constexpr uint8_t type0_midi[] = {
    // MThd
    'M',
    'T',
    'h',
    'd',
    0x00,
    0x00,
    0x00,
    0x06,
    0x00,
    0x00,  // format 0
    0x00,
    0x01,  // 1 track
    0x01,
    0xE0,  // division = 480
    // MTrk
    'M',
    'T',
    'r',
    'k',
    0x00,
    0x00,
    0x00,
    0x0D,  // chunk length = 13
    // delta=0, note-on ch0 note=60 vel=100
    0x00,
    0x90,
    0x3C,
    0x64,
    // delta=480 (0x83 0x60), note-off ch0 note=60 vel=0
    0x83,
    0x60,
    0x80,
    0x3C,
    0x00,
    // delta=0, meta end-of-track
    0x00,
    0xFF,
    0x2F,
    0x00,
};

TEST(midi_file_read, type0_header)
{
    int fmt = -1, ntrks = -1, div = -1;
    MidiFileCallbacks cb;
    cb.on_header = [&](int f, int n, int d) {
        fmt = f;
        ntrks = n;
        div = d;
    };

    auto const s = read_midi_file(std::span<uint8_t const>(type0_midi), cb);
    EXPECT_TRUE(is_success(s));
    EXPECT_EQ(fmt, 0);
    EXPECT_EQ(ntrks, 1);
    EXPECT_EQ(div, 480);
}

TEST(midi_file_read, type0_messages)
{
    struct Event
    {
        MidiTick tick;
        uint8_t status;
        uint8_t data1;
        uint8_t data2;
    };
    Event events[4]{};
    int count = 0;

    MidiFileCallbacks cb;
    cb.on_message = [&](MidiTick t, MidiMessage const& msg) {
        if (count < 4) {
            events[count] = {t, msg.status(), msg.data1(), msg.data2()};
        }
        ++count;
    };

    EXPECT_TRUE(is_success(read_midi_file(std::span<uint8_t const>(type0_midi), cb)));
    EXPECT_EQ(count, 2);
    EXPECT_EQ(events[0].tick, MidiTick{0});
    EXPECT_EQ(events[0].status, uint8_t{0x90});
    EXPECT_EQ(events[0].data1, uint8_t{60});
    EXPECT_EQ(events[0].data2, uint8_t{100});
    EXPECT_EQ(events[1].tick, MidiTick{480});
    EXPECT_EQ(events[1].status, uint8_t{0x80});
}

TEST(midi_file_read, type0_track_end_time)
{
    MidiTick end_time = 0;
    int end_track_num = -1;

    MidiFileCallbacks cb;
    cb.on_track_end = [&](MidiTick t, int trk) {
        end_time = t;
        end_track_num = trk;
    };

    EXPECT_TRUE(is_success(read_midi_file(std::span<uint8_t const>(type0_midi), cb)));
    EXPECT_EQ(end_time, MidiTick{480});
    EXPECT_EQ(end_track_num, 0);
}

TEST(midi_file_read, type0_meta_end_of_track)
{
    uint8_t meta_type = 0xFF;
    MidiTick meta_time = 999;

    MidiFileCallbacks cb;
    cb.on_meta = [&](MidiTick t, MetaEvent const& ev) {
        meta_type = ev.type;
        meta_time = t;
    };

    EXPECT_TRUE(is_success(read_midi_file(std::span<uint8_t const>(type0_midi), cb)));
    EXPECT_EQ(meta_type, meta::end_of_track);
    EXPECT_EQ(meta_time, MidiTick{480});
}

// Type 1: two tracks — tempo track + note track
static constexpr uint8_t type1_midi[] = {
    // MThd
    'M',
    'T',
    'h',
    'd',
    0x00,
    0x00,
    0x00,
    0x06,
    0x00,
    0x01,  // format 1
    0x00,
    0x02,  // 2 tracks
    0x01,
    0xE0,  // division = 480
    // Track 0: tempo track
    'M',
    'T',
    'r',
    'k',
    0x00,
    0x00,
    0x00,
    0x0B,
    // delta=0, meta tempo 120 BPM (500000 us = 0x07A120)
    0x00,
    0xFF,
    0x51,
    0x03,
    0x07,
    0xA1,
    0x20,
    // delta=0, end-of-track
    0x00,
    0xFF,
    0x2F,
    0x00,
    // Track 1: note track
    'M',
    'T',
    'r',
    'k',
    0x00,
    0x00,
    0x00,
    0x0D,  // chunk length = 13
    // delta=0, note-on ch0 C4 vel=100
    0x00,
    0x90,
    0x3C,
    0x64,
    // delta=480, note-off ch0 C4 vel=0
    0x83,
    0x60,
    0x80,
    0x3C,
    0x00,
    // delta=0, end-of-track
    0x00,
    0xFF,
    0x2F,
    0x00,
};

TEST(midi_file_read, type1_header)
{
    int fmt = -1, ntrks = -1;
    MidiFileCallbacks cb;
    cb.on_header = [&](int f, int n, int) {
        fmt = f;
        ntrks = n;
    };

    auto const s = read_midi_file(std::span<uint8_t const>(type1_midi), cb);
    EXPECT_TRUE(is_success(s));
    EXPECT_EQ(fmt, 1);
    EXPECT_EQ(ntrks, 2);
}

TEST(midi_file_read, type1_track_start_end)
{
    int starts[4]{};
    int ends[4]{};
    MidiTick end_times[4]{};
    int start_count = 0;
    int end_count = 0;

    MidiFileCallbacks cb;
    cb.on_track_start = [&](int trk) {
        if (start_count < 4) {
            starts[start_count++] = trk;
        }
    };
    cb.on_track_end = [&](MidiTick t, int trk) {
        if (end_count < 4) {
            ends[end_count] = trk;
            end_times[end_count] = t;
            ++end_count;
        }
    };

    EXPECT_TRUE(is_success(read_midi_file(std::span<uint8_t const>(type1_midi), cb)));
    EXPECT_EQ(start_count, 2);
    EXPECT_EQ(end_count, 2);
    EXPECT_EQ(starts[0], 0);
    EXPECT_EQ(starts[1], 1);
    EXPECT_EQ(ends[0], 0);
    EXPECT_EQ(ends[1], 1);
    EXPECT_EQ(end_times[0], MidiTick{0});
    EXPECT_EQ(end_times[1], MidiTick{480});
}

TEST(midi_file_read, type1_tempo_meta)
{
    float bpm = 0.0f;
    MidiFileCallbacks cb;
    cb.on_meta = [&](MidiTick, MetaEvent const& ev) {
        if (ev.type == meta::tempo) {
            auto const result = ev.tempo_bpm();
            if (result.has_value()) {
                bpm = result.value();
            }
        }
    };

    EXPECT_TRUE(is_success(read_midi_file(std::span<uint8_t const>(type1_midi), cb)));
    EXPECT_TRUE(bpm > 119.9f && bpm < 120.1f);
}

TEST(midi_file_read, truncated_header)
{
    uint8_t const bad[] = {'M', 'T', 'h', 'd', 0x00, 0x00};
    auto const s = read_midi_file(std::span<uint8_t const>(bad), MidiFileCallbacks{});
    EXPECT_TRUE(is_failure(s));
}

TEST(midi_file_read, bad_magic)
{
    uint8_t const bad[] = {'N', 'O', 'P', 'E', 0x00, 0x00, 0x00, 0x06, 0, 0, 0, 1, 0, 0x60};
    auto const s = read_midi_file(std::span<uint8_t const>(bad), MidiFileCallbacks{});
    EXPECT_TRUE(is_failure(s));
}

TEST(midi_file_read, unset_callbacks_noop)
{
    MidiFileCallbacks cb;
    auto const s = read_midi_file(std::span<uint8_t const>(type0_midi), cb);
    EXPECT_TRUE(is_success(s));
}

TEST(midi_error, error_codes_convert)
{
    auto const ec = make_error_code(MidiError::unexpected_byte);
    EXPECT_TRUE(ec.category().name() != nullptr);
    EXPECT_EQ(ec.value(), 1);
    EXPECT_FALSE(ec.message().empty());
}

TEST(midi_error, all_codes_have_messages)
{
    auto const codes = {
        MidiError::unexpected_byte,
        MidiError::buffer_overflow,
        MidiError::invalid_status,
        MidiError::malformed_file,
        MidiError::invalid_header,
        MidiError::unexpected_eof,
        MidiError::track_overflow,
        MidiError::invalid_vlq,
        MidiError::invalid_meta_event,
    };
    for (auto const code : codes) {
        auto const ec = make_error_code(code);
        EXPECT_FALSE(ec.message().empty());
    }
}

TEST(midi_error, status_integration)
{
    Status const s = failure(MidiError::unexpected_eof);
    EXPECT_TRUE(is_failure(s));
}

TEST(midi_file_write, header_bytes)
{
    std::array<uint8_t, 256> storage{};
    MutableBuffer buf(storage);
    MidiFileWriter writer(buf);

    EXPECT_TRUE(is_success(writer.write_header(0, 1, 480)));
    EXPECT_EQ(buf.size(), size_t{14});
    auto const s = buf.get_span();
    EXPECT_EQ(s[0], uint8_t{'M'});
    EXPECT_EQ(s[1], uint8_t{'T'});
    EXPECT_EQ(s[2], uint8_t{'h'});
    EXPECT_EQ(s[3], uint8_t{'d'});
    EXPECT_EQ(s[4], uint8_t{0});
    EXPECT_EQ(s[5], uint8_t{0});
    EXPECT_EQ(s[6], uint8_t{0});
    EXPECT_EQ(s[7], uint8_t{6});
    EXPECT_EQ(s[8], uint8_t{0});
    EXPECT_EQ(s[9], uint8_t{0});
    EXPECT_EQ(s[10], uint8_t{0});
    EXPECT_EQ(s[11], uint8_t{1});
    EXPECT_EQ(s[12], uint8_t{0x01});
    EXPECT_EQ(s[13], uint8_t{0xE0});
}

TEST(midi_file_write, single_track_note)
{
    std::array<uint8_t, 512> storage{};
    MutableBuffer buf(storage);
    MidiFileWriter writer(buf);

    EXPECT_TRUE(is_success(writer.write_header(0, 1, 480)));
    EXPECT_TRUE(is_success(writer.begin_track()));
    EXPECT_TRUE(is_success(writer.write_message(0, MidiMessage::note_on(0, 60, 100))));
    EXPECT_TRUE(is_success(writer.write_message(480, MidiMessage::note_off(0, 60, 0))));
    EXPECT_TRUE(is_success(writer.write_end_of_track(480)));
    EXPECT_TRUE(is_success(writer.end_track()));

    // Verify by reading it back
    int msg_count = 0;
    MidiTick last_tick = 0;
    MidiFileCallbacks cb;
    cb.on_message = [&](MidiTick t, MidiMessage const&) {
        last_tick = t;
        ++msg_count;
    };
    auto const result = read_midi_file(buf.get_span(), cb);
    EXPECT_TRUE(is_success(result));
    EXPECT_EQ(msg_count, 2);
    EXPECT_EQ(last_tick, MidiTick{480});
}

TEST(midi_file_write, sysex_f7_escape_roundtrips)
{
    std::array<uint8_t, 512> storage{};
    MutableBuffer buf(storage);
    MidiFileWriter writer(buf);

    std::array<uint8_t, 3> const normal_body{0x7E, 0x00, 0xF7};
    std::array<uint8_t, 2> const escape_body{0x11, 0x22};

    EXPECT_TRUE(is_success(writer.write_header(0, 1, 480)));
    EXPECT_TRUE(is_success(writer.begin_track()));
    EXPECT_TRUE(is_success(writer.write_sysex(0, normal_body)));         // 0xF0 default
    EXPECT_TRUE(is_success(writer.write_sysex(10, escape_body, 0xF7)));  // F7 escape
    EXPECT_TRUE(is_success(writer.write_end_of_track(10)));
    EXPECT_TRUE(is_success(writer.end_track()));

    // An invalid leading status must be rejected.
    EXPECT_TRUE(is_failure(writer.write_sysex(0, escape_body, 0x90)));

    std::vector<uint8_t> statuses;
    MidiFileCallbacks cb;
    cb.on_sysex = [&](MidiTick, SysexEvent const& sx) { statuses.push_back(sx.status); };
    auto const result = read_midi_file(buf.get_span(), cb);
    EXPECT_TRUE(is_success(result));
    // Both events round-trip with their original status byte, not rewritten to F0.
    EXPECT_EQ(statuses.size(), size_t{2});
    EXPECT_EQ(statuses[0], uint8_t{0xF0});
    EXPECT_EQ(statuses[1], uint8_t{0xF7});
}

TEST(midi_file_write, running_status_optimization)
{
    std::array<uint8_t, 512> storage{};
    MutableBuffer buf(storage);
    MidiFileWriter writer(buf);

    EXPECT_TRUE(is_success(writer.write_header(0, 1, 480)));
    EXPECT_TRUE(is_success(writer.begin_track()));
    EXPECT_TRUE(is_success(writer.write_message(0, MidiMessage::note_on(0, 60, 100))));
    auto const size_after_first = buf.size();
    EXPECT_TRUE(is_success(writer.write_message(0, MidiMessage::note_on(0, 64, 80))));
    auto const size_after_second = buf.size();

    // First: delta(1) + status(1) + data(2) = 4
    // Second: delta(1) + data(2) = 3 (running status omits status byte)
    auto const first_msg_size = size_after_first - 22;  // subtract header(14) + MTrk header(8)
    auto const second_msg_size = size_after_second - size_after_first;
    EXPECT_EQ(first_msg_size, size_t{4});
    EXPECT_EQ(second_msg_size, size_t{3});

    EXPECT_TRUE(is_success(writer.write_end_of_track(0)));
    EXPECT_TRUE(is_success(writer.end_track()));
}

TEST(midi_file_write, out_of_order_fails)
{
    std::array<uint8_t, 512> storage{};
    MutableBuffer buf(storage);
    MidiFileWriter writer(buf);

    EXPECT_TRUE(is_success(writer.write_header(0, 1, 480)));
    EXPECT_TRUE(is_success(writer.begin_track()));
    EXPECT_TRUE(is_success(writer.write_message(100, MidiMessage::note_on(0, 60, 100))));
    auto const result = writer.write_message(50, MidiMessage::note_off(0, 60, 0));
    EXPECT_TRUE(is_failure(result));
}

TEST(midi_file_write, write_outside_track_fails)
{
    std::array<uint8_t, 512> storage{};
    MutableBuffer buf(storage);
    MidiFileWriter writer(buf);

    EXPECT_TRUE(is_success(writer.write_header(0, 1, 480)));
    auto const result = writer.write_message(0, MidiMessage::note_on(0, 60, 100));
    EXPECT_TRUE(is_failure(result));
}

TEST(midi_file_write, type1_multitrack_roundtrip)
{
    std::array<uint8_t, 1024> storage{};
    MutableBuffer buf(storage);
    MidiFileWriter writer(buf);

    EXPECT_TRUE(is_success(writer.write_header(1, 2, 480)));

    // Track 0: tempo
    EXPECT_TRUE(is_success(writer.begin_track()));
    EXPECT_TRUE(is_success(writer.write_tempo(0, 500000)));
    EXPECT_TRUE(is_success(writer.write_end_of_track(0)));
    EXPECT_TRUE(is_success(writer.end_track()));

    // Track 1: notes
    EXPECT_TRUE(is_success(writer.begin_track()));
    EXPECT_TRUE(is_success(writer.write_message(0, MidiMessage::note_on(0, 60, 100))));
    EXPECT_TRUE(is_success(writer.write_message(480, MidiMessage::note_off(0, 60, 0))));
    EXPECT_TRUE(is_success(writer.write_end_of_track(480)));
    EXPECT_TRUE(is_success(writer.end_track()));

    // Read back
    int fmt = -1, ntrks = -1;
    float bpm = 0.0f;
    int msg_count = 0;

    MidiFileCallbacks cb;
    cb.on_header = [&](int f, int n, int) {
        fmt = f;
        ntrks = n;
    };
    cb.on_meta = [&](MidiTick, MetaEvent const& ev) {
        if (ev.type == meta::tempo) {
            auto const r = ev.tempo_bpm();
            if (r.has_value()) {
                bpm = r.value();
            }
        }
    };
    cb.on_message = [&](MidiTick, MidiMessage const&) { ++msg_count; };

    auto const result = read_midi_file(buf.get_span(), cb);
    EXPECT_TRUE(is_success(result));
    EXPECT_EQ(fmt, 1);
    EXPECT_EQ(ntrks, 2);
    EXPECT_TRUE(bpm > 119.9f && bpm < 120.1f);
    EXPECT_EQ(msg_count, 2);
}

TEST(midi_file_write, tempo_convenience)
{
    std::array<uint8_t, 256> storage{};
    MutableBuffer buf(storage);
    MidiFileWriter writer(buf);

    EXPECT_TRUE(is_success(writer.write_header(0, 1, 480)));
    EXPECT_TRUE(is_success(writer.begin_track()));
    EXPECT_TRUE(is_success(writer.write_tempo(0, 500000)));
    EXPECT_TRUE(is_success(writer.write_end_of_track(0)));
    EXPECT_TRUE(is_success(writer.end_track()));

    float bpm = 0.0f;
    MidiFileCallbacks cb;
    cb.on_meta = [&](MidiTick, MetaEvent const& ev) {
        if (ev.type == meta::tempo) {
            auto const r = ev.tempo_bpm();
            if (r.has_value()) {
                bpm = r.value();
            }
        }
    };
    EXPECT_TRUE(is_success(read_midi_file(buf.get_span(), cb)));
    EXPECT_TRUE(bpm > 119.9f && bpm < 120.1f);
}

TEST(midi_file_write, time_signature_convenience)
{
    std::array<uint8_t, 256> storage{};
    MutableBuffer buf(storage);
    MidiFileWriter writer(buf);

    EXPECT_TRUE(is_success(writer.write_header(0, 1, 480)));
    EXPECT_TRUE(is_success(writer.begin_track()));
    EXPECT_TRUE(is_success(writer.write_time_signature(0, 6, 3, 0x24, 8)));
    EXPECT_TRUE(is_success(writer.write_end_of_track(0)));
    EXPECT_TRUE(is_success(writer.end_track()));

    std::array<uint8_t, 4> ts{};
    MidiFileCallbacks cb;
    cb.on_meta = [&](MidiTick, MetaEvent const& ev) {
        if (ev.type == meta::time_signature) {
            auto const r = ev.time_signature();
            if (r.has_value()) {
                ts = r.value();
            }
        }
    };
    EXPECT_TRUE(is_success(read_midi_file(buf.get_span(), cb)));
    EXPECT_EQ(ts[0], uint8_t{6});
    EXPECT_EQ(ts[1], uint8_t{3});
}

TEST(midi_file_write, key_signature_convenience)
{
    std::array<uint8_t, 256> storage{};
    MutableBuffer buf(storage);
    MidiFileWriter writer(buf);

    EXPECT_TRUE(is_success(writer.write_header(0, 1, 480)));
    EXPECT_TRUE(is_success(writer.begin_track()));
    EXPECT_TRUE(is_success(writer.write_key_signature(0, -3, 1)));
    EXPECT_TRUE(is_success(writer.write_end_of_track(0)));
    EXPECT_TRUE(is_success(writer.end_track()));

    std::array<int8_t, 2> ks{};
    MidiFileCallbacks cb;
    cb.on_meta = [&](MidiTick, MetaEvent const& ev) {
        if (ev.type == meta::key_signature) {
            auto const r = ev.key_signature();
            if (r.has_value()) {
                ks = r.value();
            }
        }
    };
    EXPECT_TRUE(is_success(read_midi_file(buf.get_span(), cb)));
    EXPECT_EQ(ks[0], int8_t{-3});
    EXPECT_EQ(ks[1], int8_t{1});
}

TEST(midi_processor, identity_roundtrip)
{
    // Write a Type 1 file
    std::array<uint8_t, 1024> src_storage{};
    MutableBuffer src_buf(src_storage);
    MidiFileWriter src_writer(src_buf);
    EXPECT_TRUE(is_success(src_writer.write_header(1, 2, 480)));
    EXPECT_TRUE(is_success(src_writer.begin_track()));
    EXPECT_TRUE(is_success(src_writer.write_tempo(0, 500000)));
    EXPECT_TRUE(is_success(src_writer.write_end_of_track(0)));
    EXPECT_TRUE(is_success(src_writer.end_track()));
    EXPECT_TRUE(is_success(src_writer.begin_track()));
    EXPECT_TRUE(is_success(src_writer.write_message(0, MidiMessage::note_on(0, 60, 100))));
    EXPECT_TRUE(is_success(src_writer.write_message(480, MidiMessage::note_off(0, 60, 0))));
    EXPECT_TRUE(is_success(src_writer.write_end_of_track(480)));
    EXPECT_TRUE(is_success(src_writer.end_track()));
    auto const src_span = src_buf.get_span();

    // Copy through identity processor
    std::array<uint8_t, 1024> dst_storage{};
    MutableBuffer dst_buf(dst_storage);
    MidiFileWriter dst_writer(dst_buf);
    MidiFileProcessor proc;

    auto const s = process_midi_file(src_span, proc, dst_writer);
    EXPECT_TRUE(is_success(s));

    auto const dst_span = dst_buf.get_span();
    EXPECT_EQ(src_span.size(), dst_span.size());
    for (size_t i = 0; i < src_span.size(); ++i) {
        EXPECT_EQ(src_span[i], dst_span[i]);
    }
}

TEST(midi_processor, transpose_callback)
{
    std::array<uint8_t, 512> src_storage{};
    MutableBuffer src_buf(src_storage);
    MidiFileWriter src_writer(src_buf);
    EXPECT_TRUE(is_success(src_writer.write_header(0, 1, 480)));
    EXPECT_TRUE(is_success(src_writer.begin_track()));
    EXPECT_TRUE(is_success(src_writer.write_message(0, MidiMessage::note_on(0, 60, 100))));
    EXPECT_TRUE(is_success(src_writer.write_message(480, MidiMessage::note_off(0, 60, 0))));
    EXPECT_TRUE(is_success(src_writer.write_end_of_track(480)));
    EXPECT_TRUE(is_success(src_writer.end_track()));

    std::array<uint8_t, 512> dst_storage{};
    MutableBuffer dst_buf(dst_storage);
    MidiFileWriter dst_writer(dst_buf);
    MidiFileProcessor proc;
    proc.process_message = [](MidiTick&, MidiMessage& msg) {
        if (msg.is_note_on() || msg.is_note_off()) {
            msg = MidiMessage(msg.status(), static_cast<uint8_t>(msg.note() + 12), msg.data2());
        }
    };

    EXPECT_TRUE(is_success(process_midi_file(src_buf.get_span(), proc, dst_writer)));

    uint8_t note = 0;
    MidiFileCallbacks cb;
    cb.on_message = [&](MidiTick, MidiMessage const& msg) {
        if (msg.is_note_on()) {
            note = msg.note();
        }
    };
    EXPECT_TRUE(is_success(read_midi_file(dst_buf.get_span(), cb)));
    EXPECT_EQ(note, uint8_t{72});
}

TEST(midi_fileshow, type0_output_contains_header)
{
    FILE* tmp = std::tmpfile();
    EXPECT_TRUE(tmp != nullptr);

    auto cb = make_fileshow_callbacks(tmp);
    EXPECT_TRUE(is_success(read_midi_file(std::span<uint8_t const>(type0_midi), cb)));

    std::rewind(tmp);
    std::array<char, 4096> buf{};
    auto const len = std::fread(buf.data(), 1, buf.size() - 1, tmp);
    buf[len] = '\0';
    std::fclose(tmp);

    std::string_view output(buf.data(), len);
    EXPECT_TRUE(output.find("Header: format=0 tracks=1 division=480") != std::string_view::npos);
    EXPECT_TRUE(output.find("Note On ch=0 note=60 vel=100") != std::string_view::npos);
    EXPECT_TRUE(output.find("Note Off ch=0 note=60 vel=0") != std::string_view::npos);
    EXPECT_TRUE(output.find("End of Track") != std::string_view::npos);
    EXPECT_TRUE(output.find("Track 0 start") != std::string_view::npos);
    EXPECT_TRUE(output.find("Track 0 end") != std::string_view::npos);
}

TEST(midi_fileshow, type1_output_contains_tempo)
{
    FILE* tmp = std::tmpfile();
    EXPECT_TRUE(tmp != nullptr);

    auto cb = make_fileshow_callbacks(tmp);
    EXPECT_TRUE(is_success(read_midi_file(std::span<uint8_t const>(type1_midi), cb)));

    std::rewind(tmp);
    std::array<char, 4096> buf{};
    auto const len = std::fread(buf.data(), 1, buf.size() - 1, tmp);
    buf[len] = '\0';
    std::fclose(tmp);

    std::string_view output(buf.data(), len);
    EXPECT_TRUE(output.find("Header: format=1 tracks=2 division=480") != std::string_view::npos);
    EXPECT_TRUE(output.find("Tempo 500000 us/beat") != std::string_view::npos);
    EXPECT_TRUE(output.find("120.00 BPM") != std::string_view::npos);
    EXPECT_TRUE(output.find("Track 1 start") != std::string_view::npos);
}

TEST(midi_matrix, initial_state)
{
    MidiMatrix matrix;
    EXPECT_EQ(matrix.total_count(), 0);
    EXPECT_EQ(matrix.channel_count(0), 0);
    EXPECT_EQ(matrix.note_count(0, 60), uint8_t{0});
    EXPECT_FALSE(matrix.hold_pedal(0));
}

TEST(midi_matrix, note_on_increments)
{
    MidiMatrix matrix;
    matrix.process(MidiMessage::note_on(0, 60, 100));
    EXPECT_EQ(matrix.total_count(), 1);
    EXPECT_EQ(matrix.channel_count(0), 1);
    EXPECT_EQ(matrix.note_count(0, 60), uint8_t{1});
}

TEST(midi_matrix, note_off_decrements)
{
    MidiMatrix matrix;
    matrix.process(MidiMessage::note_on(0, 60, 100));
    matrix.process(MidiMessage::note_off(0, 60));
    EXPECT_EQ(matrix.total_count(), 0);
    EXPECT_EQ(matrix.channel_count(0), 0);
    EXPECT_EQ(matrix.note_count(0, 60), uint8_t{0});
}

TEST(midi_matrix, note_on_vel_zero_is_note_off)
{
    MidiMatrix matrix;
    matrix.process(MidiMessage::note_on(0, 60, 100));
    matrix.process(MidiMessage::note_on(0, 60, 0));
    EXPECT_EQ(matrix.total_count(), 0);
    EXPECT_EQ(matrix.note_count(0, 60), uint8_t{0});
}

TEST(midi_matrix, multiple_notes_same_channel)
{
    MidiMatrix matrix;
    matrix.process(MidiMessage::note_on(0, 60, 100));
    matrix.process(MidiMessage::note_on(0, 64, 80));
    matrix.process(MidiMessage::note_on(0, 67, 90));
    EXPECT_EQ(matrix.channel_count(0), 3);
    EXPECT_EQ(matrix.total_count(), 3);
}

TEST(midi_matrix, multiple_channels)
{
    MidiMatrix matrix;
    matrix.process(MidiMessage::note_on(0, 60, 100));
    matrix.process(MidiMessage::note_on(1, 64, 80));
    matrix.process(MidiMessage::note_on(2, 67, 90));
    EXPECT_EQ(matrix.channel_count(0), 1);
    EXPECT_EQ(matrix.channel_count(1), 1);
    EXPECT_EQ(matrix.channel_count(2), 1);
    EXPECT_EQ(matrix.total_count(), 3);
}

TEST(midi_matrix, double_note_on_counted)
{
    MidiMatrix matrix;
    matrix.process(MidiMessage::note_on(0, 60, 100));
    matrix.process(MidiMessage::note_on(0, 60, 90));
    EXPECT_EQ(matrix.note_count(0, 60), uint8_t{2});
    EXPECT_EQ(matrix.channel_count(0), 2);
    EXPECT_EQ(matrix.total_count(), 2);
}

TEST(midi_matrix, note_off_without_note_on_no_underflow)
{
    MidiMatrix matrix;
    matrix.process(MidiMessage::note_off(0, 60));
    EXPECT_EQ(matrix.total_count(), 0);
    EXPECT_EQ(matrix.channel_count(0), 0);
    EXPECT_EQ(matrix.note_count(0, 60), uint8_t{0});
}

TEST(midi_matrix, sustain_pedal)
{
    MidiMatrix matrix;
    EXPECT_FALSE(matrix.hold_pedal(0));
    matrix.process(MidiMessage::control_change(0, 64, 127));
    EXPECT_TRUE(matrix.hold_pedal(0));
    matrix.process(MidiMessage::control_change(0, 64, 0));
    EXPECT_FALSE(matrix.hold_pedal(0));
}

TEST(midi_matrix, sustain_pedal_threshold)
{
    MidiMatrix matrix;
    matrix.process(MidiMessage::control_change(0, 64, 63));
    EXPECT_FALSE(matrix.hold_pedal(0));
    matrix.process(MidiMessage::control_change(0, 64, 64));
    EXPECT_TRUE(matrix.hold_pedal(0));
}

TEST(midi_matrix, all_notes_off)
{
    MidiMatrix matrix;
    matrix.process(MidiMessage::note_on(0, 60, 100));
    matrix.process(MidiMessage::note_on(0, 64, 80));
    matrix.process(MidiMessage::note_on(1, 72, 90));
    EXPECT_EQ(matrix.total_count(), 3);

    // CC 123 = All Notes Off on channel 0
    matrix.process(MidiMessage::control_change(0, 123, 0));
    EXPECT_EQ(matrix.channel_count(0), 0);
    EXPECT_EQ(matrix.channel_count(1), 1);
    EXPECT_EQ(matrix.total_count(), 1);
}

TEST(midi_matrix, clear)
{
    MidiMatrix matrix;
    matrix.process(MidiMessage::note_on(0, 60, 100));
    matrix.process(MidiMessage::note_on(1, 64, 80));
    matrix.process(MidiMessage::control_change(0, 64, 127));
    matrix.clear();
    EXPECT_EQ(matrix.total_count(), 0);
    EXPECT_EQ(matrix.channel_count(0), 0);
    EXPECT_EQ(matrix.channel_count(1), 0);
    EXPECT_FALSE(matrix.hold_pedal(0));
}

TEST(midi_matrix, clear_channel)
{
    MidiMatrix matrix;
    matrix.process(MidiMessage::note_on(0, 60, 100));
    matrix.process(MidiMessage::note_on(1, 64, 80));
    matrix.clear_channel(0);
    EXPECT_EQ(matrix.channel_count(0), 0);
    EXPECT_EQ(matrix.channel_count(1), 1);
    EXPECT_EQ(matrix.total_count(), 1);
}

TEST(midi_matrix, ignores_non_channel_messages)
{
    MidiMatrix matrix;
    matrix.process(MidiMessage(status::timing_clock));
    matrix.process(MidiMessage(status::tune_request));
    EXPECT_EQ(matrix.total_count(), 0);
}

//
// Test Runner
//
//
// MIDI file reader: decoder safety. MIDI files are typically local and
// trusted, but read_midi_file walks attacker-controllable chunk lengths
// and VLQ-encoded deltas, so the bounds checks still need to hold.
//

TEST(midi_file_safety, mtrk_chunk_length_exceeds_file)
{
    // Valid MThd + MTrk magic, but MTrk length claims 100 bytes when
    // only 4 bytes of track data are actually present.
    uint8_t const bad[] = {
        'M',  'T',  'h',  'd',  0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x01, 0x01, 0xE0,  // format 0, 1 track, div=480
        'M',  'T',  'r',  'k',  0x00, 0x00, 0x00, 0x64,                                      // mtrk_len = 100 (lie)
        0x00, 0xFF, 0x2F, 0x00,                                                              // only 4 bytes of track
    };
    auto const s = read_midi_file(std::span<uint8_t const>(bad), MidiFileCallbacks{});
    EXPECT_TRUE(is_failure(s));
}

TEST(midi_file_safety, mtrk_magic_corrupt_reports_malformed)
{
    // Header says 1 track, but track magic is wrong.
    uint8_t const bad[] = {
        'M',  'T',  'h',  'd', 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
        0x01, 0x01, 0xE0, 'N', 'O',  'P',  'E',  0x00, 0x00, 0x00, 0x00,  // wrong MTrk magic
    };
    auto const s = read_midi_file(std::span<uint8_t const>(bad), MidiFileCallbacks{});
    EXPECT_TRUE(is_failure(s));
}

TEST(midi_file_safety, meta_event_length_overruns_track)
{
    // Meta event claims 100 bytes of payload inside a 6-byte track.
    // 100 VLQ = single byte 0x64. Reader must trigger unexpected_eof.
    uint8_t const bad[] = {
        'M', 'T', 'h', 'd',  0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x01, 0x01, 0xE0, 'M',
        'T', 'r', 'k', 0x00, 0x00, 0x00, 0x06, 0x00, 0xFF, 0x01, 0x64, 0xAA, 0xBB,  // meta text type=0x01 len=100, only 2 data bytes present
    };
    bool saw_eof = false;
    MidiFileCallbacks cb;
    cb.on_error = [&](MidiError e) {
        if (e == MidiError::unexpected_eof) {
            saw_eof = true;
        }
    };
    (void)read_midi_file(std::span<uint8_t const>(bad), cb);
    EXPECT_TRUE(saw_eof);
}

TEST(midi_file_safety, running_status_without_prior_status_errors)
{
    // First event in track is a data byte with the high bit clear, and
    // there is no running_status yet. Reader must flag invalid_status.
    uint8_t const bad[] = {
        'M',  'T', 'h', 'd', 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x01, 0x01,
        0xE0, 'M', 'T', 'r', 'k',  0x00, 0x00, 0x00, 0x03, 0x00, 0x3C, 0x64,  // delta=0, data byte 0x3C (< 0x80) with no running status
    };
    bool saw_invalid_status = false;
    MidiFileCallbacks cb;
    cb.on_error = [&](MidiError e) {
        if (e == MidiError::invalid_status) {
            saw_invalid_status = true;
        }
    };
    (void)read_midi_file(std::span<uint8_t const>(bad), cb);
    EXPECT_TRUE(saw_invalid_status);
}

TEST(midi_file_safety, channel_message_truncated_at_track_end)
{
    // Note-on (0x90) requires two data bytes, but only one is present
    // before the track ends.
    uint8_t const bad[] = {
        'M',  'T', 'h', 'd', 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x01, 0x01,
        0xE0, 'M', 'T', 'r', 'k',  0x00, 0x00, 0x00, 0x03, 0x00, 0x90, 0x3C,  // delta=0, note-on, only data1 (missing data2)
    };
    bool saw_eof = false;
    MidiFileCallbacks cb;
    cb.on_error = [&](MidiError e) {
        if (e == MidiError::unexpected_eof) {
            saw_eof = true;
        }
    };
    (void)read_midi_file(std::span<uint8_t const>(bad), cb);
    EXPECT_TRUE(saw_eof);
}

TEST(midi_file_safety, header_declares_ntrks_but_file_truncated)
{
    // Header says 2 tracks, only 1 track present.
    uint8_t const bad[] = {
        'M',  'T',  'h', 'd', 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x02,              // ntrks=2 (lie)
        0x01, 0xE0, 'M', 'T', 'r',  'k',  0x00, 0x00, 0x00, 0x04, 0x00, 0xFF, 0x2F, 0x00,  // track 0 end
        // track 1 absent entirely
    };
    auto const s = read_midi_file(std::span<uint8_t const>(bad), MidiFileCallbacks{});
    EXPECT_TRUE(is_failure(s));
}

TEST(midi_file_safety, ntrks_zero_is_valid_header)
{
    // Edge case: header with ntrks=0 should parse successfully, just
    // producing a header callback and no tracks.
    uint8_t const data[] = {
        'M',
        'T',
        'h',
        'd',
        0x00,
        0x00,
        0x00,
        0x06,
        0x00,
        0x00,
        0x00,
        0x00,  // ntrks = 0
        0x01,
        0xE0,
    };
    int ntrks_seen = -1;
    MidiFileCallbacks cb;
    cb.on_header = [&](int /*fmt*/, int n, int /*div*/) { ntrks_seen = n; };
    auto const s = read_midi_file(std::span<uint8_t const>(data), cb);
    EXPECT_TRUE(is_success(s));
    EXPECT_EQ(ntrks_seen, 0);
}

TEST(midi_matrix, note_count_saturates_without_desync)
{
    // Stacking more than 255 note-ons on one note must not wrap the uint8_t
    // per-note counter and leave it disagreeing with the channel/total tallies.
    MidiMatrix matrix;
    for (int i = 0; i < 300; ++i) {
        matrix.process(MidiMessage::note_on(0, 60, 100));
    }
    EXPECT_EQ(matrix.note_count(0, 60), uint8_t{255});
    EXPECT_EQ(matrix.channel_count(0), 255);
    EXPECT_EQ(matrix.total_count(), 255);

    // Every note-off decrements in lockstep down to a clean zero.
    for (int i = 0; i < 255; ++i) {
        matrix.process(MidiMessage::note_off(0, 60, 0));
    }
    EXPECT_EQ(matrix.note_count(0, 60), uint8_t{0});
    EXPECT_EQ(matrix.channel_count(0), 0);
    EXPECT_EQ(matrix.total_count(), 0);
}

TEST_MAIN(statusbar_midi, midi_test)
