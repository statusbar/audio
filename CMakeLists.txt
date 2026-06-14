cmake_minimum_required(VERSION 3.31)
project(
  statusbar-audio
  VERSION 1.1.0
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

# Give every installed executable a statusbar- prefixed on-disk name, so the
# bin/ directory is uniform (tools that set no OUTPUT_NAME, and examples, would
# otherwise install unprefixed).
get_property(_exe_targets GLOBAL PROPERTY STATUSBAR_INSTALL_TARGETS)
foreach(_t ${_exe_targets})
  if(NOT TARGET ${_t})
    continue()
  endif()
  get_target_property(_t_type ${_t} TYPE)
  if(NOT _t_type STREQUAL "EXECUTABLE")
    continue()
  endif()
  get_target_property(_t_name ${_t} OUTPUT_NAME)
  if(NOT _t_name)
    set(_t_name "${_t}")
  endif()
  if(NOT _t_name MATCHES "^statusbar-")
    string(REPLACE "_" "-" _t_name "${_t_name}")
    set_target_properties(${_t} PROPERTIES OUTPUT_NAME "statusbar-${_t_name}")
  endif()
endforeach()

# ---- install + export ----
# Skipped under an outer aggregate build, which builds, not installs. Install
# rules are split into runtime (tools / shared libs) and devel (static libs,
# headers, CMake config) components for CPack.
if(NOT STATUSBAR_AGGREGATE_BUILD)
  get_property(_install_targets GLOBAL PROPERTY STATUSBAR_INSTALL_TARGETS)
  install(
    TARGETS ${_install_targets}
    EXPORT statusbar-audioTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT devel
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
  install(
    EXPORT statusbar-audioTargets
    FILE statusbar-audioTargets.cmake
    NAMESPACE statusbar::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/statusbar-audio
    COMPONENT devel)
  # Also export the build tree, so a sibling package can find this one without
  # an install step: pass -Dstatusbar-audio_DIR=<build-dir>.
  export(
    EXPORT statusbar-audioTargets
    FILE statusbar-audioTargets.cmake
    NAMESPACE statusbar::)
  install(
    DIRECTORY statusbar/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/statusbar
    COMPONENT devel
    FILES_MATCHING
    PATTERN "*.hpp"
    PATTERN "*.h")
  configure_package_config_file(
    cmake/statusbar-audioConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/statusbar-audioConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/statusbar-audio)
  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/statusbar-audioConfigVersion.cmake
    VERSION 1.1.0
    COMPATIBILITY AnyNewerVersion)
  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/statusbar-audioConfig.cmake
          ${CMAKE_CURRENT_BINARY_DIR}/statusbar-audioConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/statusbar-audio
    COMPONENT devel)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/docs)
    install(
      DIRECTORY docs/
      DESTINATION ${CMAKE_INSTALL_DOCDIR}
      COMPONENT devel
      FILES_MATCHING
      PATTERN "*.md"
      PATTERN "*.svg"
      PATTERN "*.png")
  endif()
endif()

# ---- packaging (CPack: TGZ + ZIP, plus DEB/RPM on Linux) ----
# Standalone builds only — run `cpack` from the build directory to produce this
# package's archives.
if(NOT STATUSBAR_AGGREGATE_BUILD)
  set(CPACK_PACKAGE_NAME "statusbar-audio")
  set(CPACK_PACKAGE_VENDOR "Jeff Koftinoff")
  set(CPACK_PACKAGE_VERSION "1.1.0")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
      "Audio toolkit — DSP primitives, the real-time data-flow engine, low-latency cross-platform audio I/O, SMPTE timecode, MIDI, and Open Sound Control."
  )
  set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER
      "Jeff Koftinoff <jeff.koftinoff@statusbar.com>")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_RPM_PACKAGE_LICENSE "MIT")
  set(CPACK_RPM_PACKAGE_GROUP "Development/Libraries")
  set(CPACK_RPM_PACKAGE_AUTOREQ ON)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  endif()
  set(CPACK_DEB_COMPONENT_INSTALL ON)
  set(CPACK_RPM_COMPONENT_INSTALL ON)
  set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
  # Name DEB/RPM files the package-manager-conventional way
  # (<name>_<version>_<arch>.deb), so dependent builds and the orchestrator
  # locate them by package name.
  set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
  set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
  set(CPACK_DEBIAN_RUNTIME_PACKAGE_NAME "statusbar-audio")
  set(CPACK_RPM_RUNTIME_PACKAGE_NAME "statusbar-audio")
  set(CPACK_COMPONENT_RUNTIME_DESCRIPTION "Command-line tools and executables")
  set(CPACK_COMPONENTS_ALL runtime devel)
  set(CPACK_DEBIAN_DEVEL_PACKAGE_NAME "statusbar-audio-dev")
  set(CPACK_RPM_DEVEL_PACKAGE_NAME "statusbar-audio-devel")
  # -dev pulls its own runtime so its cmake config's IMPORTED targets resolve.
  # >= (not =) so an auto-incremented Debian revision (1.1.0-<rev>) still
  # satisfies the cross-dependencies.
  set(CPACK_DEBIAN_DEVEL_PACKAGE_DEPENDS
      "statusbar-audio (>= 1.1.0), statusbar-core-dev (>= 1.1.0)")
  set(CPACK_RPM_DEVEL_PACKAGE_REQUIRES
      "statusbar-audio >= 1.1.0, statusbar-core-devel >= 1.1.0")
  set(CPACK_COMPONENT_DEVEL_DESCRIPTION
      "C++ headers, static libraries and CMake package config")
  set(CPACK_GENERATOR "TGZ;ZIP")
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND CPACK_GENERATOR "DEB")
    find_program(_rpmbuild rpmbuild)
    if(_rpmbuild)
      list(APPEND CPACK_GENERATOR "RPM")
    endif()
  endif()
  include(CPack)
endif()

# clang-tidy aggregate target (no-op unless clang-tidy is on PATH).
statusbar_register_clang_tidy_target()
