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

/// Intel AVX2 specialization of SIMDVec for float32x8
///
/// This template specialization provides optimized SIMD operations
/// using Intel AVX2 intrinsics for 8-element float vectors (256-bit YMM registers).
template <>
class alignas(simd_alignment<float, 8>()) SIMDVec<float, 8>
{
  public:
    using simd_type = SIMDVec<float, 8>;
    using internal_type = __m256;
    using value_type = float;

    using pointer = value_type*;
    using const_pointer = value_type const*;
    using reference = value_type&;
    using const_reference = value_type const&;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    static constexpr size_type vector_size = 8;

    union
    {
        internal_type vec_;
        value_type item_[vector_size];
    };

    /// Default constructor - does not initialize values for performance
    SIMDVec() noexcept = default;

    /// Broadcast constructor - fills all elements with the same value
    explicit SIMDVec(value_type v) noexcept { vec_ = _mm256_set1_ps(v); }

    /// Construct from eight scalar values
    constexpr SIMDVec(
        value_type v0,
        value_type v1,
        value_type v2,
        value_type v3,
        value_type v4,
        value_type v5,
        value_type v6,
        value_type v7) noexcept
        : item_{v0, v1, v2, v3, v4, v5, v6, v7}
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

    /// Get the vector size
    [[nodiscard]] static constexpr size_type size() noexcept { return vector_size; }

    /// Get the vector maximum size
    [[nodiscard]] static constexpr size_type max_size() noexcept { return vector_size; }

    /// Vector is never empty
    [[nodiscard]] static constexpr bool empty() noexcept { return false; }

    /// Create a zero-initialized vector
    [[nodiscard]] static simd_type zero() noexcept
    {
        simd_type result;
        result.vec_ = _mm256_setzero_ps();
        return result;
    }

    /// Create a vector with all elements set to one
    [[nodiscard]] static simd_type one() noexcept
    {
        simd_type result;
        result.vec_ = _mm256_set1_ps(1.0f);
        return result;
    }

    /// Create a vector with all elements set to the given value
    [[nodiscard]] static simd_type splat(value_type v) noexcept
    {
        simd_type result;
        result.vec_ = _mm256_set1_ps(v);
        return result;
    }

    /// Fill all elements with a specific value
    auto fill(value_type v) noexcept { vec_ = _mm256_set1_ps(v); }

    /// Swap values with another vector
    auto swap(simd_type& other) noexcept
    {
        internal_type temp = vec_;
        vec_ = other.vec_;
        other.vec_ = temp;
    }

    /// Get pointer to underlying array
    [[nodiscard]] constexpr pointer data() noexcept { return item_; }

    /// Get const pointer to underlying array
    [[nodiscard]] constexpr const_pointer data() const noexcept { return item_; }

    /// Array subscript operator (const)
    [[nodiscard]] constexpr const_reference operator[](size_type index) const noexcept { return item_[index]; }

    /// Array subscript operator (non-const)
    [[nodiscard]] constexpr reference operator[](size_type index) noexcept { return item_[index]; }

    /// Bounds-checked element access (const)
    [[nodiscard]] const_reference at(size_type index) const
    {
        if (index >= vector_size) {
            detail::out_of_range("SIMDVec index out of range");
        }
        return item_[index];
    }

    /// Bounds-checked element access (non-const)
    [[nodiscard]] reference at(size_type index)
    {
        if (index >= vector_size) {
            detail::out_of_range("SIMDVec index out of range");
        }
        return item_[index];
    }

    /// Get the first element
    [[nodiscard]] constexpr reference front() noexcept { return item_[0]; }

    /// Get the first element (const)
    [[nodiscard]] constexpr const_reference front() const noexcept { return item_[0]; }

    /// Get the last element
    [[nodiscard]] constexpr reference back() noexcept { return item_[vector_size - 1]; }

    /// Get the last element (const)
    [[nodiscard]] constexpr const_reference back() const noexcept { return item_[vector_size - 1]; }

    /// Iterator to beginning
    [[nodiscard]] constexpr iterator begin() noexcept { return item_; }

    /// Const iterator to beginning
    [[nodiscard]] constexpr const_iterator begin() const noexcept { return item_; }

    /// Const iterator to beginning
    [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return item_; }

    /// Iterator to end
    [[nodiscard]] constexpr iterator end() noexcept { return item_ + vector_size; }

    /// Const iterator to end
    [[nodiscard]] constexpr const_iterator end() const noexcept { return item_ + vector_size; }

    /// Const iterator to end
    [[nodiscard]] constexpr const_iterator cend() const noexcept { return item_ + vector_size; }

