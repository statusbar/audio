include(${PROJECT_SOURCE_DIR}/cmake/module.cmake)

# Package INTERFACE library — every module target reachable in this package's
# build. Module tool executables link `statusbar`. An outer aggregate build
# supplies a single shared `statusbar`.
if(NOT STATUSBAR_AGGREGATE_BUILD)
  add_library(statusbar INTERFACE)
endif()

# Every package adds its own source root to the package INTERFACE library's
# include path, so tool and example sources can #include headers
# ("statusbar/<dir>/...") that live inside this package.
target_include_directories(statusbar
                           INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>)

define_property(
  GLOBAL
  PROPERTY STATUSBAR_TEST_FILES
  BRIEF_DOCS "test sources"
  FULL_DOCS "accumulated test sources")
define_property(
  GLOBAL
  PROPERTY STATUSBAR_INSTALL_TARGETS
  BRIEF_DOCS "install targets"
  FULL_DOCS "accumulated install targets")
# An outer aggregate build initialises these once and lets them accumulate
# across every package; a standalone build resets per package.
if(NOT STATUSBAR_AGGREGATE_BUILD)
  set_property(GLOBAL PROPERTY STATUSBAR_TEST_FILES "")
  set_property(GLOBAL PROPERTY STATUSBAR_INSTALL_TARGETS "")
endif()

# Module subdirectories — dependency order.
add_subdirectory(dsp)
add_subdirectory(audio)
add_subdirectory(engine)
add_subdirectory(midi)
add_subdirectory(osc)
add_subdirectory(ltc)

# Populate the package INTERFACE library — skipped under an outer aggregate
# build, which populates a single shared `statusbar` itself.
if(NOT STATUSBAR_AGGREGATE_BUILD)
  target_link_libraries(
    statusbar
    INTERFACE statusbar-dsp
              statusbar-audio
              statusbar-engine
              statusbar-midi
              statusbar-osc
              statusbar-ltc
              statusbar::args
              statusbar::benchmark
              statusbar::bpf
              statusbar::buffer
              statusbar::colbin
              statusbar::config
              statusbar::container
              statusbar::csv
              statusbar::ieee
              statusbar::ip
              statusbar::itc
              statusbar::net
              statusbar::pcap
              statusbar::realtime
              statusbar::sm
              statusbar::stats
              statusbar::status
              statusbar::test
              statusbar::toml
              statusbar::tsn
              statusbar::tui)
endif()

# Aggregate test binary built from every module's test sources. Skipped under an
# outer aggregate build, which builds one combined statusbar_test from every
# package.
if(NOT STATUSBAR_AGGREGATE_BUILD)
  get_property(STATUSBAR_TEST_FILES_ABS GLOBAL PROPERTY STATUSBAR_TEST_FILES)
  set(STATUSBAR_TEST_FILES "")
  foreach(TEST_FILE_ABS ${STATUSBAR_TEST_FILES_ABS})
    file(RELATIVE_PATH TEST_FILE_REL "${PROJECT_SOURCE_DIR}" "${TEST_FILE_ABS}")
    list(APPEND STATUSBAR_TEST_FILES "${TEST_FILE_REL}")
  endforeach()
  list(SORT STATUSBAR_TEST_FILES)
  if(STATUSBAR_TEST_FILES)
    create_test_sourcelist(STATUSBAR_TEST_SOURCES statusbar_test.cpp
                           ${STATUSBAR_TEST_FILES})
    add_executable(statusbar_test ${CMAKE_CURRENT_BINARY_DIR}/statusbar_test.cpp
                                  ${STATUSBAR_TEST_FILES_ABS})
    target_include_directories(statusbar_test PRIVATE ${CMAKE_CURRENT_LIST_DIR})
    target_link_libraries(statusbar_test PRIVATE statusbar)
    foreach(TEST_FILE ${STATUSBAR_TEST_FILES})
      string(REPLACE ".cpp" "" TEST_NAME "${TEST_FILE}")
      add_test(NAME ${TEST_NAME} COMMAND statusbar_test ${TEST_NAME})
    endforeach()
    add_test(NAME statusbar_all_test COMMAND statusbar_test -A)
    statusbar_register_coverage_targets(statusbar_test)
  endif()
endif()
