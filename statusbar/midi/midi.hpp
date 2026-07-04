#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// MIDI Module — MIDI message types, stream parsing, and file I/O
///
/// Provides:
/// - MidiMessage: Fixed-size 3-byte MIDI message with factory functions
/// - SysexMessage<N>: Template-parameterized sysex buffer (default 1024 bytes)
/// - MetaEvent: Non-owning view into MIDI meta event data
/// - MidiParser<N>: Byte-by-byte stream parser with lambda callbacks
/// - read_midi_file(): MIDI file reader for Type 0 and Type 1 files
/// - MidiFileWriter: Stateful builder for writing MIDI files
///
/// Usage:
///   #include "statusbar/midi/midi.hpp"
///   using namespace statusbar::midi;
///
///   // Parse a stream
///   MidiParser parser([](MidiMessage const& msg) { /* handle */ });
///   parser.parse(byte);
///
///   // Read a file
///   MidiFileCallbacks cb;
///   cb.on_message = [](MidiTick t, MidiMessage const& msg) { /* handle */ };
///   read_midi_file(file_data, cb);
///
///   // Write a file
///   MidiFileWriter writer(buffer);
///   writer.write_header(0, 1, 480);
///   writer.begin_track();
///   writer.write_message(0, MidiMessage::note_on(0, 60, 100));
///   writer.write_end_of_track(480);
///   writer.end_track();

#include "statusbar/midi/midi_detail.hpp"
#include "statusbar/midi/midi_error.hpp"
#include "statusbar/midi/midi_file_reader.hpp"
#include "statusbar/midi/midi_file_writer.hpp"
#include "statusbar/midi/midi_fileshow.hpp"
#include "statusbar/midi/midi_matrix.hpp"
#include "statusbar/midi/midi_meta.hpp"
#include "statusbar/midi/midi_parser.hpp"
#include "statusbar/midi/midi_processor.hpp"
#include "statusbar/midi/midi_sysex.hpp"
#include "statusbar/midi/midi_types.hpp"
