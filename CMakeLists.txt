cmake_minimum_required(VERSION 3.31)
project(
  statusbar-audio
  VERSION 2.1.0
  LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
enable_testing()

# Opt-out switch for the ALSA backend. Default ON preserves the historical Linux
# behaviour. With STATUSBAR_ENABLE_ALSA=OFF the audio module builds without
# libasound — `audio_device_linux.{cpp,hpp}` is excluded and any tool that
# consumes it falls back to the headless path. Useful for slim containers /
# embedded targets / cross-platform CI matrices.
option(STATUSBAR_ENABLE_ALSA
       "Enable ALSA backend in audio/ (Linux only; requires libasound2-dev)" ON)

# Modules register bench tools into this aggregate target. An outer aggregate
# build supplies a single shared target instead.
if(NOT STATUSBAR_AGGREGATE_BUILD)
  add_custom_target(run-all-benches)
endif()

# Dependency packages — must be installed and discoverable.
find_package(statusbar-core REQUIRED)

add_subdirectory(statusbar)

# Export each module target under the short statusbar::<name> form, so dependent
# packages find_package() them as statusbar::<module>.
set_target_properties(statusbar-audio PROPERTIES EXPORT_NAME audio)
set_target_properties(statusbar-dsp PROPERTIES EXPORT_NAME dsp)
set_target_properties(statusbar-engine PROPERTIES EXPORT_NAME engine)
set_target_properties(statusbar-ltc PROPERTIES EXPORT_NAME ltc)
set_target_properties(statusbar-midi PROPERTIES EXPORT_NAME midi)
set_target_properties(statusbar-osc PROPERTIES EXPORT_NAME osc)

# Uniform statusbar- prefixed on-disk names for installed executables
# (statusbar_prefix_tool_names in cmake/module.cmake).
statusbar_prefix_tool_names()

# ---- install + export ----
# Standalone install/export boilerplate lives in
# statusbar_package_install_export (cmake/module.cmake); no-op under an outer
# aggregate build.
statusbar_package_install_export(NAME audio)

# ---- packaging (CPack: TGZ + ZIP, plus DEB/RPM on Linux) ----
# Standalone builds only — run `cpack` from the build directory to produce this
# package's archives.
if(NOT STATUSBAR_AGGREGATE_BUILD)
  statusbar_package_cpack_defaults(
    NAME
    audio
    DESCRIPTION
    "Audio toolkit — DSP primitives, the real-time data-flow engine, low-latency cross-platform audio I/O, SMPTE timecode, MIDI, and Open Sound Control."
  )
  # -dev pulls its own runtime so its cmake config's IMPORTED targets resolve.
  # >= (not =) so an auto-incremented Debian revision (1.3.0-<rev>) still
  # satisfies the cross-dependencies. Floors track the versions this build was
  # actually made against (find_package sets statusbar-core_VERSION), never a
  # hardcoded literal that goes stale on the next release.
  set(CPACK_DEBIAN_DEVEL_PACKAGE_DEPENDS
      "statusbar-audio (>= ${PROJECT_VERSION}), statusbar-core-dev (>= ${statusbar-core_VERSION})"
  )
  set(CPACK_RPM_DEVEL_PACKAGE_REQUIRES
      "statusbar-audio >= ${PROJECT_VERSION}, statusbar-core-devel >= ${statusbar-core_VERSION}"
  )
  set(CPACK_COMPONENT_DEVEL_DESCRIPTION
      "C++ headers, static libraries and CMake package config")
  include(CPack)
endif()

# clang-tidy aggregate target (no-op unless clang-tidy is on PATH).
statusbar_register_clang_tidy_target()
