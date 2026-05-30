// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/midi/midi_fileshow.hpp"

#include "statusbar/midi/midi_error.hpp"
#include "statusbar/midi/midi_meta.hpp"
#include "statusbar/midi/midi_sysex.hpp"
#include "statusbar/midi/midi_types.hpp"

#include <cstddef>
#include <cstdio>
#include <print>

namespace statusbar::midi {

auto make_fileshow_callbacks(FILE* out) -> MidiFileCallbacks
{
    MidiFileCallbacks cb;

    cb.on_header = [out](int format, int ntrks, int division) {
        std::print(out, "Header: format={} tracks={} division={}\n", format, ntrks, division);
    };

    cb.on_track_start = [out](int track_num) { std::print(out, "Track {} start\n", track_num); };

    cb.on_track_end = [out](MidiTick time, int track_num) { std::print(out, "Track {} end (tick {})\n", track_num, time); };

    cb.on_message = [out](MidiTick time, MidiMessage const& msg) {
        switch (msg.type()) {
            case status::note_on:
                if (msg.velocity() > 0) {
                    std::print(out, "[{:6}] Note On ch={} note={} vel={}\n", time, msg.channel(), msg.note(), msg.velocity());
                } else {
                    std::print(out, "[{:6}] Note Off ch={} note={} vel=0\n", time, msg.channel(), msg.note());
                }
                break;
            case status::note_off:
                std::print(out, "[{:6}] Note Off ch={} note={} vel={}\n", time, msg.channel(), msg.note(), msg.velocity());
                break;
            case status::control_change:
                std::print(
                    out,
                    "[{:6}] Control Change ch={} ctrl={} val={}\n",
                    time,
                    msg.channel(),
                    msg.controller(),
                    msg.controller_value());
                break;
            case status::program_change:
                std::print(out, "[{:6}] Program Change ch={} pg={}\n", time, msg.channel(), msg.program());
                break;
            case status::pitch_bend:
                std::print(out, "[{:6}] Pitch Bend ch={} val={}\n", time, msg.channel(), msg.pitch_bend());
                break;
            case status::channel_pressure:
                std::print(out, "[{:6}] Channel Pressure ch={} val={}\n", time, msg.channel(), msg.data1());
                break;
            case status::poly_pressure:
                std::print(out, "[{:6}] Poly Pressure ch={} note={} val={}\n", time, msg.channel(), msg.note(), msg.data2());
                break;
            default:
                std::print(out, "[{:6}] Status 0x{:02X} data1={} data2={}\n", time, msg.status(), msg.data1(), msg.data2());
                break;
        }
    };

    cb.on_sysex = [out](MidiTick time, SysexEvent const& sx) {
        auto const total_len = size_t{1} + sx.body.size();
        std::print(out, "[{:6}] SysEx ({} bytes): {:02X}", time, total_len, sx.status);
        for (auto const b : sx.body) {
            std::print(out, " {:02X}", b);
        }
        std::print(out, "\n");
    };

    cb.on_meta = [out](MidiTick time, MetaEvent const& ev) {
        switch (ev.type) {
            case meta::tempo: {
                auto const us = ev.tempo_us_per_beat();
                if (us.has_value()) {
                    std::print(
                        out,
                        "[{:6}] Tempo {} us/beat ({:.2f} BPM)\n",
                        time,
                        us.value(),
                        60'000'000.0 / static_cast<double>(us.value()));
                } else {
                    std::print(out, "[{:6}] Tempo (invalid data)\n", time);
                }
                break;
            }
            case meta::time_signature: {
                auto const ts = ev.time_signature();
                if (ts.has_value()) {
                    auto const denom = 1 << ts.value()[1];
                    std::print(
                        out,
                        "[{:6}] Time Signature {}/{} clocks={} notated_32nds={}\n",
                        time,
                        ts.value()[0],
                        denom,
                        ts.value()[2],
                        ts.value()[3]);
                } else {
                    std::print(out, "[{:6}] Time Signature (invalid data)\n", time);
                }
                break;
            }
            case meta::key_signature: {
                auto const ks = ev.key_signature();
                if (ks.has_value()) {
                    auto const sf = ks.value()[0];
                    auto const mi = ks.value()[1];
                    std::print(
                        out,
                        "[{:6}] Key Signature {} {}, {}\n",
                        time,
                        sf < 0 ? -sf : sf,
                        sf < 0 ? "flats" : (sf > 0 ? "sharps" : "sharps/flats"),
                        mi ? "minor" : "major");
                } else {
                    std::print(out, "[{:6}] Key Signature (invalid data)\n", time);
                }
                break;
            }
            case meta::end_of_track:
                std::print(out, "[{:6}] End of Track\n", time);
                break;
            case meta::text:
            case meta::copyright:
            case meta::track_name:
            case meta::instrument_name:
            case meta::lyric:
            case meta::marker:
            case meta::cue_point: {
                static constexpr char const* meta_text_names[] = {
                    nullptr,
                    "Text",
                    "Copyright",
                    "Track Name",
                    "Instrument",
                    "Lyric",
                    "Marker",
                    "Cue Point",
                };
                auto const* const name = (ev.type <= 0x07) ? meta_text_names[ev.type] : "Text";
                std::print(out, "[{:6}] {}: \"{}\"\n", time, name, ev.text());
                break;
            }
            case meta::sequence_number:
                std::print(out, "[{:6}] Sequence Number\n", time);
                break;
            case meta::channel_prefix:
                std::print(out, "[{:6}] Channel Prefix {}\n", time, ev.data.empty() ? 0 : ev.data[0]);
                break;
            case meta::smpte_offset:
                std::print(out, "[{:6}] SMPTE Offset\n", time);
                break;
            default:
                std::print(out, "[{:6}] Meta 0x{:02X} ({} bytes)\n", time, ev.type, ev.data.size());
                break;
        }
    };

    cb.on_error = [out](MidiError err) { std::print(out, "Error: {}\n", make_error_code(err).message()); };

    return cb;
}

}  // namespace statusbar::midi