    // Horizontal Reduction Operations

    /// Horizontal sum of all lanes using AVX hadd
    [[nodiscard]] value_type hsum() const noexcept
    {
        // Horizontal add within 256-bit: [a0+a1, a2+a3, a4+a5, a6+a7, ...]
        __m256 sum1 = _mm256_hadd_ps(vec_, vec_);
        // Second horizontal add: [a0+a1+a2+a3, ..., a4+a5+a6+a7, ...]
        __m256 sum2 = _mm256_hadd_ps(sum1, sum1);
        // Extract low and high 128-bit lanes
        __m128 lo = _mm256_castps256_ps128(sum2);
        __m128 hi = _mm256_extractf128_ps(sum2, 1);
        // Add the two halves
        __m128 sum3 = _mm_add_ps(lo, hi);
        return _mm_cvtss_f32(sum3);
    }

    /// Horizontal product of all lanes
    [[nodiscard]] constexpr value_type hprod() const noexcept
    {
        return item_[0] * item_[1] * item_[2] * item_[3] * item_[4] * item_[5] * item_[6] * item_[7];
    }

    /// Horizontal minimum of all lanes
    [[nodiscard]] value_type hmin() const noexcept
    {
        __m128 lo = _mm256_castps256_ps128(vec_);
        __m128 hi = _mm256_extractf128_ps(vec_, 1);
        __m128 min1 = _mm_min_ps(lo, hi);
        __m128 temp = _mm_shuffle_ps(min1, min1, _MM_SHUFFLE(1, 0, 3, 2));
        __m128 min2 = _mm_min_ps(min1, temp);
        temp = _mm_shuffle_ps(min2, min2, _MM_SHUFFLE(2, 3, 0, 1));
        __m128 min3 = _mm_min_ps(min2, temp);
        return _mm_cvtss_f32(min3);
    }

    /// Horizontal maximum of all lanes
    [[nodiscard]] value_type hmax() const noexcept
    {
        __m128 lo = _mm256_castps256_ps128(vec_);
        __m128 hi = _mm256_extractf128_ps(vec_, 1);
        __m128 max1 = _mm_max_ps(lo, hi);
        __m128 temp = _mm_shuffle_ps(max1, max1, _MM_SHUFFLE(1, 0, 3, 2));
        __m128 max2 = _mm_max_ps(max1, temp);
        temp = _mm_shuffle_ps(max2, max2, _MM_SHUFFLE(2, 3, 0, 1));
        __m128 max3 = _mm_max_ps(max2, temp);
        return _mm_cvtss_f32(max3);
    }

    // Compound Assignment Operators(SIMD) ====================

    /// Add another vector to this one
    simd_type& operator+=(simd_type const& other) noexcept
    {
        vec_ = _mm256_add_ps(vec_, other.vec_);
        return *this;
    }

    /// Subtract another vector from this one
    simd_type& operator-=(simd_type const& other) noexcept
    {
        vec_ = _mm256_sub_ps(vec_, other.vec_);
        return *this;
    }

    /// Multiply this vector by another
    simd_type& operator*=(simd_type const& other) noexcept
    {
        vec_ = _mm256_mul_ps(vec_, other.vec_);
        return *this;
    }

    /// Divide this vector by another
    simd_type& operator/=(simd_type const& other) noexcept
    {
        vec_ = _mm256_div_ps(vec_, other.vec_);
        return *this;
    }

    // Compound Assignment Operators(Scalar) ====================

    /// Add a scalar to all elements
    simd_type& operator+=(value_type scalar) noexcept
    {
        vec_ = _mm256_add_ps(vec_, _mm256_set1_ps(scalar));
        return *this;
    }

    /// Subtract a scalar from all elements
    simd_type& operator-=(value_type scalar) noexcept
    {
        vec_ = _mm256_sub_ps(vec_, _mm256_set1_ps(scalar));
        return *this;
    }

    /// Multiply all elements by a scalar
    simd_type& operator*=(value_type scalar) noexcept
    {
        vec_ = _mm256_mul_ps(vec_, _mm256_set1_ps(scalar));
        return *this;
    }

    /// Divide all elements by a scalar
    simd_type& operator/=(value_type scalar) noexcept
    {
        vec_ = _mm256_mul_ps(vec_, _mm256_set1_ps(1.0f / scalar));
        return *this;
    }

    // Unary Operators

    /// Unary negation
    [[nodiscard]] friend simd_type operator-(simd_type const& a) noexcept
    {
        simd_type result;
        result.vec_ = _mm256_sub_ps(_mm256_setzero_ps(), a.vec_);
        return result;
    }

