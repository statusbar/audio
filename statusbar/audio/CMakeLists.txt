# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# SPDX-License-Identifier: MIT

set(_audio_sources
    audio_config.cpp
    audio_convert.cpp
    audio_error.cpp
    audio_mock.cpp
    audio_presentation_queue.cpp
    audio_provider.cpp
    audio_bw64_writer.cpp
    audio_ring.cpp)

if(APPLE)
  list(APPEND _audio_sources audio_impl_darwin.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND STATUSBAR_ENABLE_ALSA)
  list(APPEND _audio_sources audio_impl_linux.cpp)
endif()

statusbar_add_module(
  NAME
  audio
  SOURCES
  ${_audio_sources}
  DEPS
  statusbar-dsp
  statusbar::status
  statusbar::itc
  TESTS
  audio_config_test.cpp
  audio_convert_test.cpp
  audio_mock_test.cpp
  audio_presentation_queue_test.cpp
  audio_test.cpp
  audio_bw64_writer_test.cpp
  audio_timing_test.cpp)

# Platform-specific audio libraries
if(APPLE)
  target_link_libraries(
    statusbar-audio PUBLIC "-framework AudioUnit" "-framework AudioToolbox"
                           "-framework CoreAudio" "-framework CoreFoundation")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND STATUSBAR_ENABLE_ALSA)
  find_package(ALSA REQUIRED)
  target_link_libraries(statusbar-audio PUBLIC ALSA::ALSA)
endif()

add_executable(audio_input_example audio_input_example.cpp)
target_link_libraries(audio_input_example PRIVATE statusbar)
set_target_properties(audio_input_example PROPERTIES OUTPUT_NAME
                                                     statusbar-audio-input)

add_executable(audio_tool audio_tool.cpp)
target_link_libraries(audio_tool PRIVATE statusbar)
set_target_properties(audio_tool PROPERTIES OUTPUT_NAME statusbar-audio-tool)
set_property(GLOBAL APPEND PROPERTY STATUSBAR_INSTALL_TARGETS audio_tool)

add_executable(audio_tone_tool audio_tone_tool.cpp)
target_link_libraries(audio_tone_tool PRIVATE statusbar)
set_target_properties(audio_tone_tool PROPERTIES OUTPUT_NAME
                                                 statusbar-audio-tone-tool)

add_executable(thread_priority_tool thread_priority_tool.cpp)
target_link_libraries(thread_priority_tool PRIVATE statusbar)
set_target_properties(thread_priority_tool
                      PROPERTIES OUTPUT_NAME statusbar-thread-priority-tool)
