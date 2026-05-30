// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Unit tests for statusbar.dsp:vec module

#include "statusbar/dsp/dsp.hpp"
#include "statusbar/test/test.hpp"

#include <cmath>
#include <limits>
#include <print>

using namespace statusbar::dsp;

// Compile-time verification of constexpr constants
// dsp_constants.cppm constexpr functions
static_assert(constants::zero<double>() == 0.0);
static_assert(constants::one<double>() == 1.0);
static_assert(constants::two<double>() == 2.0);
static_assert(constants::ten<double>() == 10.0);
static_assert(constants::twenty_recip<double>() == 0.05);

// Verify pi and its related constants
static_assert(constants::pi<double>() > 3.14 && constants::pi<double>() < 3.15);
static_assert(constants::pi_over_two<double>() > 1.57 && constants::pi_over_two<double>() < 1.58);
static_assert(constants::two_pi<double>() > 6.28 && constants::two_pi<double>() < 6.29);

// Verify e and its related constants
static_assert(constants::e<double>() > 2.71 && constants::e<double>() < 2.72);

// Verify sqrt_2
static_assert(constants::sqrt_2<double>() > 1.41 && constants::sqrt_2<double>() < 1.42);
static_assert(constants::sqrt_2_recip<double>() > 0.70 && constants::sqrt_2_recip<double>() < 0.71);

// Verify reciprocal relationships at compile time
static_assert(constants::pi<double>() * constants::pi_recip<double>() > 0.999);
static_assert(constants::pi<double>() * constants::pi_recip<double>() < 1.001);
static_assert(constants::e<double>() * constants::e_recip<double>() > 0.999);
static_assert(constants::e<double>() * constants::e_recip<double>() < 1.001);
static_assert(constants::two_pi<double>() * constants::two_pi_recip<double>() > 0.999);
static_assert(constants::two_pi<double>() * constants::two_pi_recip<double>() < 1.001);

// Verify sample rate reciprocals
static_assert(constants::recip_48k<double>() > 2.08e-5 && constants::recip_48k<double>() < 2.09e-5);
static_assert(constants::recip_96k<double>() > 1.04e-5 && constants::recip_96k<double>() < 1.05e-5);

// Verify float versions work the same
static_assert(constants::zero<float>() == 0.0f);
static_assert(constants::one<float>() == 1.0f);
static_assert(constants::pi<float>() > 3.14f && constants::pi<float>() < 3.15f);

// Note: approx_equal uses std::abs which is not constexpr in all compilers,
// so we verify it at runtime instead of via static_assert

// SIMDVec constexpr static members and methods
static_assert(sizeof(simd_float32x4) == 16);
static_assert(sizeof(simd_float32x8) == 32);
static_assert(sizeof(simd_float64x2) == 16);
static_assert(sizeof(simd_float64x4) == 32);
static_assert(!simd_float32x4::empty());
static_assert(!simd_float64x2::empty());

// Construction Tests

TEST(vec_construction, default_construction)
{
    simd_float32x4 v;
    // Default construction leaves values uninitialized, so just verify it compiles
    (void)v;
}

TEST(vec_construction, initializer_list)
{
    simd_float32x4 v{1.0f, 2.0f, 3.0f, 4.0f};
    EXPECT_EQ(v[0], 1.0f);
    EXPECT_EQ(v[1], 2.0f);
    EXPECT_EQ(v[2], 3.0f);
    EXPECT_EQ(v[3], 4.0f);
}

TEST(vec_construction, initializer_list_partial)
{
    simd_float32x4 v{1.0f, 2.0f};
    EXPECT_EQ(v[0], 1.0f);
    EXPECT_EQ(v[1], 2.0f);
    EXPECT_EQ(v[2], 0.0f);  // Zero-filled
    EXPECT_EQ(v[3], 0.0f);
}

TEST(vec_construction, broadcast)
{
    simd_float32x4 v(3.14f);
    EXPECT_EQ(v[0], 3.14f);
    EXPECT_EQ(v[1], 3.14f);
    EXPECT_EQ(v[2], 3.14f);
    EXPECT_EQ(v[3], 3.14f);
}

TEST(vec_construction, copy)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    simd_float32x4 b = a;
    EXPECT_EQ(b[0], 1.0f);
    EXPECT_EQ(b[1], 2.0f);
    EXPECT_EQ(b[2], 3.0f);
    EXPECT_EQ(b[3], 4.0f);
}

TEST(vec_construction, copy_assignment)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    simd_float32x4 b{0.0f, 0.0f, 0.0f, 0.0f};
    b = a;
    EXPECT_EQ(b[0], 1.0f);
    EXPECT_EQ(b[1], 2.0f);
    EXPECT_EQ(b[2], 3.0f);
    EXPECT_EQ(b[3], 4.0f);
}

// Static Factory Tests

TEST(vec_factory, splat)
{
    auto v = simd_float32x4::splat(42.0f);
    EXPECT_EQ(v[0], 42.0f);
    EXPECT_EQ(v[1], 42.0f);
    EXPECT_EQ(v[2], 42.0f);
    EXPECT_EQ(v[3], 42.0f);
}

TEST(vec_factory, zero)
{
    auto v = simd_float32x4::zero();
    EXPECT_EQ(v[0], 0.0f);
    EXPECT_EQ(v[1], 0.0f);
    EXPECT_EQ(v[2], 0.0f);
    EXPECT_EQ(v[3], 0.0f);
}

TEST(vec_factory, one)
{
    auto v = simd_float32x4::one();
    EXPECT_EQ(v[0], 1.0f);
    EXPECT_EQ(v[1], 1.0f);
    EXPECT_EQ(v[2], 1.0f);
    EXPECT_EQ(v[3], 1.0f);
}

// Container Interface Tests

TEST(vec_container, size)
{
    EXPECT_EQ(simd_float32x4::size(), 4U);
    EXPECT_EQ(simd_float32x8::size(), 8U);
    EXPECT_EQ(simd_float64x2::size(), 2U);
    EXPECT_EQ(simd_float64x4::size(), 4U);
}

TEST(vec_container, empty)
{
    EXPECT_FALSE(simd_float32x4::empty());
}

TEST(vec_container, fill)
{
    simd_float32x4 v;
    v.fill(5.0f);
    EXPECT_EQ(v[0], 5.0f);
    EXPECT_EQ(v[1], 5.0f);
    EXPECT_EQ(v[2], 5.0f);
    EXPECT_EQ(v[3], 5.0f);
}

TEST(vec_container, swap)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    simd_float32x4 b{5.0f, 6.0f, 7.0f, 8.0f};
    a.swap(b);
    EXPECT_EQ(a[0], 5.0f);
    EXPECT_EQ(b[0], 1.0f);
}

TEST(vec_container, data)
{
    simd_float32x4 v{1.0f, 2.0f, 3.0f, 4.0f};
    float* ptr = v.data();
    EXPECT_EQ(ptr[0], 1.0f);
    EXPECT_EQ(ptr[3], 4.0f);
}

TEST(vec_container, front_back)
{
    simd_float32x4 v{1.0f, 2.0f, 3.0f, 4.0f};
    EXPECT_EQ(v.front(), 1.0f);
    EXPECT_EQ(v.back(), 4.0f);
}

TEST(vec_container, at_valid)
{
    simd_float32x4 v{1.0f, 2.0f, 3.0f, 4.0f};
    EXPECT_EQ(v.at(0), 1.0f);
    EXPECT_EQ(v.at(3), 4.0f);
}

TEST(vec_container, at_throws)
{
    simd_float32x4 v{1.0f, 2.0f, 3.0f, 4.0f};
    bool threw = false;
    try {
        (void)v.at(4);  // Out of bounds
    } catch (std::out_of_range const&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

TEST(vec_container, iterators)
{
    simd_float32x4 v{1.0f, 2.0f, 3.0f, 4.0f};
    float sum = 0.0f;
    for (auto x : v) {
        sum += x;
    }
    EXPECT_EQ(sum, 10.0f);
}

// Unary Operators

TEST(vec_unary, minus)
{
    simd_float32x4 a{1.0f, -2.0f, 3.0f, -4.0f};
    auto b = -a;
    EXPECT_EQ(b[0], -1.0f);
    EXPECT_EQ(b[1], 2.0f);
    EXPECT_EQ(b[2], -3.0f);
    EXPECT_EQ(b[3], 4.0f);
}

TEST(vec_unary, plus)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    auto b = +a;
    EXPECT_EQ(b[0], 1.0f);
    EXPECT_EQ(b[3], 4.0f);
}

// Vector-Scalar Arithmetic

TEST(vec_scalar_arith, add)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    auto b = a + 10.0f;
    EXPECT_EQ(b[0], 11.0f);
    EXPECT_EQ(b[3], 14.0f);
}

TEST(vec_scalar_arith, sub)
{
    simd_float32x4 a{10.0f, 20.0f, 30.0f, 40.0f};
    auto b = a - 5.0f;
    EXPECT_EQ(b[0], 5.0f);
    EXPECT_EQ(b[3], 35.0f);
}

TEST(vec_scalar_arith, mul)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    auto b = a * 2.0f;
    EXPECT_EQ(b[0], 2.0f);
    EXPECT_EQ(b[3], 8.0f);
}

TEST(vec_scalar_arith, div)
{
    simd_float32x4 a{2.0f, 4.0f, 6.0f, 8.0f};
    auto b = a / 2.0f;
    EXPECT_EQ(b[0], 1.0f);
    EXPECT_EQ(b[3], 4.0f);
}

TEST(vec_scalar_arith, left_add)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    auto b = 10.0f + a;
    EXPECT_EQ(b[0], 11.0f);
    EXPECT_EQ(b[3], 14.0f);
}

TEST(vec_scalar_arith, left_mul)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    auto b = 2.0f * a;
    EXPECT_EQ(b[0], 2.0f);
    EXPECT_EQ(b[3], 8.0f);
}

// Vector-Scalar Compound Assignment

TEST(vec_scalar_assign, add)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    a += 10.0f;
    EXPECT_EQ(a[0], 11.0f);
    EXPECT_EQ(a[3], 14.0f);
}

TEST(vec_scalar_assign, sub)
{
    simd_float32x4 a{10.0f, 20.0f, 30.0f, 40.0f};
    a -= 5.0f;
    EXPECT_EQ(a[0], 5.0f);
    EXPECT_EQ(a[3], 35.0f);
}

