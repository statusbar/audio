#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// SMPTE LTC (Linear Timecode) Generator Module
// Generates SMPTE 12M compliant LTC audio signals across all supported frame
// rates (23.976 / 24 / 25 / 29.97 DF+NDF / 30 DF+NDF)

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
