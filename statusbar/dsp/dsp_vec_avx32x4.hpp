#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#if defined(__AVX__)
#    include "dsp_constants.hpp"
#    include "dsp_vec_base.hpp"

#    include <immintrin.h>

#    include <cmath>
#    include <cstddef>
#    include <initializer_list>
#    include <stdexcept>
#    include <type_traits>
#    include <utility>

namespace statusbar::dsp {

/// Intel AVX specialization of SIMDVec for float32x4
///
/// This template specialization provides optimized SIMD operations
/// using Intel AVX intrinsics for 4-element float vectors.
template <>
class alignas(simd_alignment<float, 4>()) SIMDVec<float, 4> : public SIMDVecContainer<float, 4>
{
  public:
    using simd_type = SIMDVec<float, 4>;
    using internal_type = __m128;
    // value_type / size_type / vector_size and the container/iterator interface
    // come from SIMDVecContainer.

    union
    {
        internal_type vec_;
        value_type item_[vector_size];
    };

    /// Default constructor - does not initialize values for performance
    SIMDVec() noexcept = default;

    /// Broadcast constructor - fills all elements with the same value
    explicit SIMDVec(value_type v) noexcept { vec_ = _mm_set1_ps(v); }

    /// Construct from four scalar values
    constexpr SIMDVec(value_type v0, value_type v1, value_type v2, value_type v3) noexcept
        : item_{v0, v1, v2, v3}
    {}

    /// Initializer list constructor with zero-fill for missing elements
    SIMDVec(std::initializer_list<value_type> init) noexcept
    {
        size_type i = 0;
        for (auto val : init) {
            if (i < vector_size) {
                item_[i++] = val;
            }
        }
        while (i < vector_size) {
            item_[i++] = value_type{0};
        }
    }

    /// Copy constructor
    constexpr SIMDVec(simd_type const& other) noexcept = default;

    /// Copy assignment operator
    constexpr simd_type& operator=(simd_type const& other) noexcept = default;

    /// Move constructor
    constexpr SIMDVec(simd_type&& other) noexcept = default;

    /// Move assignment operator
    constexpr simd_type& operator=(simd_type&& other) noexcept = default;

    /// Create a zero-initialized vector
    [[nodiscard]] static simd_type zero() noexcept
    {
        simd_type result;
        result.vec_ = _mm_setzero_ps();
        return result;
    }

    /// Create a vector with all elements set to one
    [[nodiscard]] static simd_type one() noexcept
    {
        simd_type result;
        result.vec_ = _mm_set1_ps(1.0f);
        return result;
    }

    /// Create a vector with all elements set to the given value
    [[nodiscard]] static simd_type splat(value_type v) noexcept
    {
        simd_type result;
        result.vec_ = _mm_set1_ps(v);
        return result;
    }

    /// Fill all elements with a specific value
    auto fill(value_type v) noexcept { vec_ = _mm_set1_ps(v); }

    /// Swap values with another vector
    auto swap(simd_type& other) noexcept
    {
        internal_type temp = vec_;
        vec_ = other.vec_;
        other.vec_ = temp;
    }

    // Horizontal Reduction Operations

    /// Horizontal sum of all lanes using SSE3 hadd
    [[nodiscard]] value_type hsum() const noexcept
    {
        // Horizontal add: [a0+a1, a2+a3, a0+a1, a2+a3]
        __m128 sum1 = _mm_hadd_ps(vec_, vec_);
        // Second horizontal add: [sum, sum, sum, sum]
        __m128 sum2 = _mm_hadd_ps(sum1, sum1);
        return _mm_cvtss_f32(sum2);
    }

    /// Horizontal product of all lanes
    [[nodiscard]] constexpr value_type hprod() const noexcept { return item_[0] * item_[1] * item_[2] * item_[3]; }

    /// Horizontal minimum of all lanes
    [[nodiscard]] value_type hmin() const noexcept
    {
        // Swap pairs: [a2, a3, a0, a1]
        __m128 temp = _mm_shuffle_ps(vec_, vec_, _MM_SHUFFLE(1, 0, 3, 2));
        // Min of pairs: [min(a0,a2), min(a1,a3), ...]
        __m128 min1 = _mm_min_ps(vec_, temp);
        // Swap adjacent: [min(a1,a3), min(a0,a2), ...]
        temp = _mm_shuffle_ps(min1, min1, _MM_SHUFFLE(2, 3, 0, 1));
        // Final min
        __m128 min2 = _mm_min_ps(min1, temp);
        return _mm_cvtss_f32(min2);
    }

