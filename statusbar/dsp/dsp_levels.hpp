#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

//
// Audio level and loudness conversions.
//
// Home for amplitude/dB conversions and (in the future) loudness metrics
// like peak, true-peak (oversampled), and LUFS / K-weighted measurements
// per ITU-R BS.1770 / EBU R128.
//
// Currently:
//   db_to_amplitude(db)        — linear amplitude from dBFS / dB-relative
//   amplitude_to_db(amplitude) — dB from linear amplitude
//
// Planned (each warrants its own filter/state object, not a free function):
//   peak / true_peak meters with hold and decay
//   K-weighting biquad pair (per BS.1770) for LUFS pre-filter
//   integrated / momentary / short-term LUFS with gating
//

#include <cmath>

namespace statusbar::dsp {

// dBFS / linear-amplitude conversions for voltage/sample magnitudes.
// dB = 20 · log10(amplitude). 0 dB ↔ amplitude 1.0; -6.02 dB ↔ 0.5; etc.
//
// Power conversions (where dB = 10 · log10(power)) are intentionally not
// provided here — audio gain/level work uses the voltage form almost
// exclusively. If a power form becomes needed, give it a distinct name
// (db_to_power / power_to_db) so caller intent is unambiguous.

[[nodiscard]] inline auto db_to_amplitude(double db) noexcept -> double
{
    return std::pow(10.0, db / 20.0);
}

[[nodiscard]] inline auto db_to_amplitude(float db) noexcept -> float
{
    return std::pow(10.0F, db / 20.0F);
}

[[nodiscard]] inline auto amplitude_to_db(double amplitude) noexcept -> double
{
    return 20.0 * std::log10(amplitude);
}

[[nodiscard]] inline auto amplitude_to_db(float amplitude) noexcept -> float
{
    return 20.0F * std::log10(amplitude);
}

}  // namespace statusbar::dsp