TEST(vec_scalar_assign, mul)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    a *= 3.0f;
    EXPECT_EQ(a[0], 3.0f);
    EXPECT_EQ(a[3], 12.0f);
}

TEST(vec_scalar_assign, div)
{
    simd_float32x4 a{10.0f, 20.0f, 30.0f, 40.0f};
    a /= 10.0f;
    EXPECT_EQ(a[0], 1.0f);
    EXPECT_EQ(a[3], 4.0f);
}

// Vector-Vector Arithmetic

TEST(vec_vector_arith, add)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    simd_float32x4 b{10.0f, 20.0f, 30.0f, 40.0f};
    auto c = a + b;
    EXPECT_EQ(c[0], 11.0f);
    EXPECT_EQ(c[3], 44.0f);
}

TEST(vec_vector_arith, sub)
{
    simd_float32x4 a{10.0f, 20.0f, 30.0f, 40.0f};
    simd_float32x4 b{1.0f, 2.0f, 3.0f, 4.0f};
    auto c = a - b;
    EXPECT_EQ(c[0], 9.0f);
    EXPECT_EQ(c[3], 36.0f);
}

TEST(vec_vector_arith, mul)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    simd_float32x4 b{2.0f, 3.0f, 4.0f, 5.0f};
    auto c = a * b;
    EXPECT_EQ(c[0], 2.0f);
    EXPECT_EQ(c[1], 6.0f);
    EXPECT_EQ(c[2], 12.0f);
    EXPECT_EQ(c[3], 20.0f);
}

TEST(vec_vector_arith, div)
{
    simd_float32x4 a{10.0f, 20.0f, 30.0f, 40.0f};
    simd_float32x4 b{2.0f, 4.0f, 5.0f, 8.0f};
    auto c = a / b;
    EXPECT_EQ(c[0], 5.0f);
    EXPECT_EQ(c[1], 5.0f);
    EXPECT_EQ(c[2], 6.0f);
    EXPECT_EQ(c[3], 5.0f);
}

// Vector-Vector Compound Assignment

TEST(vec_vector_assign, add)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    simd_float32x4 b{10.0f, 20.0f, 30.0f, 40.0f};
    a += b;
    EXPECT_EQ(a[0], 11.0f);
    EXPECT_EQ(a[3], 44.0f);
}

TEST(vec_vector_assign, sub)
{
    simd_float32x4 a{10.0f, 20.0f, 30.0f, 40.0f};
    simd_float32x4 b{1.0f, 2.0f, 3.0f, 4.0f};
    a -= b;
    EXPECT_EQ(a[0], 9.0f);
    EXPECT_EQ(a[3], 36.0f);
}

TEST(vec_vector_assign, mul)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    simd_float32x4 b{2.0f, 3.0f, 4.0f, 5.0f};
    a *= b;
    EXPECT_EQ(a[0], 2.0f);
    EXPECT_EQ(a[3], 20.0f);
}

TEST(vec_vector_assign, div)
{
    simd_float32x4 a{10.0f, 20.0f, 30.0f, 40.0f};
    simd_float32x4 b{2.0f, 4.0f, 5.0f, 8.0f};
    a /= b;
    EXPECT_EQ(a[0], 5.0f);
    EXPECT_EQ(a[3], 5.0f);
}

// Math Functions

TEST(vec_math, sqrt)
{
    simd_float32x4 a{1.0f, 4.0f, 9.0f, 16.0f};
    auto b = sqrt(a);
    EXPECT_EQ(b[0], 1.0f);
    EXPECT_EQ(b[1], 2.0f);
    EXPECT_EQ(b[2], 3.0f);
    EXPECT_EQ(b[3], 4.0f);
}

TEST(vec_math, abs)
{
    simd_float32x4 a{-1.0f, 2.0f, -3.0f, 4.0f};
    auto b = abs(a);
    EXPECT_EQ(b[0], 1.0f);
    EXPECT_EQ(b[1], 2.0f);
    EXPECT_EQ(b[2], 3.0f);
    EXPECT_EQ(b[3], 4.0f);
}

TEST(vec_math, sin)
{
    simd_float32x4 a{0.0f, 0.0f, 0.0f, 0.0f};
    auto b = sin(a);
    EXPECT_EQ(b[0], 0.0f);
    EXPECT_EQ(b[1], 0.0f);
}

TEST(vec_math, cos)
{
    simd_float32x4 a{0.0f, 0.0f, 0.0f, 0.0f};
    auto b = cos(a);
    EXPECT_EQ(b[0], 1.0f);
    EXPECT_EQ(b[1], 1.0f);
}

TEST(vec_math, exp)
{
    simd_float32x4 a{0.0f, 0.0f, 0.0f, 0.0f};
    auto b = exp(a);
    EXPECT_EQ(b[0], 1.0f);
    EXPECT_EQ(b[1], 1.0f);
}

TEST(vec_math, log)
{
    simd_float32x4 a{1.0f, 1.0f, 1.0f, 1.0f};
    auto b = log(a);
    EXPECT_EQ(b[0], 0.0f);
    EXPECT_EQ(b[1], 0.0f);
}

TEST(vec_math, reciprocal)
{
    simd_float32x4 a{1.0f, 2.0f, 4.0f, 5.0f};
    auto b = reciprocal(a);
    EXPECT_EQ(b[0], 1.0f);
    EXPECT_EQ(b[1], 0.5f);
    EXPECT_EQ(b[2], 0.25f);
    EXPECT_EQ(b[3], 0.2f);
}

// Reduction Operations

TEST(vec_reduction, hsum)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    EXPECT_EQ(a.hsum(), 10.0f);
}

TEST(vec_reduction, hprod)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    EXPECT_EQ(a.hprod(), 24.0f);
}

TEST(vec_reduction, hmin)
{
    simd_float32x4 a{3.0f, 1.0f, 4.0f, 2.0f};
    EXPECT_EQ(a.hmin(), 1.0f);
}

TEST(vec_reduction, hmax)
{
    simd_float32x4 a{3.0f, 1.0f, 4.0f, 2.0f};
    EXPECT_EQ(a.hmax(), 4.0f);
}

// Min/Max/Clamp

TEST(vec_minmax, min)
{
    simd_float32x4 a{1.0f, 5.0f, 3.0f, 8.0f};
    simd_float32x4 b{2.0f, 3.0f, 4.0f, 6.0f};
    auto c = min(a, b);
    EXPECT_EQ(c[0], 1.0f);
    EXPECT_EQ(c[1], 3.0f);
    EXPECT_EQ(c[2], 3.0f);
    EXPECT_EQ(c[3], 6.0f);
}

TEST(vec_minmax, max)
{
    simd_float32x4 a{1.0f, 5.0f, 3.0f, 8.0f};
    simd_float32x4 b{2.0f, 3.0f, 4.0f, 6.0f};
    auto c = max(a, b);
    EXPECT_EQ(c[0], 2.0f);
    EXPECT_EQ(c[1], 5.0f);
    EXPECT_EQ(c[2], 4.0f);
    EXPECT_EQ(c[3], 8.0f);
}

TEST(vec_minmax, clamp)
{
    simd_float32x4 v{-1.0f, 0.5f, 1.5f, 2.0f};
    simd_float32x4 lo{0.0f, 0.0f, 0.0f, 0.0f};
    simd_float32x4 hi{1.0f, 1.0f, 1.0f, 1.0f};
    auto c = clamp(v, lo, hi);
    EXPECT_EQ(c[0], 0.0f);  // Clamped to min
    EXPECT_EQ(c[1], 0.5f);  // Unchanged
    EXPECT_EQ(c[2], 1.0f);  // Clamped to max
    EXPECT_EQ(c[3], 1.0f);  // Clamped to max
}

// FMA and Lerp

TEST(vec_fma_lerp, madd)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    simd_float32x4 b{2.0f, 3.0f, 4.0f, 5.0f};
    simd_float32x4 c{10.0f, 20.0f, 30.0f, 40.0f};
    auto r = madd(a, b, c);
    // a * b + c
    EXPECT_EQ(r[0], 12.0f);  // 1*2 + 10
    EXPECT_EQ(r[1], 26.0f);  // 2*3 + 20
    EXPECT_EQ(r[2], 42.0f);  // 3*4 + 30
    EXPECT_EQ(r[3], 60.0f);  // 4*5 + 40
}

TEST(vec_fma_lerp, lerp)
{
    simd_float32x4 a{0.0f, 0.0f, 0.0f, 0.0f};
    simd_float32x4 b{10.0f, 20.0f, 30.0f, 40.0f};
    simd_float32x4 t{0.0f, 0.5f, 1.0f, 0.25f};
    auto r = lerp(a, b, t);
    EXPECT_EQ(r[0], 0.0f);   // t=0 -> a
    EXPECT_EQ(r[1], 10.0f);  // t=0.5 -> midpoint
    EXPECT_EQ(r[2], 30.0f);  // t=1 -> b
    EXPECT_EQ(r[3], 10.0f);  // t=0.25 -> 25% of the way
}

TEST(vec_fma_lerp, nmsub)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    simd_float32x4 b{2.0f, 2.0f, 2.0f, 2.0f};
    simd_float32x4 c{10.0f, 10.0f, 10.0f, 10.0f};
    auto r = nmsub(a, b, c);
    // c - a * b
    EXPECT_EQ(r[0], 8.0f);  // 10 - 1*2
    EXPECT_EQ(r[1], 6.0f);  // 10 - 2*2
    EXPECT_EQ(r[2], 4.0f);  // 10 - 3*2
    EXPECT_EQ(r[3], 2.0f);  // 10 - 4*2
}

// Double Precisionsimd_float64x2 Tests =====

TEST(float64x2, default_construction)
{
    simd_float64x2 v;
    (void)v;  // Default construction compiles
}

TEST(float64x2, initializer_list)
{
    simd_float64x2 v{1.0, 2.0};
    EXPECT_EQ(v[0], 1.0);
    EXPECT_EQ(v[1], 2.0);
}

TEST(float64x2, broadcast)
{
    simd_float64x2 v(3.14);
    EXPECT_EQ(v[0], 3.14);
    EXPECT_EQ(v[1], 3.14);
}

TEST(float64x2, factory_zero)
{
    auto v = simd_float64x2::zero();
    EXPECT_EQ(v[0], 0.0);
    EXPECT_EQ(v[1], 0.0);
}