    /// Horizontal maximum of all lanes
    [[nodiscard]] value_type hmax() const noexcept
    {
        // Swap pairs: [a2, a3, a0, a1]
        __m128 temp = _mm_shuffle_ps(vec_, vec_, _MM_SHUFFLE(1, 0, 3, 2));
        // Max of pairs: [max(a0,a2), max(a1,a3), ...]
        __m128 max1 = _mm_max_ps(vec_, temp);
        // Swap adjacent: [max(a1,a3), max(a0,a2), ...]
        temp = _mm_shuffle_ps(max1, max1, _MM_SHUFFLE(2, 3, 0, 1));
        // Final max
        __m128 max2 = _mm_max_ps(max1, temp);
        return _mm_cvtss_f32(max2);
    }

    // Compound Assignment Operators(SIMD) ====================

    /// Add another vector to this one
    simd_type& operator+=(simd_type const& other) noexcept
    {
        vec_ = _mm_add_ps(vec_, other.vec_);
        return *this;
    }

    /// Subtract another vector from this one
    simd_type& operator-=(simd_type const& other) noexcept
    {
        vec_ = _mm_sub_ps(vec_, other.vec_);
        return *this;
    }

    /// Multiply this vector by another
    simd_type& operator*=(simd_type const& other) noexcept
    {
        vec_ = _mm_mul_ps(vec_, other.vec_);
        return *this;
    }

    /// Divide this vector by another
    simd_type& operator/=(simd_type const& other) noexcept
    {
        vec_ = _mm_div_ps(vec_, other.vec_);
        return *this;
    }

    // Compound Assignment Operators(Scalar) ====================

    /// Add a scalar to all elements
    simd_type& operator+=(value_type scalar) noexcept
    {
        vec_ = _mm_add_ps(vec_, _mm_set1_ps(scalar));
        return *this;
    }

    /// Subtract a scalar from all elements
    simd_type& operator-=(value_type scalar) noexcept
    {
        vec_ = _mm_sub_ps(vec_, _mm_set1_ps(scalar));
        return *this;
    }

    /// Multiply all elements by a scalar
    simd_type& operator*=(value_type scalar) noexcept
    {
        vec_ = _mm_mul_ps(vec_, _mm_set1_ps(scalar));
        return *this;
    }

    /// Divide all elements by a scalar
    simd_type& operator/=(value_type scalar) noexcept
    {
        vec_ = _mm_mul_ps(vec_, _mm_set1_ps(1.0f / scalar));
        return *this;
    }

    // Unary Operators

    /// Unary negation
    [[nodiscard]] friend simd_type operator-(simd_type const& a) noexcept
    {
        simd_type result;
        result.vec_ = _mm_sub_ps(_mm_setzero_ps(), a.vec_);
        return result;
    }

    /// Unary plus (identity)
    [[nodiscard]] friend simd_type operator+(simd_type const& a) noexcept { return a; }

    // Binary Operators(SIMD) ====================

    /// Add two vectors
    [[nodiscard]] friend simd_type operator+(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_add_ps(a.vec_, b.vec_);
        return result;
    }

    /// Subtract two vectors
    [[nodiscard]] friend simd_type operator-(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_sub_ps(a.vec_, b.vec_);
        return result;
    }

    /// Multiply two vectors
    [[nodiscard]] friend simd_type operator*(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_mul_ps(a.vec_, b.vec_);
        return result;
    }

    /// Divide two vectors
    [[nodiscard]] friend simd_type operator/(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_div_ps(a.vec_, b.vec_);
        return result;
    }

    // Binary Operators(Vector + Scalar) ====================

    /// Add scalar to vector
    [[nodiscard]] friend simd_type operator+(simd_type const& a, value_type b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_add_ps(a.vec_, _mm_set1_ps(b));
        return result;
    }

    /// Subtract scalar from vector
    [[nodiscard]] friend simd_type operator-(simd_type const& a, value_type b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_sub_ps(a.vec_, _mm_set1_ps(b));
        return result;
    }

    /// Multiply vector by scalar
    [[nodiscard]] friend simd_type operator*(simd_type const& a, value_type b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_mul_ps(a.vec_, _mm_set1_ps(b));
        return result;
    }

    /// Divide vector by scalar
    [[nodiscard]] friend simd_type operator/(simd_type const& a, value_type b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_mul_ps(a.vec_, _mm_set1_ps(1.0f / b));
        return result;
    }

    // Binary Operators(Scalar + Vector) ====================

    /// Add vector to scalar
    [[nodiscard]] friend simd_type operator+(value_type a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_add_ps(_mm_set1_ps(a), b.vec_);
        return result;
    }

