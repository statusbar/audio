// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/midi/midi_file_reader.hpp"

#include "statusbar/midi/midi_detail.hpp"
#include "statusbar/midi/midi_error.hpp"
#include "statusbar/midi/midi_meta.hpp"
#include "statusbar/midi/midi_types.hpp"
#include "statusbar/status/status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace statusbar::midi {

namespace {

auto read_u16(std::span<uint8_t const> data, size_t& offset) -> StatusValue<uint16_t>
{
    if (offset + 2 > data.size()) {
        return failure(MidiError::unexpected_eof);
    }
    auto const val = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
    offset += 2;
    return val;
}

auto read_u32(std::span<uint8_t const> data, size_t& offset) -> StatusValue<uint32_t>
{
    if (offset + 4 > data.size()) {
        return failure(MidiError::unexpected_eof);
    }
    auto const val = static_cast<uint32_t>(data[offset]) << 24 | static_cast<uint32_t>(data[offset + 1]) << 16 |
        static_cast<uint32_t>(data[offset + 2]) << 8 | static_cast<uint32_t>(data[offset + 3]);
    offset += 4;
    return val;
}

// Track-event handlers. Caller has already advanced `offset` past the
// status byte for meta / sysex; channel-message handles both fresh
// status and running-status forms itself. Each returns true to continue
// the track loop, false when a fatal parse error has been reported and
// the caller should break out.

auto handle_meta_event(
    std::span<uint8_t const> data,
    size_t& offset,
    size_t track_end_offset,
    MidiTick abs_time,
    int trk,
    auto const& on_meta,
    auto const& on_track_end,
    auto const& on_error) -> bool
{
    if (offset >= track_end_offset) {
        on_error(MidiError::unexpected_eof);
        return false;
    }
    auto const meta_type = data[offset++];
    auto const meta_len = detail::read_vlq(data, offset);
    if (!meta_len.has_value()) {
        on_error(MidiError::invalid_vlq);
        return false;
    }
    if (offset + meta_len.value() > track_end_offset) {
        on_error(MidiError::unexpected_eof);
        return false;
    }
    auto const meta_data = data.subspan(offset, meta_len.value());
    offset += meta_len.value();
    MetaEvent const ev{.type = meta_type, .data = meta_data};
    on_meta(abs_time, ev);
    if (meta_type == meta::end_of_track) {
        on_track_end(abs_time, trk);
    }
    return true;
}

auto handle_sysex_event(
    std::span<uint8_t const> data,
    size_t& offset,
    size_t track_end_offset,
    MidiTick abs_time,
    uint8_t event_byte,
    auto const& on_sysex,
    auto const& on_error) -> bool
{
    auto const sysex_len = detail::read_vlq(data, offset);
    if (!sysex_len.has_value()) {
        on_error(MidiError::invalid_vlq);
        return false;
    }
    if (offset + sysex_len.value() > track_end_offset) {
        on_error(MidiError::unexpected_eof);
        return false;
    }
    SysexEvent const ev{.status = event_byte, .body = data.subspan(offset, sysex_len.value())};
    offset += sysex_len.value();
    on_sysex(abs_time, ev);
    return true;
}

auto handle_channel_message(
    std::span<uint8_t const> data,
    size_t& offset,
    size_t track_end_offset,
    MidiTick abs_time,
    uint8_t event_byte,
    uint8_t& running_status,
    auto const& on_message,
    auto const& on_error) -> bool
{
    uint8_t status = running_status;
    uint8_t d1 = 0;
    uint8_t d2 = 0;
    if (event_byte >= 0x80) {
        running_status = event_byte;
        status = event_byte;
        ++offset;
        auto const count = data_byte_count(running_status);
        if (count < 0 || offset + count > track_end_offset) {
            on_error(MidiError::unexpected_eof);
            return false;
        }
        d1 = count >= 1 ? data[offset] : 0;
        d2 = count >= 2 ? data[offset + 1] : 0;
        offset += count;
    } else {
        if (running_status == 0) {
            on_error(MidiError::invalid_status);
            return false;
        }
        auto const count = data_byte_count(running_status);
        d1 = event_byte;
        if (count >= 2) {
            if (offset + 1 >= track_end_offset) {
                on_error(MidiError::unexpected_eof);
                return false;
            }
            d2 = data[offset + 1];
            offset += 2;
        } else {
            offset += 1;
        }
    }
    on_message(abs_time, MidiMessage(status, d1, d2));
    return true;
}

}  // namespace