TEST(float64x2, factory_one)
{
    auto v = simd_float64x2::one();
    EXPECT_EQ(v[0], 1.0);
    EXPECT_EQ(v[1], 1.0);
}

TEST(float64x2, factory_splat)
{
    auto v = simd_float64x2::splat(42.0);
    EXPECT_EQ(v[0], 42.0);
    EXPECT_EQ(v[1], 42.0);
}

TEST(float64x2, vector_add)
{
    simd_float64x2 a{1.0, 2.0};
    simd_float64x2 b{3.0, 4.0};
    auto c = a + b;
    EXPECT_EQ(c[0], 4.0);
    EXPECT_EQ(c[1], 6.0);
}

TEST(float64x2, vector_sub)
{
    simd_float64x2 a{10.0, 20.0};
    simd_float64x2 b{3.0, 8.0};
    auto c = a - b;
    EXPECT_EQ(c[0], 7.0);
    EXPECT_EQ(c[1], 12.0);
}

TEST(float64x2, vector_mul)
{
    simd_float64x2 a{2.0, 3.0};
    simd_float64x2 b{4.0, 5.0};
    auto c = a * b;
    EXPECT_EQ(c[0], 8.0);
    EXPECT_EQ(c[1], 15.0);
}

TEST(float64x2, vector_div)
{
    simd_float64x2 a{10.0, 20.0};
    simd_float64x2 b{2.0, 4.0};
    auto c = a / b;
    EXPECT_EQ(c[0], 5.0);
    EXPECT_EQ(c[1], 5.0);
}

TEST(float64x2, scalar_add)
{
    simd_float64x2 a{1.0, 2.0};
    auto b = a + 10.0;
    EXPECT_EQ(b[0], 11.0);
    EXPECT_EQ(b[1], 12.0);
}

TEST(float64x2, scalar_mul)
{
    simd_float64x2 a{1.0, 2.0};
    auto b = a * 3.0;
    EXPECT_EQ(b[0], 3.0);
    EXPECT_EQ(b[1], 6.0);
}

TEST(float64x2, unary_minus)
{
    simd_float64x2 a{1.0, -2.0};
    auto b = -a;
    EXPECT_EQ(b[0], -1.0);
    EXPECT_EQ(b[1], 2.0);
}

TEST(float64x2, abs)
{
    simd_float64x2 a{-1.0, 2.0};
    auto b = abs(a);
    EXPECT_EQ(b[0], 1.0);
    EXPECT_EQ(b[1], 2.0);
}

TEST(float64x2, sqrt)
{
    simd_float64x2 a{4.0, 9.0};
    auto b = sqrt(a);
    EXPECT_EQ(b[0], 2.0);
    EXPECT_EQ(b[1], 3.0);
}

TEST(float64x2, reciprocal)
{
    simd_float64x2 a{2.0, 4.0};
    auto b = reciprocal(a);
    EXPECT_EQ(b[0], 0.5);
    EXPECT_EQ(b[1], 0.25);
}

TEST(float64x2, min)
{
    simd_float64x2 a{1.0, 5.0};
    simd_float64x2 b{2.0, 3.0};
    auto c = min(a, b);
    EXPECT_EQ(c[0], 1.0);
    EXPECT_EQ(c[1], 3.0);
}

TEST(float64x2, max)
{
    simd_float64x2 a{1.0, 5.0};
    simd_float64x2 b{2.0, 3.0};
    auto c = max(a, b);
    EXPECT_EQ(c[0], 2.0);
    EXPECT_EQ(c[1], 5.0);
}

TEST(float64x2, clamp)
{
    simd_float64x2 v{-1.0, 1.5};
    simd_float64x2 lo{0.0, 0.0};
    simd_float64x2 hi{1.0, 1.0};
    auto c = clamp(v, lo, hi);
    EXPECT_EQ(c[0], 0.0);  // Clamped to min
    EXPECT_EQ(c[1], 1.0);  // Clamped to max
}

TEST(float64x2, madd)
{
    simd_float64x2 a{1.0, 2.0};
    simd_float64x2 b{3.0, 4.0};
    simd_float64x2 c{10.0, 20.0};
    auto r = madd(a, b, c);
    EXPECT_EQ(r[0], 13.0);  // 1*3 + 10
    EXPECT_EQ(r[1], 28.0);  // 2*4 + 20
}

TEST(float64x2, lerp)
{
    simd_float64x2 a{0.0, 0.0};
    simd_float64x2 b{10.0, 20.0};
    simd_float64x2 t{0.0, 0.5};
    auto r = lerp(a, b, t);
    EXPECT_EQ(r[0], 0.0);   // t=0 -> a
    EXPECT_EQ(r[1], 10.0);  // t=0.5 -> midpoint
}

TEST(float64x2, nmsub)
{
    simd_float64x2 a{1.0, 2.0};
    simd_float64x2 b{3.0, 4.0};
    simd_float64x2 c{10.0, 20.0};
    auto r = nmsub(a, b, c);
    // c - a * b
    EXPECT_EQ(r[0], 7.0);   // 10 - 1*3
    EXPECT_EQ(r[1], 12.0);  // 20 - 2*4
}

TEST(float64x2, hsum)
{
    simd_float64x2 a{1.0, 2.0};
    EXPECT_EQ(a.hsum(), 3.0);
}

TEST(float64x2, hprod)
{
    simd_float64x2 a{3.0, 4.0};
    EXPECT_EQ(a.hprod(), 12.0);
}

TEST(float64x2, hmin)
{
    simd_float64x2 a{3.0, 1.0};
    EXPECT_EQ(a.hmin(), 1.0);
}

TEST(float64x2, hmax)
{
    simd_float64x2 a{3.0, 1.0};
    EXPECT_EQ(a.hmax(), 3.0);
}

TEST(float64x2, container_size)
{
    EXPECT_EQ(simd_float64x2::size(), 2U);
}

// Keep the simple test for backward compatibility
TEST(vec_types, simd_float64x2)
{
    simd_float64x2 a{1.0, 2.0};
    simd_float64x2 b{3.0, 4.0};
    auto c = a + b;
    EXPECT_EQ(c[0], 4.0);
    EXPECT_EQ(c[1], 6.0);
}

TEST(vec_types, simd_float64x4)
{
    simd_float64x4 a{1.0, 2.0, 3.0, 4.0};
    EXPECT_EQ(a.hsum(), 10.0);
}

// ===== simd_float32x8 Tests =====

TEST(vec_types, simd_float32x8)
{
    simd_float32x8 a{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    EXPECT_EQ(a.hsum(), 36.0f);
    EXPECT_EQ(simd_float32x8::size(), 8U);
}

// ===== Comprehensive simd_float32x8 Tests =====

TEST(vec8f, construction_broadcast)
{
    simd_float32x8 v(3.0f);
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(v[i], 3.0f);
    }
}

TEST(vec8f, construction_initializer_list)
{
    simd_float32x8 v{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(v[i], static_cast<float>(i + 1));
    }
}

TEST(vec8f, factory_zero)
{
    auto v = simd_float32x8::zero();
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(v[i], 0.0f);
    }
}

TEST(vec8f, factory_one)
{
    auto v = simd_float32x8::one();
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(v[i], 1.0f);
    }
}

TEST(vec8f, factory_splat)
{
    auto v = simd_float32x8::splat(42.0f);
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(v[i], 42.0f);
    }
}