    /// Subtract vector from scalar
    [[nodiscard]] friend simd_type operator-(value_type a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_sub_ps(_mm_set1_ps(a), b.vec_);
        return result;
    }

    /// Multiply scalar by vector
    [[nodiscard]] friend simd_type operator*(value_type a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_mul_ps(_mm_set1_ps(a), b.vec_);
        return result;
    }

    /// Divide scalar by vector
    [[nodiscard]] friend simd_type operator/(value_type a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_div_ps(_mm_set1_ps(a), b.vec_);
        return result;
    }

    // Mathematical Functions

    /// Element-wise square root using SSE
    [[nodiscard]] friend simd_type sqrt(simd_type const& a) noexcept
    {
        simd_type result;
        result.vec_ = _mm_sqrt_ps(a.vec_);
        return result;
    }

    /// Element-wise absolute value using SSE bit masking
    [[nodiscard]] friend simd_type abs(simd_type const& a) noexcept
    {
        simd_type result;
        // Clear the sign bit by ANDing with 0x7FFFFFFF
        __m128 sign_mask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
        result.vec_ = _mm_and_ps(a.vec_, sign_mask);
        return result;
    }

    /// Element-wise argument (phase angle) - falls back to scalar
    [[nodiscard]] friend simd_type arg(simd_type const& a) noexcept
    {
        simd_type result;
        for (size_type i = 0; i < vector_size; ++i) {
            result.item_[i] = std::arg(a.item_[i]);
        }
        return result;
    }

    /// Element-wise sine - falls back to scalar
    [[nodiscard]] friend simd_type sin(simd_type const& a) noexcept
    {
        simd_type result;
        for (size_type i = 0; i < vector_size; ++i) {
            result.item_[i] = std::sin(a.item_[i]);
        }
        return result;
    }

    /// Element-wise cosine - falls back to scalar
    [[nodiscard]] friend simd_type cos(simd_type const& a) noexcept
    {
        simd_type result;
        for (size_type i = 0; i < vector_size; ++i) {
            result.item_[i] = std::cos(a.item_[i]);
        }
        return result;
    }

    /// Element-wise exponential - falls back to scalar
    [[nodiscard]] friend simd_type exp(simd_type const& a) noexcept
    {
        simd_type result;
        for (size_type i = 0; i < vector_size; ++i) {
            result.item_[i] = std::exp(a.item_[i]);
        }
        return result;
    }

    /// Element-wise natural logarithm - falls back to scalar
    [[nodiscard]] friend simd_type log(simd_type const& a) noexcept
    {
        simd_type result;
        for (size_type i = 0; i < vector_size; ++i) {
            result.item_[i] = std::log(a.item_[i]);
        }
        return result;
    }

    /// Element-wise reciprocal using SSE division
    [[nodiscard]] friend simd_type reciprocal(simd_type const& a) noexcept
    {
        simd_type result;
        result.vec_ = _mm_div_ps(_mm_set1_ps(1.0f), a.vec_);
        return result;
    }

    /// Element-wise reciprocal square root using SSE rsqrt with Newton-Raphson refinement
    [[nodiscard]] friend simd_type reciprocal_sqrt(simd_type const& a) noexcept
    {
        simd_type result;
        // Use rsqrt approximation followed by Newton-Raphson refinement for better accuracy
        __m128 approx = _mm_rsqrt_ps(a.vec_);
        // Newton-Raphson: x' = 0.5 * x * (3 - a * x * x)
        __m128 half = _mm_set1_ps(0.5f);
        __m128 three = _mm_set1_ps(3.0f);
        __m128 muls = _mm_mul_ps(_mm_mul_ps(a.vec_, approx), approx);
        result.vec_ = _mm_mul_ps(_mm_mul_ps(half, approx), _mm_sub_ps(three, muls));
        return result;
    }

    // Comparison Functions

    /// Element-wise equality comparison (returns 1.0f or 0.0f)
    [[nodiscard]] friend simd_type equal_to(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        __m128 cmp = _mm_cmpeq_ps(a.vec_, b.vec_);
        result.vec_ = _mm_and_ps(cmp, _mm_set1_ps(1.0f));
        return result;
    }

    /// Element-wise inequality comparison (returns 1.0f or 0.0f)
    [[nodiscard]] friend simd_type not_equal_to(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        __m128 cmp = _mm_cmpneq_ps(a.vec_, b.vec_);
        result.vec_ = _mm_and_ps(cmp, _mm_set1_ps(1.0f));
        return result;
    }

