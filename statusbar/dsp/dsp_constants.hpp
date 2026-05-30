#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include <cmath>
#include <numbers>

namespace statusbar::dsp::constants {

template <typename T = double>
[[nodiscard]] constexpr auto zero() noexcept -> T
{
    return T{0.0};
}

template <typename T = double>
[[nodiscard]] constexpr auto pi() noexcept -> T
{
    return T{std::numbers::pi_v<T>};
}

template <typename T = double>
[[nodiscard]] constexpr auto pi_recip() noexcept -> T
{
    return T{std::numbers::inv_pi_v<T>};
}

template <typename T = double>
[[nodiscard]] constexpr auto pi_over_two() noexcept -> T
{
    return T{std::numbers::pi_v<T> / T{2}};
}

template <typename T = double>
[[nodiscard]] constexpr auto one() noexcept -> T
{
    return T{1.0};
}

template <typename T = double>
[[nodiscard]] constexpr auto two() noexcept -> T
{
    return T{2.0};
}

template <typename T = double>
[[nodiscard]] constexpr auto two_pi() noexcept -> T
{
    return T{std::numbers::pi_v<T> * T{2}};
}

template <typename T = double>
[[nodiscard]] constexpr auto two_pi_recip() noexcept -> T
{
    return T{std::numbers::inv_pi_v<T> / T{2}};
}

template <typename T = double>
[[nodiscard]] constexpr auto e() noexcept -> T
{
    return T{std::numbers::e_v<T>};
}

template <typename T = double>
[[nodiscard]] constexpr auto e_recip() noexcept -> T
{
    return T{T{1} / std::numbers::e_v<T>};
}

template <typename T = double>
[[nodiscard]] constexpr auto sqrt_2() noexcept -> T
{
    return T{std::numbers::sqrt2_v<T>};
}

template <typename T = double>
[[nodiscard]] constexpr auto sqrt_2_recip() noexcept -> T
{
    return T{T{1} / std::numbers::sqrt2_v<T>};
}

template <typename T = double>
[[nodiscard]] constexpr auto log2_e() noexcept -> T
{
    return T{std::numbers::log2e_v<T>};
}

template <typename T = double>
[[nodiscard]] constexpr auto log2_ten() noexcept -> T
{
    return T{3.321928094887362347870319429489390175865};
}

template <typename T = double>
[[nodiscard]] constexpr auto ln2() noexcept -> T
{
    return T{std::numbers::ln2_v<T>};
}

template <typename T = double>
[[nodiscard]] constexpr auto ln2_recip() noexcept -> T
{
    return T{std::numbers::log2e_v<T>};
}

template <typename T = double>
[[nodiscard]] constexpr auto log10_2() noexcept -> T
{
    return T{0.301029995663981195213738894724493026768};
}

template <typename T = double>
[[nodiscard]] constexpr auto log10_2_recip() noexcept -> T
{
    return T{3.3219280948873623478703194294893901758648313930245806120547563958};
}

template <typename T = double>
[[nodiscard]] constexpr auto ln10() noexcept -> T
{
    return T{std::numbers::ln10_v<T>};
}

template <typename T = double>
[[nodiscard]] constexpr auto ln10_recip() noexcept -> T
{
    return T{std::numbers::log10e_v<T>};
}

template <typename T = double>
[[nodiscard]] constexpr auto ten() noexcept -> T
{
    return T{10.0};
}

template <typename T = double>
[[nodiscard]] constexpr auto twenty_recip() noexcept -> T
{
    return T{0.05};
}

template <typename T = double>
[[nodiscard]] constexpr auto twenty_div_ln10() noexcept -> T
{
    return T{8.6858896380650365530225783783321016458879401160733313222890756633};
}

template <typename T = double>
[[nodiscard]] constexpr auto two_gigi() noexcept -> T
{
    return T{2147483648.0};
}

template <typename T = double>
[[nodiscard]] constexpr auto two_gigi_recip() noexcept -> T
{
    return T{4.656612873077392578125e-10};
}

template <typename T = double>
[[nodiscard]] constexpr auto recip_48k() noexcept -> T
{
    return T{0.0000208333333333333333333333333333333333333333333333333333333333};
}

template <typename T = double>
[[nodiscard]] constexpr auto recip_96k() noexcept -> T
{
    return T{0.0000104166666666666666666666666666666666666666666666666666666666};
}

template <typename T = double>
[[nodiscard]] constexpr auto recip_192k() noexcept -> T
{
    return T{5.20833333333333333333333333333333333333333333333333333333333e-6};
}

}  // namespace statusbar::dsp::constants