auto read_midi_file(std::span<uint8_t const> data, MidiFileCallbacks const& cb) -> Status
{
    auto const on_header = cb.on_header ? cb.on_header : decltype(cb.on_header){[](int, int, int) {}};
    auto const on_track_start = cb.on_track_start ? cb.on_track_start : decltype(cb.on_track_start){[](int) {}};
    auto const on_track_end = cb.on_track_end ? cb.on_track_end : decltype(cb.on_track_end){[](MidiTick, int) {}};
    auto const on_message = cb.on_message ? cb.on_message : decltype(cb.on_message){[](MidiTick, MidiMessage const&) {}};
    auto const on_sysex = cb.on_sysex ? cb.on_sysex : decltype(cb.on_sysex){[](MidiTick, SysexEvent const&) {}};
    auto const on_meta = cb.on_meta ? cb.on_meta : decltype(cb.on_meta){[](MidiTick, MetaEvent const&) {}};
    auto const on_error = cb.on_error ? cb.on_error : decltype(cb.on_error){[](MidiError) {}};

    size_t offset = 0;

    // Read MThd header
    auto const mthd_id = read_u32(data, offset);
    if (!mthd_id.has_value()) {
        return failure(MidiError::unexpected_eof);
    }
    if (mthd_id.value() != 0x4D546864u) {
        return failure(MidiError::invalid_header);
    }

    auto const mthd_len = read_u32(data, offset);
    if (!mthd_len.has_value()) {
        return failure(MidiError::unexpected_eof);
    }
    if (mthd_len.value() < 6) {
        return failure(MidiError::invalid_header);
    }

    auto const format = read_u16(data, offset);
    if (!format.has_value()) {
        return failure(MidiError::unexpected_eof);
    }

    auto const ntrks = read_u16(data, offset);
    if (!ntrks.has_value()) {
        return failure(MidiError::unexpected_eof);
    }

    auto const division = read_u16(data, offset);
    if (!division.has_value()) {
        return failure(MidiError::unexpected_eof);
    }

    if (mthd_len.value() > 6) {
        offset += mthd_len.value() - 6;
    }

    on_header(static_cast<int>(format.value()), static_cast<int>(ntrks.value()), static_cast<int>(division.value()));

    for (int trk = 0; trk < static_cast<int>(ntrks.value()); ++trk) {
        auto const mtrk_id = read_u32(data, offset);
        if (!mtrk_id.has_value()) {
            return failure(MidiError::unexpected_eof);
        }
        if (mtrk_id.value() != 0x4D54726Bu) {
            return failure(MidiError::malformed_file);
        }

        auto const mtrk_len = read_u32(data, offset);
        if (!mtrk_len.has_value()) {
            return failure(MidiError::unexpected_eof);
        }

        auto const track_end_offset = offset + mtrk_len.value();
        if (track_end_offset > data.size()) {
            return failure(MidiError::unexpected_eof);
        }

        on_track_start(trk);

        MidiTick abs_time = 0;
        uint8_t running_status = 0;

        while (offset < track_end_offset) {
            auto const delta = detail::read_vlq(data, offset);
            if (!delta.has_value()) {
                on_error(MidiError::invalid_vlq);
                break;
            }
            abs_time += delta.value();

            if (offset >= track_end_offset) {
                on_error(MidiError::unexpected_eof);
                break;
            }

            auto const event_byte = data[offset];
            bool keep_going = true;
            if (event_byte == 0xFF) {
                ++offset;  // consume 0xFF status byte
                keep_going = handle_meta_event(data, offset, track_end_offset, abs_time, trk, on_meta, on_track_end, on_error);
            } else if (event_byte == 0xF0 || event_byte == 0xF7) {
                ++offset;  // consume F0 / F7 status byte
                keep_going = handle_sysex_event(data, offset, track_end_offset, abs_time, event_byte, on_sysex, on_error);
            } else {
                keep_going = handle_channel_message(
                    data, offset, track_end_offset, abs_time, event_byte, running_status, on_message, on_error);
            }
            if (!keep_going) {
                break;
            }
        }

        offset = track_end_offset;
    }

    return success();
}

}  // namespace statusbar::midi