    /// Element-wise less-than comparison (returns 1.0f or 0.0f)
    [[nodiscard]] friend simd_type less(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        __m128 cmp = _mm_cmplt_ps(a.vec_, b.vec_);
        result.vec_ = _mm_and_ps(cmp, _mm_set1_ps(1.0f));
        return result;
    }

    /// Element-wise less-than-or-equal comparison (returns 1.0f or 0.0f)
    [[nodiscard]] friend simd_type less_equal(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        __m128 cmp = _mm_cmple_ps(a.vec_, b.vec_);
        result.vec_ = _mm_and_ps(cmp, _mm_set1_ps(1.0f));
        return result;
    }

    /// Element-wise greater-than comparison (returns 1.0f or 0.0f)
    [[nodiscard]] friend simd_type greater(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        __m128 cmp = _mm_cmpgt_ps(a.vec_, b.vec_);
        result.vec_ = _mm_and_ps(cmp, _mm_set1_ps(1.0f));
        return result;
    }

    /// Element-wise greater-than-or-equal comparison (returns 1.0f or 0.0f)
    [[nodiscard]] friend simd_type greater_equal(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        __m128 cmp = _mm_cmpge_ps(a.vec_, b.vec_);
        result.vec_ = _mm_and_ps(cmp, _mm_set1_ps(1.0f));
        return result;
    }

    // Min/Max/Clamp Functions

    /// Element-wise minimum using SSE
    [[nodiscard]] friend simd_type min(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_min_ps(a.vec_, b.vec_);
        return result;
    }

    /// Element-wise maximum using SSE
    [[nodiscard]] friend simd_type max(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_max_ps(a.vec_, b.vec_);
        return result;
    }

    /// Clamp values to range [lo, hi] element-wise
    [[nodiscard]] friend simd_type clamp(simd_type const& v, simd_type const& lo, simd_type const& hi) noexcept
    {
        return min(max(v, lo), hi);
    }

    // FMA and Lerp Functions

    /// Fused multiply-add: a * b + c
    [[nodiscard]] friend simd_type madd(simd_type const& a, simd_type const& b, simd_type const& c) noexcept
    {
        simd_type result;
#    if defined(__FMA__)
        // Use hardware FMA if available
        result.vec_ = _mm_fmadd_ps(a.vec_, b.vec_, c.vec_);
#    else
        // Fallback to separate multiply and add
        result.vec_ = _mm_add_ps(_mm_mul_ps(a.vec_, b.vec_), c.vec_);
#    endif
        return result;
    }

    /// Negative multiply-subtract: c - a * b (NEON vmlsq equivalent)
    [[nodiscard]] friend simd_type nmsub(simd_type const& a, simd_type const& b, simd_type const& c) noexcept
    {
        simd_type result;
#    if defined(__FMA__)
        // Use hardware FMA if available: c - a*b
        result.vec_ = _mm_fnmadd_ps(a.vec_, b.vec_, c.vec_);
#    else
        // Fallback to separate multiply and subtract
        result.vec_ = _mm_sub_ps(c.vec_, _mm_mul_ps(a.vec_, b.vec_));
#    endif
        return result;
    }

    /// Linear interpolation: a + t * (b - a)
    [[nodiscard]] friend simd_type lerp(simd_type const& a, simd_type const& b, simd_type const& t) noexcept
    {
        return madd(t, b - a, a);
    }

    // Dot Product

    /// Dot product of two vectors
    [[nodiscard]] friend value_type dot(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type prod = a * b;
        return prod.hsum();
    }
};

// Free Function Overloads for ADL

/// Splat a value into an AVX vector (for compatibility with generic code)
inline SIMDVec<float, 4>& splat(SIMDVec<float, 4>& v, float a) noexcept
{
    v.vec_ = _mm_set1_ps(a);
    return v;
}

/// Zero an AVX vector (for compatibility with generic code)
inline void zero(SIMDVec<float, 4>& v) noexcept
{
    v.vec_ = _mm_setzero_ps();
}

/// Set an AVX vector to all ones (for compatibility with generic code)
inline SIMDVec<float, 4>& one(SIMDVec<float, 4>& v) noexcept
{
    v.vec_ = _mm_set1_ps(1.0f);
    return v;
}

namespace constants {

template <>
[[nodiscard]] constexpr simd_float32x4 zero<simd_float32x4>() noexcept
{
    return simd_float32x4::zero();
}

template <>
[[nodiscard]] constexpr simd_float32x4 one<simd_float32x4>() noexcept
{
    return simd_float32x4::one();
}

}  // namespace constants

}  // namespace statusbar::dsp

#endif  // #if defined(__AVX__)