TEST(vec8f, container_interface)
{
    simd_float32x8 v{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    EXPECT_EQ(simd_float32x8::size(), 8U);
    EXPECT_EQ(simd_float32x8::max_size(), 8U);
    EXPECT_FALSE(simd_float32x8::empty());
    EXPECT_EQ(v.front(), 1.0f);
    EXPECT_EQ(v.back(), 8.0f);
    EXPECT_EQ(v.at(0), 1.0f);
    EXPECT_EQ(v.at(7), 8.0f);
    EXPECT_TRUE(v.data() != nullptr);

    // const data
    auto const& cv = v;
    EXPECT_EQ(cv.data()[0], 1.0f);
    EXPECT_EQ(cv.front(), 1.0f);
    EXPECT_EQ(cv.back(), 8.0f);
    EXPECT_EQ(cv.at(3), 4.0f);
}

TEST(vec8f, container_at_throws)
{
    simd_float32x8 v{};
    bool threw = false;
    try {
        (void)v.at(8);
    } catch (std::out_of_range const&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

TEST(vec8f, iterators)
{
    simd_float32x8 v{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    float sum = 0.0f;
    for (auto it = v.begin(); it != v.end(); ++it) {
        sum += *it;
    }
    EXPECT_EQ(sum, 36.0f);

    // const iterators
    auto const& cv = v;
    float csum = 0.0f;
    for (auto it = cv.cbegin(); it != cv.cend(); ++it) {
        csum += *it;
    }
    EXPECT_EQ(csum, 36.0f);
}

TEST(vec8f, fill_and_swap)
{
    simd_float32x8 a;
    a.fill(5.0f);
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(a[i], 5.0f);
    }

    simd_float32x8 b{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    a.swap(b);
    EXPECT_EQ(a[0], 1.0f);
    EXPECT_EQ(b[0], 5.0f);
}

TEST(vec8f, zero_function)
{
    simd_float32x8 v{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    zero(v);
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(v[i], 0.0f);
    }
}

TEST(vec8f, unary_plus_minus)
{
    simd_float32x8 a{1.0f, -2.0f, 3.0f, -4.0f, 5.0f, -6.0f, 7.0f, -8.0f};
    auto p = +a;
    auto m = -a;
    EXPECT_EQ(p[0], 1.0f);
    EXPECT_EQ(m[0], -1.0f);
    EXPECT_EQ(m[1], 2.0f);
    EXPECT_EQ(m[7], 8.0f);
}

TEST(vec8f, vector_arithmetic)
{
    simd_float32x8 a{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    simd_float32x8 b{8.0f, 7.0f, 6.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f};

    auto sum = a + b;
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(sum[i], 9.0f);
    }

    auto diff = a - b;
    EXPECT_EQ(diff[0], -7.0f);
    EXPECT_EQ(diff[7], 7.0f);

    auto prod = a * b;
    EXPECT_EQ(prod[0], 8.0f);
    EXPECT_EQ(prod[3], 20.0f);

    auto quot = a / b;
    EXPECT_TRUE(approx_equal(quot[0], 0.125f, 1e-6f));
    EXPECT_EQ(quot[7], 8.0f);
}

TEST(vec8f, scalar_arithmetic)
{
    simd_float32x8 a{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};

    auto add_r = a + 10.0f;
    auto add_l = 10.0f + a;
    EXPECT_EQ(add_r[0], 11.0f);
    EXPECT_EQ(add_l[0], 11.0f);

    auto sub_r = a - 1.0f;
    auto sub_l = 10.0f - a;
    EXPECT_EQ(sub_r[0], 0.0f);
    EXPECT_EQ(sub_l[0], 9.0f);

    auto mul_r = a * 2.0f;
    auto mul_l = 2.0f * a;
    EXPECT_EQ(mul_r[7], 16.0f);
    EXPECT_EQ(mul_l[7], 16.0f);

    auto div_r = a / 2.0f;
    auto div_l = 8.0f / a;
    EXPECT_EQ(div_r[3], 2.0f);
    EXPECT_EQ(div_l[0], 8.0f);
    EXPECT_EQ(div_l[7], 1.0f);
}

TEST(vec8f, compound_assignment_vector)
{
    simd_float32x8 a{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    simd_float32x8 b{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

    auto c = a;
    c += b;
    EXPECT_EQ(c[0], 2.0f);

    c = a;
    c -= b;
    EXPECT_EQ(c[0], 0.0f);

    c = a;
    c *= b;
    EXPECT_EQ(c[7], 8.0f);

    c = a;
    c /= b;
    EXPECT_EQ(c[3], 4.0f);
}

TEST(vec8f, compound_assignment_scalar)
{
    simd_float32x8 a{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};

    auto c = a;
    c += 10.0f;
    EXPECT_EQ(c[0], 11.0f);

    c = a;
    c -= 1.0f;
    EXPECT_EQ(c[0], 0.0f);

    c = a;
    c *= 3.0f;
    EXPECT_EQ(c[2], 9.0f);

    c = a;
    c /= 2.0f;
    EXPECT_EQ(c[3], 2.0f);
}

TEST(vec8f, math_functions)
{
    simd_float32x8 a{1.0f, 4.0f, 9.0f, 16.0f, 25.0f, 36.0f, 49.0f, 64.0f};
    auto s = sqrt(a);
    EXPECT_EQ(s[0], 1.0f);
    EXPECT_EQ(s[1], 2.0f);
    EXPECT_EQ(s[7], 8.0f);

    simd_float32x8 b{-1.0f, 2.0f, -3.0f, 4.0f, -5.0f, 6.0f, -7.0f, 8.0f};
    auto ab = abs(b);
    EXPECT_EQ(ab[0], 1.0f);
    EXPECT_EQ(ab[6], 7.0f);

    simd_float32x8 zeros = simd_float32x8::zero();
    auto si = sin(zeros);
    auto co = cos(zeros);
    auto ex = exp(zeros);
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(si[i], 0.0f);
        EXPECT_EQ(co[i], 1.0f);
        EXPECT_EQ(ex[i], 1.0f);
    }

    simd_float32x8 ones = simd_float32x8::one();
    auto lg = log(ones);
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(lg[i], 0.0f);
    }

    simd_float32x8 r_in{1.0f, 2.0f, 4.0f, 5.0f, 8.0f, 10.0f, 0.5f, 0.25f};
    auto rec = reciprocal(r_in);
    EXPECT_EQ(rec[0], 1.0f);
    EXPECT_EQ(rec[1], 0.5f);
    EXPECT_EQ(rec[6], 2.0f);
    EXPECT_EQ(rec[7], 4.0f);
}

TEST(vec8f, reciprocal_sqrt)
{
    simd_float32x8 a{1.0f, 4.0f, 16.0f, 25.0f, 1.0f, 4.0f, 9.0f, 100.0f};
    auto r = reciprocal_sqrt(a);
    EXPECT_TRUE(approx_equal(r[0], 1.0f, 1e-5f));
    EXPECT_TRUE(approx_equal(r[1], 0.5f, 1e-5f));
    EXPECT_TRUE(approx_equal(r[2], 0.25f, 1e-5f));
    EXPECT_TRUE(approx_equal(r[7], 0.1f, 1e-5f));
}

TEST(vec8f, reductions)
{
    simd_float32x8 a{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    EXPECT_EQ(a.hsum(), 36.0f);
    EXPECT_EQ(a.hprod(), 40320.0f);  // 8!
    EXPECT_EQ(a.hmin(), 1.0f);
    EXPECT_EQ(a.hmax(), 8.0f);
}

TEST(vec8f, min_max_clamp)
{
    simd_float32x8 a{1.0f, 5.0f, 3.0f, 8.0f, 2.0f, 9.0f, 0.0f, 7.0f};
    simd_float32x8 b{2.0f, 3.0f, 4.0f, 6.0f, 4.0f, 1.0f, 5.0f, 3.0f};
    auto mn = min(a, b);
    auto mx = max(a, b);
    EXPECT_EQ(mn[0], 1.0f);
    EXPECT_EQ(mn[1], 3.0f);
    EXPECT_EQ(mx[0], 2.0f);
    EXPECT_EQ(mx[1], 5.0f);

    simd_float32x8 v{-1.0f, 0.5f, 1.5f, 2.0f, -0.5f, 0.0f, 1.0f, 3.0f};
    auto lo = simd_float32x8::zero();
    auto hi = simd_float32x8::one();
    auto cl = clamp(v, lo, hi);
    EXPECT_EQ(cl[0], 0.0f);
    EXPECT_EQ(cl[1], 0.5f);
    EXPECT_EQ(cl[2], 1.0f);
    EXPECT_EQ(cl[7], 1.0f);
}

TEST(vec8f, fma_lerp_nmsub)
{
    simd_float32x8 a{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    simd_float32x8 b{2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f};
    simd_float32x8 c{10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f};

    auto f = madd(a, b, c);
    EXPECT_EQ(f[0], 12.0f);  // 1*2+10
    EXPECT_EQ(f[7], 26.0f);  // 8*2+10

    simd_float32x8 t_half = simd_float32x8::splat(0.5f);
    auto l = lerp(simd_float32x8::zero(), c, t_half);
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(l[i], 5.0f);
    }

    auto n = nmsub(a, b, c);
    EXPECT_EQ(n[0], 8.0f);   // 10-1*2
    EXPECT_EQ(n[7], -6.0f);  // 10-8*2
}

TEST(vec8f, comparisons)
{
    simd_float32x8 a{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    simd_float32x8 b{2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f};

    auto eq = equal_to(a, b);
    EXPECT_EQ(eq[0], 0.0f);
    EXPECT_EQ(eq[1], 1.0f);
    EXPECT_EQ(eq[2], 0.0f);

    auto ne = not_equal_to(a, b);
    EXPECT_EQ(ne[0], 1.0f);
    EXPECT_EQ(ne[1], 0.0f);

    auto lt = less(a, b);
    EXPECT_EQ(lt[0], 1.0f);
    EXPECT_EQ(lt[1], 0.0f);
    EXPECT_EQ(lt[2], 0.0f);

    auto le = less_equal(a, b);
    EXPECT_EQ(le[0], 1.0f);
    EXPECT_EQ(le[1], 1.0f);
    EXPECT_EQ(le[2], 0.0f);

    auto gt = greater(a, b);
    EXPECT_EQ(gt[0], 0.0f);
    EXPECT_EQ(gt[1], 0.0f);
    EXPECT_EQ(gt[2], 1.0f);

    auto ge = greater_equal(a, b);
    EXPECT_EQ(ge[0], 0.0f);
    EXPECT_EQ(ge[1], 1.0f);
    EXPECT_EQ(ge[2], 1.0f);
}

TEST(vec8f, dot_product)
{
    simd_float32x8 a{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    simd_float32x8 b{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    EXPECT_EQ(dot(a, b), 36.0f);

    auto d = dot(a, a);
    EXPECT_EQ(d, 204.0f);  // 1+4+9+16+25+36+49+64
}

TEST(vec8f, arg_function)
{
    simd_float32x8 a{-1.0f, 0.0f, 1.0f, 2.0f, -3.0f, 0.0f, 4.0f, -5.0f};
    auto r = arg(a);
    // arg returns pi for negative, 0 for non-negative
    EXPECT_TRUE(approx_equal(r[0], static_cast<float>(M_PI), 1e-5f));
    EXPECT_EQ(r[1], 0.0f);
    EXPECT_EQ(r[2], 0.0f);
    EXPECT_TRUE(approx_equal(r[4], static_cast<float>(M_PI), 1e-5f));
}

// ===== Comprehensive simd_float64x2 Gap Coverage =====

TEST(vec2d, container_interface_gaps)
{
    simd_float64x2 v{1.0, 2.0};
    EXPECT_EQ(simd_float64x2::max_size(), 2U);
    EXPECT_FALSE(simd_float64x2::empty());

    // Mutable data pointer
    double* ptr = v.data();
    EXPECT_EQ(ptr[0], 1.0);

    // Const data pointer
    auto const& cv = v;
    EXPECT_EQ(cv.data()[1], 2.0);
    EXPECT_EQ(cv.front(), 1.0);
    EXPECT_EQ(cv.back(), 2.0);

    // Mutable front/back
    v.front() = 10.0;
    v.back() = 20.0;
    EXPECT_EQ(v[0], 10.0);
    EXPECT_EQ(v[1], 20.0);

    // Mutable at
    v.at(0) = 100.0;
    EXPECT_EQ(v[0], 100.0);
}

TEST(vec2d, iterators_gaps)
{
    simd_float64x2 v{3.0, 7.0};

    // Non-const begin/end
    double sum = 0.0;
    for (auto it = v.begin(); it != v.end(); ++it) {
        sum += *it;
    }
    EXPECT_EQ(sum, 10.0);

    // Const begin/end
    auto const& cv = v;
    sum = 0.0;
    for (auto it = cv.begin(); it != cv.end(); ++it) {
        sum += *it;
    }
    EXPECT_EQ(sum, 10.0);

    // cbegin/cend
    sum = 0.0;
    for (auto it = cv.cbegin(); it != cv.cend(); ++it) {
        sum += *it;
    }
    EXPECT_EQ(sum, 10.0);
}

TEST(vec2d, fill_and_swap)
{
    simd_float64x2 a;
    a.fill(5.0);
    EXPECT_EQ(a[0], 5.0);
    EXPECT_EQ(a[1], 5.0);

    simd_float64x2 b{1.0, 2.0};
    a.swap(b);
    EXPECT_EQ(a[0], 1.0);
    EXPECT_EQ(b[0], 5.0);
}

TEST(vec2d, zero_function)
{
    simd_float64x2 v{1.0, 2.0};
    zero(v);
    EXPECT_EQ(v[0], 0.0);
    EXPECT_EQ(v[1], 0.0);
}

TEST(vec2d, unary_plus)
{
    simd_float64x2 a{1.0, -2.0};
    auto p = +a;
    EXPECT_EQ(p[0], 1.0);
    EXPECT_EQ(p[1], -2.0);
}

TEST(vec2d, scalar_arithmetic_gaps)
{
    simd_float64x2 a{3.0, 6.0};

    auto add_l = 10.0 + a;
    EXPECT_EQ(add_l[0], 13.0);

    auto sub_r = a - 1.0;
    EXPECT_EQ(sub_r[0], 2.0);
    auto sub_l = 10.0 - a;
    EXPECT_EQ(sub_l[0], 7.0);

    auto mul_l = 2.0 * a;
    EXPECT_EQ(mul_l[0], 6.0);

    auto div_r = a / 3.0;
    EXPECT_EQ(div_r[0], 1.0);
    auto div_l = 12.0 / a;
    EXPECT_EQ(div_l[0], 4.0);
    EXPECT_EQ(div_l[1], 2.0);
}

TEST(vec2d, compound_assignment_vector)
{
    simd_float64x2 a{3.0, 6.0};
    simd_float64x2 b{1.0, 2.0};

    auto c = a;
    c += b;
    EXPECT_EQ(c[0], 4.0);

    c = a;
    c -= b;
    EXPECT_EQ(c[0], 2.0);

    c = a;
    c *= b;
    EXPECT_EQ(c[0], 3.0);
    EXPECT_EQ(c[1], 12.0);

    c = a;
    c /= b;
    EXPECT_EQ(c[0], 3.0);
    EXPECT_EQ(c[1], 3.0);
}

TEST(vec2d, compound_assignment_scalar)
{
    simd_float64x2 a{3.0, 6.0};

    auto c = a;
    c += 10.0;
    EXPECT_EQ(c[0], 13.0);

    c = a;
    c -= 1.0;
    EXPECT_EQ(c[0], 2.0);

    c = a;
    c *= 3.0;
    EXPECT_EQ(c[0], 9.0);

    c = a;
    c /= 3.0;
    EXPECT_EQ(c[0], 1.0);
}

TEST(vec2d, math_gaps)
{
    simd_float64x2 zeros = simd_float64x2::zero();
    auto si = sin(zeros);
    auto co = cos(zeros);
    auto ex = exp(zeros);
    EXPECT_EQ(si[0], 0.0);
    EXPECT_EQ(co[0], 1.0);
    EXPECT_EQ(ex[0], 1.0);

    simd_float64x2 ones = simd_float64x2::one();
    auto lg = log(ones);
    EXPECT_EQ(lg[0], 0.0);
}

TEST(vec2d, reciprocal_sqrt)
{
    simd_float64x2 a{4.0, 16.0};
    auto r = reciprocal_sqrt(a);
    EXPECT_TRUE(approx_equal(r[0], 0.5, 1e-12));
    EXPECT_TRUE(approx_equal(r[1], 0.25, 1e-12));
}

TEST(vec2d, comparisons)
{
    simd_float64x2 a{1.0, 3.0};
    simd_float64x2 b{2.0, 2.0};

    auto eq = equal_to(a, b);
    EXPECT_EQ(eq[0], 0.0);

    auto ne = not_equal_to(a, b);
    EXPECT_EQ(ne[0], 1.0);

    auto lt = less(a, b);
    EXPECT_EQ(lt[0], 1.0);
    EXPECT_EQ(lt[1], 0.0);

    auto le = less_equal(a, b);
    EXPECT_EQ(le[0], 1.0);

    auto gt = greater(a, b);
    EXPECT_EQ(gt[1], 1.0);

    auto ge = greater_equal(a, b);
    EXPECT_EQ(ge[1], 1.0);
}

TEST(vec2d, dot_product)
{
    simd_float64x2 a{3.0, 4.0};
    simd_float64x2 b{1.0, 1.0};
    EXPECT_EQ(dot(a, b), 7.0);
    EXPECT_EQ(dot(a, a), 25.0);
}

TEST(vec2d, arg_function)
{
    simd_float64x2 a{-1.0, 2.0};
    auto r = arg(a);
    EXPECT_TRUE(approx_equal(r[0], M_PI, 1e-12));
    EXPECT_EQ(r[1], 0.0);
}

// ===== Comprehensive simd_float64x4 Tests =====

TEST(vec4d, construction_broadcast)
{
    simd_float64x4 v(3.14);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(v[i], 3.14);
    }
}

TEST(vec4d, construction_initializer_list)
{
    simd_float64x4 v{1.0, 2.0, 3.0, 4.0};
    EXPECT_EQ(v[0], 1.0);
    EXPECT_EQ(v[1], 2.0);
    EXPECT_EQ(v[2], 3.0);
    EXPECT_EQ(v[3], 4.0);
}

TEST(vec4d, factory_zero)
{
    auto v = simd_float64x4::zero();
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(v[i], 0.0);
    }
}

TEST(vec4d, factory_one)
{
    auto v = simd_float64x4::one();
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(v[i], 1.0);
    }
}

TEST(vec4d, factory_splat)
{
    auto v = simd_float64x4::splat(42.0);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(v[i], 42.0);
    }
}

TEST(vec4d, container_interface)
{
    simd_float64x4 v{1.0, 2.0, 3.0, 4.0};
    EXPECT_EQ(simd_float64x4::size(), 4U);
    EXPECT_EQ(simd_float64x4::max_size(), 4U);
    EXPECT_FALSE(simd_float64x4::empty());
    EXPECT_EQ(v.front(), 1.0);
    EXPECT_EQ(v.back(), 4.0);
    EXPECT_EQ(v.at(0), 1.0);
    EXPECT_EQ(v.at(3), 4.0);
    EXPECT_TRUE(v.data() != nullptr);

    auto const& cv = v;
    EXPECT_EQ(cv.data()[0], 1.0);
    EXPECT_EQ(cv.front(), 1.0);
    EXPECT_EQ(cv.back(), 4.0);

    // Mutable element accessors
    v[0] = 10.0;
    EXPECT_EQ(v[0], 10.0);
    v.at(1) = 20.0;
    EXPECT_EQ(v[1], 20.0);
}

TEST(vec4d, iterators)
{
    simd_float64x4 v{1.0, 2.0, 3.0, 4.0};
    double sum = 0.0;
    for (auto it = v.begin(); it != v.end(); ++it) {
        sum += *it;
    }
    EXPECT_EQ(sum, 10.0);

    auto const& cv = v;
    sum = 0.0;
    for (auto it = cv.cbegin(); it != cv.cend(); ++it) {
        sum += *it;
    }
    EXPECT_EQ(sum, 10.0);
}

TEST(vec4d, fill_and_swap)
{
    simd_float64x4 a;
    a.fill(5.0);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(a[i], 5.0);
    }

    simd_float64x4 b{1.0, 2.0, 3.0, 4.0};
    a.swap(b);
    EXPECT_EQ(a[0], 1.0);
    EXPECT_EQ(b[0], 5.0);
}

TEST(vec4d, zero_function)
{
    simd_float64x4 v{1.0, 2.0, 3.0, 4.0};
    zero(v);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(v[i], 0.0);
    }
}

TEST(vec4d, unary_plus_minus)
{
    simd_float64x4 a{1.0, -2.0, 3.0, -4.0};
    auto p = +a;
    auto m = -a;
    EXPECT_EQ(p[0], 1.0);
    EXPECT_EQ(m[0], -1.0);
    EXPECT_EQ(m[1], 2.0);
    EXPECT_EQ(m[3], 4.0);
}

TEST(vec4d, vector_arithmetic)
{
    simd_float64x4 a{1.0, 2.0, 3.0, 4.0};
    simd_float64x4 b{4.0, 3.0, 2.0, 1.0};

    auto sum = a + b;
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(sum[i], 5.0);
    }

    auto diff = a - b;
    EXPECT_EQ(diff[0], -3.0);
    EXPECT_EQ(diff[3], 3.0);

    auto prod = a * b;
    EXPECT_EQ(prod[0], 4.0);
    EXPECT_EQ(prod[1], 6.0);

    auto quot = a / b;
    EXPECT_EQ(quot[0], 0.25);
    EXPECT_EQ(quot[3], 4.0);
}

TEST(vec4d, scalar_arithmetic)
{
    simd_float64x4 a{1.0, 2.0, 3.0, 4.0};

    auto add_r = a + 10.0;
    auto add_l = 10.0 + a;
    EXPECT_EQ(add_r[0], 11.0);
    EXPECT_EQ(add_l[0], 11.0);

    auto sub_r = a - 1.0;
    auto sub_l = 10.0 - a;
    EXPECT_EQ(sub_r[0], 0.0);
    EXPECT_EQ(sub_l[0], 9.0);

    auto mul_r = a * 2.0;
    auto mul_l = 2.0 * a;
    EXPECT_EQ(mul_r[3], 8.0);
    EXPECT_EQ(mul_l[3], 8.0);

    auto div_r = a / 2.0;
    auto div_l = 12.0 / a;
    EXPECT_EQ(div_r[1], 1.0);
    EXPECT_EQ(div_l[0], 12.0);
    EXPECT_EQ(div_l[3], 3.0);
}

TEST(vec4d, compound_assignment_vector)
{
    simd_float64x4 a{1.0, 2.0, 3.0, 4.0};
    simd_float64x4 b{1.0, 1.0, 1.0, 1.0};

    auto c = a;
    c += b;
    EXPECT_EQ(c[0], 2.0);

    c = a;
    c -= b;
    EXPECT_EQ(c[0], 0.0);

    c = a;
    c *= b;
    EXPECT_EQ(c[3], 4.0);

    c = a;
    c /= b;
    EXPECT_EQ(c[2], 3.0);
}

TEST(vec4d, compound_assignment_scalar)
{
    simd_float64x4 a{1.0, 2.0, 3.0, 4.0};

    auto c = a;
    c += 10.0;
    EXPECT_EQ(c[0], 11.0);

    c = a;
    c -= 1.0;
    EXPECT_EQ(c[0], 0.0);

    c = a;
    c *= 3.0;
    EXPECT_EQ(c[2], 9.0);

    c = a;
    c /= 2.0;
    EXPECT_EQ(c[3], 2.0);
}

TEST(vec4d, math_functions)
{
    simd_float64x4 a{1.0, 4.0, 9.0, 16.0};
    auto s = sqrt(a);
    EXPECT_EQ(s[0], 1.0);
    EXPECT_EQ(s[1], 2.0);
    EXPECT_EQ(s[3], 4.0);

    simd_float64x4 b{-1.0, 2.0, -3.0, 4.0};
    auto ab = abs(b);
    EXPECT_EQ(ab[0], 1.0);
    EXPECT_EQ(ab[2], 3.0);

    simd_float64x4 zeros = simd_float64x4::zero();
    auto si = sin(zeros);
    auto co = cos(zeros);
    auto ex = exp(zeros);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(si[i], 0.0);
        EXPECT_EQ(co[i], 1.0);
        EXPECT_EQ(ex[i], 1.0);
    }

    simd_float64x4 ones = simd_float64x4::one();
    auto lg = log(ones);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(lg[i], 0.0);
    }

    simd_float64x4 r_in{1.0, 2.0, 4.0, 5.0};
    auto rec = reciprocal(r_in);
    EXPECT_EQ(rec[0], 1.0);
    EXPECT_EQ(rec[1], 0.5);
    EXPECT_EQ(rec[2], 0.25);
    EXPECT_EQ(rec[3], 0.2);
}