    /// Unary plus (identity)
    [[nodiscard]] friend simd_type operator+(simd_type const& a) noexcept { return a; }

    // Binary Operators(SIMD) ====================

    /// Add two vectors
    [[nodiscard]] friend simd_type operator+(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm256_add_ps(a.vec_, b.vec_);
        return result;
    }

    /// Subtract two vectors
    [[nodiscard]] friend simd_type operator-(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm256_sub_ps(a.vec_, b.vec_);
        return result;
    }

    /// Multiply two vectors
    [[nodiscard]] friend simd_type operator*(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm256_mul_ps(a.vec_, b.vec_);
        return result;
    }

    /// Divide two vectors
    [[nodiscard]] friend simd_type operator/(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm256_div_ps(a.vec_, b.vec_);
        return result;
    }

    // Binary Operators(Vector + Scalar) ====================

    /// Add scalar to vector
    [[nodiscard]] friend simd_type operator+(simd_type const& a, value_type b) noexcept
    {
        simd_type result;
        result.vec_ = _mm256_add_ps(a.vec_, _mm256_set1_ps(b));
        return result;
    }

    /// Subtract scalar from vector
    [[nodiscard]] friend simd_type operator-(simd_type const& a, value_type b) noexcept
    {
        simd_type result;
        result.vec_ = _mm256_sub_ps(a.vec_, _mm256_set1_ps(b));
        return result;
    }

    /// Multiply vector by scalar
    [[nodiscard]] friend simd_type operator*(simd_type const& a, value_type b) noexcept
    {
        simd_type result;
        result.vec_ = _mm256_mul_ps(a.vec_, _mm256_set1_ps(b));
        return result;
    }

    /// Divide vector by scalar
    [[nodiscard]] friend simd_type operator/(simd_type const& a, value_type b) noexcept
    {
        simd_type result;
        result.vec_ = _mm256_mul_ps(a.vec_, _mm256_set1_ps(1.0f / b));
        return result;
    }

    // Binary Operators(Scalar + Vector) ====================

    /// Add vector to scalar
    [[nodiscard]] friend simd_type operator+(value_type a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm256_add_ps(_mm256_set1_ps(a), b.vec_);
        return result;
    }

    /// Subtract vector from scalar
    [[nodiscard]] friend simd_type operator-(value_type a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm256_sub_ps(_mm256_set1_ps(a), b.vec_);
        return result;
    }

    /// Multiply scalar by vector
    [[nodiscard]] friend simd_type operator*(value_type a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm256_mul_ps(_mm256_set1_ps(a), b.vec_);
        return result;
    }

    /// Divide scalar by vector
    [[nodiscard]] friend simd_type operator/(value_type a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm256_div_ps(_mm256_set1_ps(a), b.vec_);
        return result;
    }

    // Mathematical Functions

    /// Element-wise square root using AVX
    [[nodiscard]] friend simd_type sqrt(simd_type const& a) noexcept
    {
        simd_type result;
        result.vec_ = _mm256_sqrt_ps(a.vec_);
        return result;
    }

