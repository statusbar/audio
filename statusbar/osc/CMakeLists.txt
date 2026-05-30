# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# SPDX-License-Identifier: MIT

statusbar_add_module(
  NAME
  osc
  SOURCES
  osc_argument.cpp
  osc_bundle.cpp
  osc_deserializer.cpp
  osc_error.cpp
  osc_message.cpp
  osc_serializer.cpp
  DEPS
  statusbar::buffer
  statusbar::ieee
  statusbar::status
  TESTS
  osc_test.cpp)