TEST(vec4d, reciprocal_sqrt)
{
    simd_float64x4 a{1.0, 4.0, 16.0, 25.0};
    auto r = reciprocal_sqrt(a);
    EXPECT_TRUE(approx_equal(r[0], 1.0, 1e-12));
    EXPECT_TRUE(approx_equal(r[1], 0.5, 1e-12));
    EXPECT_TRUE(approx_equal(r[2], 0.25, 1e-12));
    EXPECT_TRUE(approx_equal(r[3], 0.2, 1e-12));
}

TEST(vec4d, reductions)
{
    simd_float64x4 a{1.0, 2.0, 3.0, 4.0};
    EXPECT_EQ(a.hsum(), 10.0);
    EXPECT_EQ(a.hprod(), 24.0);
    EXPECT_EQ(a.hmin(), 1.0);
    EXPECT_EQ(a.hmax(), 4.0);
}

TEST(vec4d, min_max_clamp)
{
    simd_float64x4 a{1.0, 5.0, 3.0, 8.0};
    simd_float64x4 b{2.0, 3.0, 4.0, 6.0};
    auto mn = min(a, b);
    auto mx = max(a, b);
    EXPECT_EQ(mn[0], 1.0);
    EXPECT_EQ(mn[1], 3.0);
    EXPECT_EQ(mx[0], 2.0);
    EXPECT_EQ(mx[1], 5.0);

    simd_float64x4 v{-1.0, 0.5, 1.5, 2.0};
    auto lo = simd_float64x4::zero();
    auto hi = simd_float64x4::one();
    auto cl = clamp(v, lo, hi);
    EXPECT_EQ(cl[0], 0.0);
    EXPECT_EQ(cl[1], 0.5);
    EXPECT_EQ(cl[2], 1.0);
    EXPECT_EQ(cl[3], 1.0);
}

