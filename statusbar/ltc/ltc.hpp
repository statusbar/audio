#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// SMPTE LTC (Linear Timecode) Generator Module
// Generates SMPTE 12M compliant LTC audio signals at 30fps non-drop frame

#include "statusbar/ltc/ltc_frame.hpp"
#include "statusbar/ltc/ltc_generator.hpp"
#include "statusbar/ltc/ltc_playback.hpp"
#include "statusbar/ltc/ltc_servo.hpp"
#include "statusbar/ltc/ltc_timecode.hpp"

namespace statusbar::ltc {

/// Module version
inline constexpr struct
{
    int major = 1;
    int minor = 0;
    int patch = 0;
} version;

}  // namespace statusbar::ltc
