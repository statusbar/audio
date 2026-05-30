#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Audio Module - Main Export
// Cross-platform low-latency audio I/O

#include "statusbar/audio/audio_config.hpp"
#include "statusbar/audio/audio_convert.hpp"
#include "statusbar/audio/audio_device.hpp"
#include "statusbar/audio/audio_error.hpp"
#include "statusbar/audio/audio_mock.hpp"
#include "statusbar/audio/audio_presentation_queue.hpp"
#include "statusbar/audio/audio_provider.hpp"
#include "statusbar/audio/audio_ring.hpp"
#include "statusbar/audio/audio_stream.hpp"
#include "statusbar/audio/audio_timing.hpp"
#include "statusbar/audio/audio_types.hpp"

#ifdef __APPLE__
#    include "statusbar/audio/audio_device_darwin.hpp"
#elif defined(__linux__)
#    include "statusbar/audio/audio_device_linux.hpp"
#else
#    error "Unsupported platform"
#endif

namespace statusbar::audio {

/// Module version
inline constexpr struct
{
    int major = 1;
    int minor = 1;
    int patch = 0;
} version;

}  // namespace statusbar::audio
