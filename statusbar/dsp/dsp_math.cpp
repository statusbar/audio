// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/dsp/dsp_math.hpp"

namespace statusbar::dsp {

auto calculate_rms(std::span<double const> signal) noexcept -> double
{
    if (signal.empty()) {
        return 0.0;
    }
    double sum_sq = 0.0;
    for (double const s : signal) {
        sum_sq += s * s;
    }
    return std::sqrt(sum_sq / static_cast<double>(signal.size()));
}

auto calculate_rms(std::span<float const> signal) noexcept -> float
{
    if (signal.empty()) {
        return 0.0F;
    }
    float sum_sq = 0.0F;
    for (float const s : signal) {
        sum_sq += s * s;
    }
    return std::sqrt(sum_sq / static_cast<float>(signal.size()));
}

auto count_zero_crossings(std::span<double const> signal) noexcept -> size_t
{
    if (signal.size() < 2) {
        return 0;
    }
    size_t crossings = 0;
    for (size_t i = 1; i < signal.size(); ++i) {
        if ((signal[i - 1] < 0.0 && signal[i] >= 0.0) || (signal[i - 1] >= 0.0 && signal[i] < 0.0)) {
            ++crossings;
        }
    }
    return crossings;
}

auto count_zero_crossings(std::span<float const> signal) noexcept -> size_t
{
    if (signal.size() < 2) {
        return 0;
    }
    size_t crossings = 0;
    for (size_t i = 1; i < signal.size(); ++i) {
        if ((signal[i - 1] < 0.0F && signal[i] >= 0.0F) || (signal[i - 1] >= 0.0F && signal[i] < 0.0F)) {
            ++crossings;
        }
    }
    return crossings;
}

}  // namespace statusbar::dsp