TEST(vec4d, fma_lerp_nmsub)
{
    simd_float64x4 a{1.0, 2.0, 3.0, 4.0};
    simd_float64x4 b{2.0, 2.0, 2.0, 2.0};
    simd_float64x4 c{10.0, 10.0, 10.0, 10.0};

    auto f = madd(a, b, c);
    EXPECT_EQ(f[0], 12.0);  // 1*2+10
    EXPECT_EQ(f[3], 18.0);  // 4*2+10

    simd_float64x4 t_half = simd_float64x4::splat(0.5);
    auto l = lerp(simd_float64x4::zero(), c, t_half);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(l[i], 5.0);
    }

    auto n = nmsub(a, b, c);
    EXPECT_EQ(n[0], 8.0);  // 10-1*2
    EXPECT_EQ(n[3], 2.0);  // 10-4*2
}

TEST(vec4d, comparisons)
{
    simd_float64x4 a{1.0, 2.0, 3.0, 4.0};
    simd_float64x4 b{2.0, 2.0, 2.0, 2.0};

    auto eq = equal_to(a, b);
    EXPECT_EQ(eq[0], 0.0);
    EXPECT_EQ(eq[1], 1.0);

    auto ne = not_equal_to(a, b);
    EXPECT_EQ(ne[0], 1.0);
    EXPECT_EQ(ne[1], 0.0);

    auto lt = less(a, b);
    EXPECT_EQ(lt[0], 1.0);
    EXPECT_EQ(lt[1], 0.0);

    auto le = less_equal(a, b);
    EXPECT_EQ(le[0], 1.0);
    EXPECT_EQ(le[1], 1.0);

    auto gt = greater(a, b);
    EXPECT_EQ(gt[0], 0.0);
    EXPECT_EQ(gt[2], 1.0);

    auto ge = greater_equal(a, b);
    EXPECT_EQ(ge[0], 0.0);
    EXPECT_EQ(ge[1], 1.0);
    EXPECT_EQ(ge[2], 1.0);
}

TEST(vec4d, dot_product)
{
    simd_float64x4 a{1.0, 2.0, 3.0, 4.0};
    simd_float64x4 b{1.0, 1.0, 1.0, 1.0};
    EXPECT_EQ(dot(a, b), 10.0);
    EXPECT_EQ(dot(a, a), 30.0);  // 1+4+9+16
}

TEST(vec4d, arg_function)
{
    simd_float64x4 a{-1.0, 0.0, 1.0, -2.0};
    auto r = arg(a);
    EXPECT_TRUE(approx_equal(r[0], M_PI, 1e-12));
    EXPECT_EQ(r[1], 0.0);
    EXPECT_EQ(r[2], 0.0);
    EXPECT_TRUE(approx_equal(r[3], M_PI, 1e-12));
}

// ===== float32x4 gap coverage (left-sub, left-div, reciprocal_sqrt, comparisons, dot, arg) =====

TEST(vec4f_gaps, left_sub)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    auto r = 10.0f - a;
    EXPECT_EQ(r[0], 9.0f);
    EXPECT_EQ(r[3], 6.0f);
}

TEST(vec4f_gaps, left_div)
{
    simd_float32x4 a{1.0f, 2.0f, 4.0f, 5.0f};
    auto r = 20.0f / a;
    EXPECT_EQ(r[0], 20.0f);
    EXPECT_EQ(r[1], 10.0f);
    EXPECT_EQ(r[2], 5.0f);
    EXPECT_EQ(r[3], 4.0f);
}

TEST(vec4f_gaps, reciprocal_sqrt)
{
    simd_float32x4 a{1.0f, 4.0f, 16.0f, 25.0f};
    auto r = reciprocal_sqrt(a);
    EXPECT_TRUE(approx_equal(r[0], 1.0f, 1e-5f));
    EXPECT_TRUE(approx_equal(r[1], 0.5f, 1e-5f));
    EXPECT_TRUE(approx_equal(r[2], 0.25f, 1e-5f));
    EXPECT_TRUE(approx_equal(r[3], 0.2f, 1e-5f));
}

TEST(vec4f_gaps, comparisons)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    simd_float32x4 b{2.0f, 2.0f, 2.0f, 2.0f};

    auto eq = equal_to(a, b);
    EXPECT_EQ(eq[0], 0.0f);
    EXPECT_EQ(eq[1], 1.0f);

    auto ne = not_equal_to(a, b);
    EXPECT_EQ(ne[0], 1.0f);
    EXPECT_EQ(ne[1], 0.0f);

    auto lt = less(a, b);
    EXPECT_EQ(lt[0], 1.0f);
    EXPECT_EQ(lt[1], 0.0f);

    auto le = less_equal(a, b);
    EXPECT_EQ(le[0], 1.0f);
    EXPECT_EQ(le[1], 1.0f);

    auto gt = greater(a, b);
    EXPECT_EQ(gt[0], 0.0f);
    EXPECT_EQ(gt[2], 1.0f);

    auto ge = greater_equal(a, b);
    EXPECT_EQ(ge[0], 0.0f);
    EXPECT_EQ(ge[1], 1.0f);
}

TEST(vec4f_gaps, dot_product)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    simd_float32x4 b{1.0f, 1.0f, 1.0f, 1.0f};
    EXPECT_EQ(dot(a, b), 10.0f);
}

TEST(vec4f_gaps, arg_function)
{
    simd_float32x4 a{-1.0f, 0.0f, 1.0f, -2.0f};
    auto r = arg(a);
    EXPECT_TRUE(approx_equal(r[0], static_cast<float>(M_PI), 1e-5f));
    EXPECT_EQ(r[1], 0.0f);
    EXPECT_EQ(r[2], 0.0f);
    EXPECT_TRUE(approx_equal(r[3], static_cast<float>(M_PI), 1e-5f));
}

TEST(vec4f_gaps, max_size_and_citerators)
{
    EXPECT_EQ(simd_float32x4::max_size(), 4U);

    simd_float32x4 v{1.0f, 2.0f, 3.0f, 4.0f};
    auto const& cv = v;
    float sum = 0.0f;
    for (auto it = cv.cbegin(); it != cv.cend(); ++it) {
        sum += *it;
    }
    EXPECT_EQ(sum, 10.0f);
}

// Alignment Tests

TEST(vec_alignment, simd_float32x4)
{
    // simd_float32x4 = 4 * 4 = 16 bytes, should be 16-byte aligned
    EXPECT_EQ(alignof(simd_float32x4), 16U);
}

TEST(vec_alignment, simd_float32x8)
{
    // simd_float32x8 = 8 * 4 = 32 bytes, should be 32-byte aligned
    EXPECT_EQ(alignof(simd_float32x8), 32U);
}

TEST(vec_alignment, simd_float64x4)
{
    // simd_float64x4 = 4 * 8 = 32 bytes, should be 32-byte aligned
    EXPECT_EQ(alignof(simd_float64x4), 32U);
}

