// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/midi/midi_file_writer.hpp"

#include "statusbar/buffer/span_utils.hpp"
#include "statusbar/ieee/ieee.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace statusbar::midi {

auto MidiFileWriter::append_byte(uint8_t val) -> Status
{
    return output_.append(std::span<uint8_t const>(&val, 1));
}

// SMF multi-byte fields are big-endian; ieee::doublet_t / quadlet_t hold network
// byte order and span_store writes their raw bytes.
auto MidiFileWriter::append_u16(uint16_t val) -> Status
{
    std::array<uint8_t, 2> buf{};
    span_store(buf, statusbar::ieee::doublet_t{val});
    return output_.append(std::span<uint8_t const>(buf));
}

auto MidiFileWriter::append_u32(uint32_t val) -> Status
{
    std::array<uint8_t, 4> buf{};
    span_store(buf, statusbar::ieee::quadlet_t{val});
    return output_.append(std::span<uint8_t const>(buf));
}

auto MidiFileWriter::write_delta_time(MidiTick time) -> Status
{
    if (time < track_time_) {
        return failure(MidiError::malformed_file);
    }
    auto const delta = time - track_time_;
    track_time_ = time;
    return detail::write_vlq(output_, delta);
}

auto MidiFileWriter::write_header(int format, int ntrks, int division) -> Status
{
    auto s = append_u32(0x4D546864u);  // MThd
    if (is_failure(s)) {
        return s;
    }
    s = append_u32(6);
    if (is_failure(s)) {
        return s;
    }
    s = append_u16(static_cast<uint16_t>(format));
    if (is_failure(s)) {
        return s;
    }
    s = append_u16(static_cast<uint16_t>(ntrks));
    if (is_failure(s)) {
        return s;
    }
    return append_u16(static_cast<uint16_t>(division));
}

auto MidiFileWriter::begin_track() -> Status
{
    if (in_track_) {
        return failure(MidiError::malformed_file);
    }

    auto s = append_u32(0x4D54726Bu);  // MTrk
    if (is_failure(s)) {
        return s;
    }

    track_length_offset_ = output_.size();
    s = append_u32(0);  // placeholder
    if (is_failure(s)) {
        return s;
    }

    track_data_start_ = output_.size();
    track_time_ = 0;
    running_status_ = 0;
    in_track_ = true;
    return success();
}

auto MidiFileWriter::end_track() -> Status
{
    if (!in_track_) {
        return failure(MidiError::malformed_file);
    }

    auto const track_length = static_cast<uint32_t>(output_.size() - track_data_start_);
    std::array<uint8_t, 4> len_bytes{};
    span_store(len_bytes, statusbar::ieee::quadlet_t{track_length});
    auto const s = output_.store(track_length_offset_, std::span<uint8_t const>(len_bytes));
    if (is_failure(s)) {
        return s;
    }

    in_track_ = false;
    return success();
}

auto MidiFileWriter::write_message(MidiTick time, MidiMessage const& msg) -> Status
{
    if (!in_track_) {
        return failure(MidiError::malformed_file);
    }

    auto s = write_delta_time(time);
    if (is_failure(s)) {
        return s;
    }

    auto const count = data_byte_count(msg.status());

    if (msg.is_channel_message() && msg.status() == running_status_) {
        // Running status: omit status byte
    } else {
        s = append_byte(msg.status());
        if (is_failure(s)) {
            return s;
        }
        if (msg.is_channel_message()) {
            running_status_ = msg.status();
        } else {
            running_status_ = 0;
        }
    }

    if (count >= 1) {
        s = append_byte(msg.data1());
        if (is_failure(s)) {
            return s;
        }
    }
    if (count >= 2) {
        s = append_byte(msg.data2());
        if (is_failure(s)) {
            return s;
        }
    }

    return success();
}

auto MidiFileWriter::write_sysex(MidiTick time, std::span<uint8_t const> data, uint8_t status) -> Status
{
    if (!in_track_) {
        return failure(MidiError::malformed_file);
    }
    if (status != 0xF0 && status != 0xF7) {
        return failure(MidiError::invalid_status);
    }

    auto s = write_delta_time(time);
    if (is_failure(s)) {
        return s;
    }

    running_status_ = 0;

    s = append_byte(status);
    if (is_failure(s)) {
        return s;
    }

    s = detail::write_vlq(output_, static_cast<uint32_t>(data.size()));
    if (is_failure(s)) {
        return s;
    }

    return output_.append(data);
}

auto MidiFileWriter::write_meta(MidiTick time, uint8_t meta_type, std::span<uint8_t const> data) -> Status
{
    if (!in_track_) {
        return failure(MidiError::malformed_file);
    }

    auto s = write_delta_time(time);
    if (is_failure(s)) {
        return s;
    }

    running_status_ = 0;

    s = append_byte(0xFF);
    if (is_failure(s)) {
        return s;
    }
    s = append_byte(meta_type);
    if (is_failure(s)) {
        return s;
    }
    s = detail::write_vlq(output_, static_cast<uint32_t>(data.size()));
    if (is_failure(s)) {
        return s;
    }

    if (!data.empty()) {
        s = output_.append(data);
        if (is_failure(s)) {
            return s;
        }
    }

    return success();
}

auto MidiFileWriter::write_tempo(MidiTick time, uint32_t us_per_beat) -> Status
{
    std::array<uint8_t, 3> data = {
        static_cast<uint8_t>((us_per_beat >> 16) & 0xFF),
        static_cast<uint8_t>((us_per_beat >> 8) & 0xFF),
        static_cast<uint8_t>(us_per_beat & 0xFF),
    };
    return write_meta(time, meta::tempo, std::span<uint8_t const>(data));
}

auto MidiFileWriter::write_time_signature(
    MidiTick time, uint8_t num, uint8_t denom_power, uint8_t clocks_per_click, uint8_t notated_32nds) -> Status
{
    std::array<uint8_t, 4> data = {num, denom_power, clocks_per_click, notated_32nds};
    return write_meta(time, meta::time_signature, std::span<uint8_t const>(data));
}

auto MidiFileWriter::write_key_signature(MidiTick time, int8_t sharps_flats, uint8_t minor) -> Status
{
    std::array<uint8_t, 2> data = {
        static_cast<uint8_t>(sharps_flats),
        minor,
    };
    return write_meta(time, meta::key_signature, std::span<uint8_t const>(data));
}

auto MidiFileWriter::write_end_of_track(MidiTick time) -> Status
{
    return write_meta(time, meta::end_of_track, std::span<uint8_t const>{});
}

}  // namespace statusbar::midi
