#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/dsp/dsp_constants.hpp"
#include "statusbar/dsp/dsp_vec.hpp"

#include <cmath>
#include <cstddef>
#include <span>
#include <type_traits>

namespace statusbar::dsp {

template <typename PlugInT, typename T>
void process_in_place(PlugInT& plugin, std::span<T> samples) noexcept
{
    for (auto& sample : samples) {
        sample = plugin(sample);
    }
}

template <typename PlugInT, typename T, size_t N>
void process_in_place(PlugInT& plugin, std::span<T, N> samples) noexcept
{
    for (auto& sample : samples) {
        sample = plugin(sample);
    }
}

template <typename PlugInT, typename T>
void process_mix_in_place(PlugInT& plugin, std::span<T> samples) noexcept
{
    for (auto& sample : samples) {
        sample += plugin(sample);
    }
}

template <typename PlugInT, typename T, size_t N>
void process_mix_in_place(PlugInT& plugin, std::span<T, N> samples) noexcept
{
    for (auto& sample : samples) {
        sample += plugin(sample);
    }
}

template <typename PlugInT, typename T>
void process(PlugInT& plugin, std::span<T const> input, std::span<T> output) noexcept
{
    size_t const count = (input.size() < output.size()) ? input.size() : output.size();
    for (size_t i = 0; i < count; ++i) {
        output[i] = plugin(input[i]);
    }
}

template <typename PlugInT, typename T, size_t N>
void process(PlugInT& plugin, std::span<T const, N> input, std::span<T, N> output) noexcept
{
    for (size_t i = 0; i < N; ++i) {
        output[i] = plugin(input[i]);
    }
}

template <typename PlugInT, typename T>
void process_accumulate(PlugInT& plugin, std::span<T const> input, std::span<T> output) noexcept
{
    size_t const count = (input.size() < output.size()) ? input.size() : output.size();
    for (size_t i = 0; i < count; ++i) {
        output[i] += plugin(input[i]);
    }
}

template <typename PlugInT, typename T, size_t N>
void process_accumulate(PlugInT& plugin, std::span<T const, N> input, std::span<T, N> output) noexcept
{
    for (size_t i = 0; i < N; ++i) {
        output[i] += plugin(input[i]);
    }
}

}  // namespace statusbar::dsp
