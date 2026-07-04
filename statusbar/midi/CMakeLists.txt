# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# SPDX-License-Identifier: MIT

statusbar_add_module(
  NAME
  midi
  SOURCES
  midi_error.cpp
  midi_file_reader.cpp
  midi_file_writer.cpp
  midi_fileshow.cpp
  midi_processor.cpp
  DEPS
  statusbar::buffer
  statusbar::status
  TESTS
  midi_test.cpp)

add_executable(midi_show_tool midi_show_tool.cpp)
target_link_libraries(midi_show_tool PRIVATE statusbar)
set_target_properties(midi_show_tool PROPERTIES OUTPUT_NAME statusbar-midi-show)
set_property(GLOBAL APPEND PROPERTY STATUSBAR_INSTALL_TARGETS midi_show_tool)

add_executable(midi_copy_tool midi_copy_tool.cpp)
target_link_libraries(midi_copy_tool PRIVATE statusbar)
set_target_properties(midi_copy_tool PROPERTIES OUTPUT_NAME statusbar-midi-copy)
set_property(GLOBAL APPEND PROPERTY STATUSBAR_INSTALL_TARGETS midi_copy_tool)

# Fuzz harnesses for the untrusted-byte parsers (no-op unless ENABLE_FUZZING).
add_audio_fuzzer(${CMAKE_CURRENT_LIST_DIR}/midi_file_reader_fuzzer.cpp)
add_audio_fuzzer(${CMAKE_CURRENT_LIST_DIR}/midi_parser_fuzzer.cpp)