TEST(vec_alignment, simd_float64x2)
{
    // simd_float64x2 = 2 * 8 = 16 bytes, should be 16-byte aligned
    EXPECT_EQ(alignof(simd_float64x2), 16U);
}

// Chaining Operations

TEST(vec_chaining, operations)
{
    simd_float32x4 a{1.0f, 2.0f, 3.0f, 4.0f};
    simd_float32x4 b{2.0f, 2.0f, 2.0f, 2.0f};

    // (a * b + 1) * 2
    auto result = (a * b + 1.0f) * 2.0f;
    EXPECT_EQ(result[0], 6.0f);   // (1*2+1)*2 = 6
    EXPECT_EQ(result[1], 10.0f);  // (2*2+1)*2 = 10
    EXPECT_EQ(result[2], 14.0f);  // (3*2+1)*2 = 14
    EXPECT_EQ(result[3], 18.0f);  // (4*2+1)*2 = 18
}

// Edge Case Tests

TEST(vec_edge, zero_operations)
{
    auto v = simd_float32x4::zero();
    auto result = v + v;
    EXPECT_EQ(result[0], 0.0f);
    EXPECT_EQ(result.hsum(), 0.0f);
    EXPECT_EQ(result.hprod(), 0.0f);
}

TEST(vec_edge, one_operations)
{
    auto v = simd_float32x4::one();
    auto result = v * v;
    EXPECT_EQ(result[0], 1.0f);
    EXPECT_EQ(result.hsum(), 4.0f);
    EXPECT_EQ(result.hprod(), 1.0f);
}

TEST(vec_edge, negative_values)
{
    simd_float32x4 v{-1.0f, -2.0f, -3.0f, -4.0f};
    EXPECT_EQ(v.hsum(), -10.0f);
    EXPECT_EQ(v.hmin(), -4.0f);
    EXPECT_EQ(v.hmax(), -1.0f);
}

TEST(vec_edge, mixed_sign_values)
{
    simd_float32x4 v{-2.0f, 1.0f, -1.0f, 2.0f};
    EXPECT_EQ(v.hsum(), 0.0f);
    EXPECT_EQ(v.hmin(), -2.0f);
    EXPECT_EQ(v.hmax(), 2.0f);
}

TEST(vec_edge, abs_negative)
{
    simd_float32x4 v{-1.0f, -2.0f, -3.0f, -4.0f};
    auto result = abs(v);
    EXPECT_EQ(result[0], 1.0f);
    EXPECT_EQ(result[3], 4.0f);
    EXPECT_EQ(result.hsum(), 10.0f);
}

TEST(vec_edge, clamp_all_below)
{
    simd_float32x4 v{-5.0f, -4.0f, -3.0f, -2.0f};
    auto lo = simd_float32x4::zero();
    auto hi = simd_float32x4::one();
    auto result = clamp(v, lo, hi);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(result[i], 0.0f);
    }
}

TEST(vec_edge, clamp_all_above)
{
    simd_float32x4 v{5.0f, 6.0f, 7.0f, 8.0f};
    auto lo = simd_float32x4::zero();
    auto hi = simd_float32x4::one();
    auto result = clamp(v, lo, hi);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(result[i], 1.0f);
    }
}

TEST(vec_edge, lerp_boundaries)
{
    simd_float32x4 a{0.0f, 0.0f, 0.0f, 0.0f};
    simd_float32x4 b{100.0f, 100.0f, 100.0f, 100.0f};
    simd_float32x4 t_zero = simd_float32x4::zero();
    simd_float32x4 t_one = simd_float32x4::one();

    auto at_zero = lerp(a, b, t_zero);
    auto at_one = lerp(a, b, t_one);

    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(at_zero[i], 0.0f);
        EXPECT_EQ(at_one[i], 100.0f);
    }
}

TEST(vec_edge, fma_zero_addend)
{
    simd_float32x4 a{2.0f, 3.0f, 4.0f, 5.0f};
    simd_float32x4 b{3.0f, 4.0f, 5.0f, 6.0f};
    auto c = simd_float32x4::zero();
    auto result = madd(a, b, c);
    EXPECT_EQ(result[0], 6.0f);
    EXPECT_EQ(result[1], 12.0f);
    EXPECT_EQ(result[2], 20.0f);
    EXPECT_EQ(result[3], 30.0f);
}

TEST(vec_edge, double_precision_accuracy)
{
    // Test that double precision maintains accuracy
    simd_float64x2 a{1.0000000001, 2.0000000002};
    simd_float64x2 b{1.0000000001, 2.0000000002};
    auto sum = a + b;
    EXPECT_TRUE(approx_equal(sum[0], 2.0000000002, 1e-9));
    EXPECT_TRUE(approx_equal(sum[1], 4.0000000004, 1e-9));
}

TEST(vec_edge, reciprocal_of_one)
{
    auto v = simd_float32x4::one();
    auto result = reciprocal(v);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(result[i], 1.0f);
    }
}

// lround tests

TEST(vec_lround, positive_values)
{
    simd_float32x4 v{1.4F, 1.5F, 1.6F, 2.5F};
    auto result = statusbar::dsp::lround(v);
    EXPECT_EQ(result[0], 1);  // 1.4 rounds to 1
    EXPECT_EQ(result[1], 2);  // 1.5 rounds to 2 (away from zero)
    EXPECT_EQ(result[2], 2);  // 1.6 rounds to 2
    EXPECT_EQ(result[3], 3);  // 2.5 rounds to 3 (away from zero)
}

TEST(vec_lround, negative_values)
{
    simd_float32x4 v{-1.4F, -1.5F, -1.6F, -2.5F};
    auto result = statusbar::dsp::lround(v);
    EXPECT_EQ(result[0], -1);  // -1.4 rounds to -1
    EXPECT_EQ(result[1], -2);  // -1.5 rounds to -2 (away from zero)
    EXPECT_EQ(result[2], -2);  // -1.6 rounds to -2
    EXPECT_EQ(result[3], -3);  // -2.5 rounds to -3 (away from zero)
}

TEST(vec_lround, zero_values)
{
    simd_float32x4 v{0.0F, -0.0F, 0.4F, -0.4F};
    auto result = statusbar::dsp::lround(v);
    EXPECT_EQ(result[0], 0);  // 0.0 rounds to 0
    EXPECT_EQ(result[1], 0);  // -0.0 rounds to 0
    EXPECT_EQ(result[2], 0);  // 0.4 rounds to 0
    EXPECT_EQ(result[3], 0);  // -0.4 rounds to 0
}

TEST(vec_lround, double_precision)
{
    simd_float64x2 v{1.5, -1.5};
    auto result = statusbar::dsp::lround(v);
    EXPECT_EQ(result[0], 2);   // 1.5 rounds to 2 (away from zero)
    EXPECT_EQ(result[1], -2);  // -1.5 rounds to -2 (away from zero)
}

TEST(vec_lround, large_values)
{
    simd_float64x2 v{1000000.4, -1000000.6};
    auto result = statusbar::dsp::lround(v);
    EXPECT_EQ(result[0], 1000000);   // rounds to nearest
    EXPECT_EQ(result[1], -1000001);  // rounds to nearest
}

//
// Tests: Scalar math functions (coverage for non-SIMD overloads)
//

TEST(dsp_scalar_math, reciprocal_float)
{
    EXPECT_TRUE(approx_equal(reciprocal(2.0f), 0.5f, 1e-6f));
    EXPECT_TRUE(approx_equal(reciprocal(4.0f), 0.25f, 1e-6f));
    EXPECT_TRUE(approx_equal(reciprocal(0.5f), 2.0f, 1e-6f));
}

TEST(dsp_scalar_math, reciprocal_double)
{
    EXPECT_TRUE(approx_equal(reciprocal(2.0), 0.5, 1e-12));
    EXPECT_TRUE(approx_equal(reciprocal(4.0), 0.25, 1e-12));
    EXPECT_TRUE(approx_equal(reciprocal(0.5), 2.0, 1e-12));
}

TEST(dsp_scalar_math, reciprocal_sqrt_float)
{
    EXPECT_TRUE(approx_equal(reciprocal_sqrt(4.0f), 0.5f, 1e-6f));
    EXPECT_TRUE(approx_equal(reciprocal_sqrt(1.0f), 1.0f, 1e-6f));
    EXPECT_TRUE(approx_equal(reciprocal_sqrt(16.0f), 0.25f, 1e-6f));
}

TEST(dsp_scalar_math, reciprocal_sqrt_double)
{
    EXPECT_TRUE(approx_equal(reciprocal_sqrt(4.0), 0.5, 1e-12));
    EXPECT_TRUE(approx_equal(reciprocal_sqrt(1.0), 1.0, 1e-12));
    EXPECT_TRUE(approx_equal(reciprocal_sqrt(16.0), 0.25, 1e-12));
}

TEST(dsp_scalar_math, equal_to_float)
{
    EXPECT_TRUE(approx_equal(equal_to(1.0f, 1.0f), 1.0f, 1e-6f));
    EXPECT_TRUE(approx_equal(equal_to(1.0f, 2.0f), 0.0f, 1e-6f));
}

TEST(dsp_scalar_math, equal_to_double)
{
    EXPECT_TRUE(approx_equal(equal_to(1.0, 1.0), 1.0, 1e-12));
    EXPECT_TRUE(approx_equal(equal_to(1.0, 2.0), 0.0, 1e-12));
}

TEST(dsp_scalar_math, not_equal_to_float)
{
    EXPECT_TRUE(approx_equal(not_equal_to(1.0f, 2.0f), 1.0f, 1e-6f));
    EXPECT_TRUE(approx_equal(not_equal_to(1.0f, 1.0f), 0.0f, 1e-6f));
}

TEST(dsp_scalar_math, not_equal_to_double)
{
    EXPECT_TRUE(approx_equal(not_equal_to(1.0, 2.0), 1.0, 1e-12));
    EXPECT_TRUE(approx_equal(not_equal_to(1.0, 1.0), 0.0, 1e-12));
}

TEST(dsp_scalar_math, less_float)
{
    EXPECT_TRUE(approx_equal(less(1.0f, 2.0f), 1.0f, 1e-6f));
    EXPECT_TRUE(approx_equal(less(2.0f, 1.0f), 0.0f, 1e-6f));
    EXPECT_TRUE(approx_equal(less(1.0f, 1.0f), 0.0f, 1e-6f));
}

