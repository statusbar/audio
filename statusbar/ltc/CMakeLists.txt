# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# SPDX-License-Identifier: MIT

statusbar_add_module(
  NAME
  ltc
  SOURCES
  ltc_generator.cpp
  ltc_playback.cpp
  ltc_servo.cpp
  ltc_timecode.cpp
  DEPS
  statusbar-audio
  statusbar::status
  TESTS
  ltc_test.cpp)

add_executable(ltc_gen_tool ltc_gen_tool.cpp)
target_link_libraries(ltc_gen_tool PRIVATE statusbar)
set_target_properties(ltc_gen_tool PROPERTIES OUTPUT_NAME statusbar-ltc-gen)
set_property(GLOBAL APPEND PROPERTY STATUSBAR_INSTALL_TARGETS ltc_gen_tool)
