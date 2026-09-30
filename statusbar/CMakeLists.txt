# Package INTERFACE library, include path and the global accumulation properties
# (statusbar_package_module_init in cmake/module.cmake).
statusbar_package_module_init()

# Helper: build a libFuzzer harness (Linux) or standalone driver (macOS) for
# audio parsers. Mirrors avb's add_avb_fuzzer / crypto's add_crypto_fuzzer.
# No-op unless ENABLE_FUZZING; the fuzz-smoke / fuzz-all targets are
# auto-discovered by statusbar_register_fuzz_targets() at the root.
function(add_audio_fuzzer source_file)
  if(NOT ENABLE_FUZZING)
    return()
  endif()
  get_filename_component(FUZZ_NAME ${source_file} NAME_WE)
  if(APPLE)
    add_executable(${FUZZ_NAME} ${source_file})
    target_link_libraries(${FUZZ_NAME} PRIVATE statusbar)
    target_compile_options(${FUZZ_NAME} PRIVATE -fsanitize=address,undefined
                                                -fno-omit-frame-pointer)
    target_link_options(${FUZZ_NAME} PRIVATE -fsanitize=address,undefined)
  else()
    add_executable(${FUZZ_NAME} ${source_file})
    target_link_libraries(${FUZZ_NAME} PRIVATE statusbar)
    target_compile_options(
      ${FUZZ_NAME} PRIVATE -fsanitize=fuzzer,address,undefined
                           -fno-omit-frame-pointer)
    target_link_options(
      ${FUZZ_NAME} PRIVATE -fsanitize=fuzzer,address,undefined
      ${FUZZER_EXTRA_LINK_OPTIONS})
  endif()
endfunction()

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

# Standalone per-package test binary (statusbar_package_test_binary in
# cmake/module.cmake); the outer aggregate build builds one combined
# statusbar_test from every aggregated package instead.
statusbar_package_test_binary()