TEST(dsp_scalar_math, less_double)
{
    EXPECT_TRUE(approx_equal(less(1.0, 2.0), 1.0, 1e-12));
    EXPECT_TRUE(approx_equal(less(2.0, 1.0), 0.0, 1e-12));
}

TEST(dsp_scalar_math, less_equal_float)
{
    EXPECT_TRUE(approx_equal(less_equal(1.0f, 2.0f), 1.0f, 1e-6f));
    EXPECT_TRUE(approx_equal(less_equal(1.0f, 1.0f), 1.0f, 1e-6f));
    EXPECT_TRUE(approx_equal(less_equal(2.0f, 1.0f), 0.0f, 1e-6f));
}

TEST(dsp_scalar_math, less_equal_double)
{
    EXPECT_TRUE(approx_equal(less_equal(1.0, 2.0), 1.0, 1e-12));
    EXPECT_TRUE(approx_equal(less_equal(1.0, 1.0), 1.0, 1e-12));
    EXPECT_TRUE(approx_equal(less_equal(2.0, 1.0), 0.0, 1e-12));
}

TEST(dsp_scalar_math, greater_float)
{
    EXPECT_TRUE(approx_equal(greater(2.0f, 1.0f), 1.0f, 1e-6f));
    EXPECT_TRUE(approx_equal(greater(1.0f, 2.0f), 0.0f, 1e-6f));
    EXPECT_TRUE(approx_equal(greater(1.0f, 1.0f), 0.0f, 1e-6f));
}

TEST(dsp_scalar_math, greater_double)
{
    EXPECT_TRUE(approx_equal(greater(2.0, 1.0), 1.0, 1e-12));
    EXPECT_TRUE(approx_equal(greater(1.0, 2.0), 0.0, 1e-12));
}

TEST(dsp_scalar_math, greater_equal_float)
{
    EXPECT_TRUE(approx_equal(greater_equal(2.0f, 1.0f), 1.0f, 1e-6f));
    EXPECT_TRUE(approx_equal(greater_equal(1.0f, 1.0f), 1.0f, 1e-6f));
    EXPECT_TRUE(approx_equal(greater_equal(1.0f, 2.0f), 0.0f, 1e-6f));
}

TEST(dsp_scalar_math, greater_equal_double)
{
    EXPECT_TRUE(approx_equal(greater_equal(2.0, 1.0), 1.0, 1e-12));
    EXPECT_TRUE(approx_equal(greater_equal(1.0, 1.0), 1.0, 1e-12));
    EXPECT_TRUE(approx_equal(greater_equal(1.0, 2.0), 0.0, 1e-12));
}

TEST(dsp_scalar_math, lround_float)
{
    EXPECT_EQ(statusbar::dsp::lround(1.5f), 2);
    EXPECT_EQ(statusbar::dsp::lround(-1.5f), -2);
    EXPECT_EQ(statusbar::dsp::lround(0.4f), 0);
    EXPECT_EQ(statusbar::dsp::lround(-0.4f), 0);
    EXPECT_EQ(statusbar::dsp::lround(0.0f), 0);
}

//
// Tests: Runtime coverage for constexpr dsp constants
//

TEST(dsp_constants_rt, double_constants)
{
    double volatile ten = constants::ten<double>();
    EXPECT_TRUE(approx_equal(ten, 10.0, 1e-12));

    double volatile twenty_recip = constants::twenty_recip<double>();
    EXPECT_TRUE(approx_equal(twenty_recip, 0.05, 1e-12));

    double volatile pi_over_two = constants::pi_over_two<double>();
    EXPECT_TRUE(pi_over_two > 1.57 && pi_over_two < 1.58);

    double volatile e_val = constants::e<double>();
    EXPECT_TRUE(e_val > 2.71 && e_val < 2.72);

    double volatile sqrt2_recip = constants::sqrt_2_recip<double>();
    EXPECT_TRUE(sqrt2_recip > 0.70 && sqrt2_recip < 0.71);

    double volatile pi_recip = constants::pi_recip<double>();
    EXPECT_TRUE(constants::pi<double>() * pi_recip > 0.999);

    double volatile e_recip = constants::e_recip<double>();
    EXPECT_TRUE(constants::e<double>() * e_recip > 0.999);

    double volatile two_pi_recip = constants::two_pi_recip<double>();
    EXPECT_TRUE(constants::two_pi<double>() * two_pi_recip > 0.999);

    double volatile recip_48k = constants::recip_48k<double>();
    EXPECT_TRUE(recip_48k > 2.08e-5 && recip_48k < 2.09e-5);

    double volatile recip_96k = constants::recip_96k<double>();
    EXPECT_TRUE(recip_96k > 1.04e-5 && recip_96k < 1.05e-5);
}

TEST(dsp_constants_rt, float_pi)
{
    float volatile pi = constants::pi<float>();
    EXPECT_TRUE(pi > 3.14f && pi < 3.15f);
}

//
// Runtime coverage for lround(float) via volatile to prevent constexpr folding
//

TEST(dsp_lround_rt, float_volatile)
{
    float volatile v_1_5 = 1.5f;
    EXPECT_EQ(statusbar::dsp::lround(v_1_5), 2);

    float volatile v_neg_1_5 = -1.5f;
    EXPECT_EQ(statusbar::dsp::lround(v_neg_1_5), -2);

    float volatile v_0_4 = 0.4f;
    EXPECT_EQ(statusbar::dsp::lround(v_0_4), 0);

    float volatile v_neg_0_4 = -0.4f;
    EXPECT_EQ(statusbar::dsp::lround(v_neg_0_4), 0);

    float volatile v_0 = 0.0f;
    EXPECT_EQ(statusbar::dsp::lround(v_0), 0);

    float volatile v_large = 1000.7f;
    EXPECT_EQ(statusbar::dsp::lround(v_large), 1001);
}

//
// Tests: Scalar splat/one free functions (coverage for non-SIMD overloads)
//

TEST(dsp_scalar_splat_one, splat_float)
{
    float v = 0.0f;
    auto& ref = statusbar::dsp::splat(v, 3.14f);
    EXPECT_TRUE(approx_equal(v, 3.14f, 1e-6f));
    EXPECT_EQ(&ref, &v);
}

TEST(dsp_scalar_splat_one, splat_double_from_float)
{
    double v = 0.0;
    auto& ref = statusbar::dsp::splat(v, 2.5f);
    EXPECT_TRUE(approx_equal(v, 2.5, 1e-12));
    EXPECT_EQ(&ref, &v);
}

TEST(dsp_scalar_splat_one, one_float)
{
    float v = 0.0f;
    auto& ref = statusbar::dsp::one(v);
    EXPECT_TRUE(approx_equal(v, 1.0f, 1e-6f));
    EXPECT_EQ(&ref, &v);
}

TEST(dsp_scalar_splat_one, one_double)
{
    double v = 0.0;
    auto& ref = statusbar::dsp::one(v);
    EXPECT_TRUE(approx_equal(v, 1.0, 1e-12));
    EXPECT_EQ(&ref, &v);
}

TEST(dsp_scalar_splat_one, splat_overwrites)
{
    float v = 99.0f;
    statusbar::dsp::splat(v, -1.0f);
    EXPECT_TRUE(approx_equal(v, -1.0f, 1e-6f));
}

TEST(dsp_scalar_splat_one, one_overwrites)
{
    double v = 99.0;
    statusbar::dsp::one(v);
    EXPECT_TRUE(approx_equal(v, 1.0, 1e-12));
}

//
// Tests: SIMD free function splat/one overloads (requires NEON or AVX)
//
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__AVX__)

TEST(dsp_simd_splat_one, splat_float4)
{
    statusbar::dsp::SIMDVec<float, 4> v{};
    auto& ref = statusbar::dsp::splat(v, 5.0f);
    EXPECT_EQ(&ref, &v);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(approx_equal(v[i], 5.0f, 1e-6f));
    }
}

TEST(dsp_simd_splat_one, splat_double2)
{
    statusbar::dsp::SIMDVec<double, 2> v{};
    auto& ref = statusbar::dsp::splat(v, 7.0);
    EXPECT_EQ(&ref, &v);
    for (size_t i = 0; i < 2; ++i) {
        EXPECT_TRUE(approx_equal(v[i], 7.0, 1e-12));
    }
}

TEST(dsp_simd_splat_one, one_float4)
{
    statusbar::dsp::SIMDVec<float, 4> v{};
    auto& ref = statusbar::dsp::one(v);
    EXPECT_EQ(&ref, &v);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(approx_equal(v[i], 1.0f, 1e-6f));
    }
}

TEST(dsp_simd_splat_one, one_double2)
{
    statusbar::dsp::SIMDVec<double, 2> v{};
    auto& ref = statusbar::dsp::one(v);
    EXPECT_EQ(&ref, &v);
    for (size_t i = 0; i < 2; ++i) {
        EXPECT_TRUE(approx_equal(v[i], 1.0, 1e-12));
    }
}

TEST(dsp_simd_splat_one, constants_zero_float4)
{
    auto v = statusbar::dsp::constants::zero<statusbar::dsp::SIMDVec<float, 4>>();
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(approx_equal(v[i], 0.0f, 1e-6f));
    }
}

TEST(dsp_simd_splat_one, constants_one_float4)
{
    auto v = statusbar::dsp::constants::one<statusbar::dsp::SIMDVec<float, 4>>();
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(approx_equal(v[i], 1.0f, 1e-6f));
    }
}

TEST(dsp_simd_splat_one, constants_zero_double2)
{
    auto v = statusbar::dsp::constants::zero<statusbar::dsp::SIMDVec<double, 2>>();
    for (size_t i = 0; i < 2; ++i) {
        EXPECT_TRUE(approx_equal(v[i], 0.0, 1e-12));
    }
}

TEST(dsp_simd_splat_one, constants_one_double2)
{
    auto v = statusbar::dsp::constants::one<statusbar::dsp::SIMDVec<double, 2>>();
    for (size_t i = 0; i < 2; ++i) {
        EXPECT_TRUE(approx_equal(v[i], 1.0, 1e-12));
    }
}

#endif  // __ARM_NEON || __AVX__

// Main test runner function required by create_test_sourcelist
TEST_MAIN(statusbar_dsp, dsp_vec_test)