# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# SPDX-License-Identifier: MIT

statusbar_add_module(
  NAME
  engine
  INTERFACE
  DEPS
  statusbar-dsp
  statusbar::itc
  TESTS
  engine_test.cpp)

add_executable(engine_bench_tool
               ${CMAKE_CURRENT_LIST_DIR}/engine_bench_tool.cpp)
target_link_libraries(engine_bench_tool PRIVATE statusbar)
set_target_properties(engine_bench_tool PROPERTIES OUTPUT_NAME
                                                   statusbar-engine-bench)

# Runnable target for invoking the benchmark from the Makefile.
add_custom_target(
  run-engine-bench
  COMMAND $<TARGET_FILE:engine_bench_tool>
  DEPENDS engine_bench_tool
  USES_TERMINAL VERBATIM)
add_dependencies(run-all-benches run-engine-bench)