    /// Element-wise absolute value using AVX bit masking
    [[nodiscard]] friend simd_type abs(simd_type const& a) noexcept
    {
        simd_type result;
        // Clear the sign bit by ANDing with 0x7FFFFFFF
        __m256 sign_mask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF));
        result.vec_ = _mm256_and_ps(a.vec_, sign_mask);
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

    /// Element-wise reciprocal using AVX division
    [[nodiscard]] friend simd_type reciprocal(simd_type const& a) noexcept
    {
        simd_type result;
        result.vec_ = _mm256_div_ps(_mm256_set1_ps(1.0f), a.vec_);
        return result;
    }

    /// Element-wise reciprocal square root using AVX rsqrt with Newton-Raphson refinement
    [[nodiscard]] friend simd_type reciprocal_sqrt(simd_type const& a) noexcept
    {
        simd_type result;
        // Use rsqrt approximation followed by Newton-Raphson refinement for better accuracy
        __m256 approx = _mm256_rsqrt_ps(a.vec_);
        // Newton-Raphson: x' = 0.5 * x * (3 - a * x * x)
        __m256 half = _mm256_set1_ps(0.5f);
        __m256 three = _mm256_set1_ps(3.0f);
        __m256 muls = _mm256_mul_ps(_mm256_mul_ps(a.vec_, approx), approx);
        result.vec_ = _mm256_mul_ps(_mm256_mul_ps(half, approx), _mm256_sub_ps(three, muls));
        return result;
    }

    // Comparison Functions

    /// Element-wise equality comparison (returns 1.0f or 0.0f)
    [[nodiscard]] friend simd_type equal_to(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        __m256 cmp = _mm256_cmp_ps(a.vec_, b.vec_, _CMP_EQ_OQ);
        result.vec_ = _mm256_and_ps(cmp, _mm256_set1_ps(1.0f));
        return result;
    }

    /// Element-wise inequality comparison (returns 1.0f or 0.0f)
    [[nodiscard]] friend simd_type not_equal_to(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        __m256 cmp = _mm256_cmp_ps(a.vec_, b.vec_, _CMP_NEQ_OQ);
        result.vec_ = _mm256_and_ps(cmp, _mm256_set1_ps(1.0f));
        return result;
    }

    /// Element-wise less-than comparison (returns 1.0f or 0.0f)
    [[nodiscard]] friend simd_type less(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        __m256 cmp = _mm256_cmp_ps(a.vec_, b.vec_, _CMP_LT_OQ);
        result.vec_ = _mm256_and_ps(cmp, _mm256_set1_ps(1.0f));
        return result;
    }

    /// Element-wise less-than-or-equal comparison (returns 1.0f or 0.0f)
    [[nodiscard]] friend simd_type less_equal(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        __m256 cmp = _mm256_cmp_ps(a.vec_, b.vec_, _CMP_LE_OQ);
        result.vec_ = _mm256_and_ps(cmp, _mm256_set1_ps(1.0f));
        return result;
    }

    /// Element-wise greater-than comparison (returns 1.0f or 0.0f)
    [[nodiscard]] friend simd_type greater(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        __m256 cmp = _mm256_cmp_ps(a.vec_, b.vec_, _CMP_GT_OQ);
        result.vec_ = _mm256_and_ps(cmp, _mm256_set1_ps(1.0f));
        return result;
    }

    /// Element-wise greater-than-or-equal comparison (returns 1.0f or 0.0f)
    [[nodiscard]] friend simd_type greater_equal(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        __m256 cmp = _mm256_cmp_ps(a.vec_, b.vec_, _CMP_GE_OQ);
        result.vec_ = _mm256_and_ps(cmp, _mm256_set1_ps(1.0f));
        return result;
    }

    // Min/Max/Clamp Functions

    /// Element-wise minimum using AVX
    [[nodiscard]] friend simd_type min(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm256_min_ps(a.vec_, b.vec_);
        return result;
    }

    /// Element-wise maximum using AVX
    [[nodiscard]] friend simd_type max(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm256_max_ps(a.vec_, b.vec_);
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
        result.vec_ = _mm256_fmadd_ps(a.vec_, b.vec_, c.vec_);
#    else
        // Fallback to separate multiply and add
        result.vec_ = _mm256_add_ps(_mm256_mul_ps(a.vec_, b.vec_), c.vec_);
#    endif
        return result;
    }

    /// Negative multiply-subtract: c - a * b
    [[nodiscard]] friend simd_type nmsub(simd_type const& a, simd_type const& b, simd_type const& c) noexcept
    {
        simd_type result;
#    if defined(__FMA__)
        // Use hardware FMA if available: c - a*b
        result.vec_ = _mm256_fnmadd_ps(a.vec_, b.vec_, c.vec_);
#    else
        // Fallback to separate multiply and subtract
        result.vec_ = _mm256_sub_ps(c.vec_, _mm256_mul_ps(a.vec_, b.vec_));
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
inline SIMDVec<float, 8>& splat(SIMDVec<float, 8>& v, float a) noexcept
{
    v.vec_ = _mm256_set1_ps(a);
    return v;
}

/// Zero an AVX vector (for compatibility with generic code)
inline void zero(SIMDVec<float, 8>& v) noexcept
{
    v.vec_ = _mm256_setzero_ps();
}

/// Set an AVX vector to all ones (for compatibility with generic code)
inline SIMDVec<float, 8>& one(SIMDVec<float, 8>& v) noexcept
{
    v.vec_ = _mm256_set1_ps(1.0f);
    return v;
}

namespace constants {

template <>
[[nodiscard]] constexpr simd_float32x8 zero<simd_float32x8>() noexcept
{
    return simd_float32x8::zero();
}

template <>
[[nodiscard]] constexpr simd_float32x8 one<simd_float32x8>() noexcept
{
    return simd_float32x8::one();
}

}  // namespace constants

}  // namespace statusbar::dsp

#endif  // #if defined(__AVX__)
