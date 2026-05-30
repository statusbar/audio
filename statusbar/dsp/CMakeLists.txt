# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# SPDX-License-Identifier: MIT

statusbar_add_module(
  NAME
  dsp
  SOURCES
  dsp_biquad_design.cpp
  dsp_math.cpp
  TESTS
  dsp_biquad_test.cpp
  dsp_complex_biquad_test.cpp
  dsp_gain_test.cpp
  dsp_levels_test.cpp
  dsp_oscillator_test.cpp
  dsp_process_test.cpp
  dsp_vec_test.cpp)

add_executable(dsp_bench_tool ${CMAKE_CURRENT_LIST_DIR}/dsp_bench_tool.cpp)
target_link_libraries(dsp_bench_tool PRIVATE statusbar)
set_target_properties(dsp_bench_tool PROPERTIES OUTPUT_NAME statusbar-dsp-bench)

# Runnable target for the top-level `make bench-cpp` — lets the Makefile invoke
# the benchmark without hardcoding CMake's output layout.
add_custom_target(
  run-dsp-bench
  COMMAND $<TARGET_FILE:dsp_bench_tool>
  DEPENDS dsp_bench_tool
  USES_TERMINAL VERBATIM)
add_dependencies(run-all-benches run-dsp-bench)
