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

/// Intel SSE2 specialization of SIMDVec for float64x2
///
/// This template specialization provides optimized SIMD operations
/// using Intel SSE2 intrinsics for 2-element double vectors (128-bit XMM registers).
template <>
class alignas(simd_alignment<double, 2>()) SIMDVec<double, 2>
{
  public:
    using simd_type = SIMDVec<double, 2>;
    using internal_type = __m128d;
    using value_type = double;

    using pointer = value_type*;
    using const_pointer = value_type const*;
    using reference = value_type&;
    using const_reference = value_type const&;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    static constexpr size_type vector_size = 2;

    union
    {
        internal_type vec_;
        value_type item_[vector_size];
    };

    /// Default constructor - does not initialize values for performance
    SIMDVec() noexcept = default;

    /// Broadcast constructor - fills all elements with the same value
    explicit SIMDVec(value_type v) noexcept { vec_ = _mm_set1_pd(v); }

    /// Construct from two scalar values
    constexpr SIMDVec(value_type v0, value_type v1) noexcept
        : item_{v0, v1}
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
        result.vec_ = _mm_setzero_pd();
        return result;
    }

    /// Create a vector with all elements set to one
    [[nodiscard]] static simd_type one() noexcept
    {
        simd_type result;
        result.vec_ = _mm_set1_pd(1.0);
        return result;
    }

    /// Create a vector with all elements set to the given value
    [[nodiscard]] static simd_type splat(value_type v) noexcept
    {
        simd_type result;
        result.vec_ = _mm_set1_pd(v);
        return result;
    }

    /// Fill all elements with a specific value
    auto fill(value_type v) noexcept { vec_ = _mm_set1_pd(v); }

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

    /// Horizontal sum of all lanes using SSE3 hadd
    [[nodiscard]] value_type hsum() const noexcept
    {
        __m128d sum = _mm_hadd_pd(vec_, vec_);
        return _mm_cvtsd_f64(sum);
    }

    /// Horizontal product of all lanes
    [[nodiscard]] constexpr value_type hprod() const noexcept { return item_[0] * item_[1]; }

    /// Horizontal minimum of all lanes
    [[nodiscard]] value_type hmin() const noexcept
    {
        __m128d temp = _mm_shuffle_pd(vec_, vec_, _MM_SHUFFLE2(0, 1));
        __m128d min_val = _mm_min_pd(vec_, temp);
        return _mm_cvtsd_f64(min_val);
    }

    /// Horizontal maximum of all lanes
    [[nodiscard]] value_type hmax() const noexcept
    {
        __m128d temp = _mm_shuffle_pd(vec_, vec_, _MM_SHUFFLE2(0, 1));
        __m128d max_val = _mm_max_pd(vec_, temp);
        return _mm_cvtsd_f64(max_val);
    }

    // Compound Assignment Operators(SIMD) ====================

    /// Add another vector to this one
    simd_type& operator+=(simd_type const& other) noexcept
    {
        vec_ = _mm_add_pd(vec_, other.vec_);
        return *this;
    }

    /// Subtract another vector from this one
    simd_type& operator-=(simd_type const& other) noexcept
    {
        vec_ = _mm_sub_pd(vec_, other.vec_);
        return *this;
    }

    /// Multiply this vector by another
    simd_type& operator*=(simd_type const& other) noexcept
    {
        vec_ = _mm_mul_pd(vec_, other.vec_);
        return *this;
    }

    /// Divide this vector by another
    simd_type& operator/=(simd_type const& other) noexcept
    {
        vec_ = _mm_div_pd(vec_, other.vec_);
        return *this;
    }

    // Compound Assignment Operators(Scalar) ====================

    /// Add a scalar to all elements
    simd_type& operator+=(value_type scalar) noexcept
    {
        vec_ = _mm_add_pd(vec_, _mm_set1_pd(scalar));
        return *this;
    }

    /// Subtract a scalar from all elements
    simd_type& operator-=(value_type scalar) noexcept
    {
        vec_ = _mm_sub_pd(vec_, _mm_set1_pd(scalar));
        return *this;
    }

    /// Multiply all elements by a scalar
    simd_type& operator*=(value_type scalar) noexcept
    {
        vec_ = _mm_mul_pd(vec_, _mm_set1_pd(scalar));
        return *this;
    }

    /// Divide all elements by a scalar
    simd_type& operator/=(value_type scalar) noexcept
    {
        vec_ = _mm_mul_pd(vec_, _mm_set1_pd(1.0 / scalar));
        return *this;
    }

    // Unary Operators

    /// Unary negation
    [[nodiscard]] friend simd_type operator-(simd_type const& a) noexcept
    {
        simd_type result;
        result.vec_ = _mm_sub_pd(_mm_setzero_pd(), a.vec_);
        return result;
    }

    /// Unary plus (identity)
    [[nodiscard]] friend simd_type operator+(simd_type const& a) noexcept { return a; }

    // Binary Operators(SIMD) ====================

    /// Add two vectors
    [[nodiscard]] friend simd_type operator+(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_add_pd(a.vec_, b.vec_);
        return result;
    }

    /// Subtract two vectors
    [[nodiscard]] friend simd_type operator-(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_sub_pd(a.vec_, b.vec_);
        return result;
    }

    /// Multiply two vectors
    [[nodiscard]] friend simd_type operator*(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_mul_pd(a.vec_, b.vec_);
        return result;
    }

    /// Divide two vectors
    [[nodiscard]] friend simd_type operator/(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_div_pd(a.vec_, b.vec_);
        return result;
    }

    // Binary Operators(Vector + Scalar) ====================

    /// Add scalar to vector
    [[nodiscard]] friend simd_type operator+(simd_type const& a, value_type b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_add_pd(a.vec_, _mm_set1_pd(b));
        return result;
    }

    /// Subtract scalar from vector
    [[nodiscard]] friend simd_type operator-(simd_type const& a, value_type b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_sub_pd(a.vec_, _mm_set1_pd(b));
        return result;
    }

    /// Multiply vector by scalar
    [[nodiscard]] friend simd_type operator*(simd_type const& a, value_type b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_mul_pd(a.vec_, _mm_set1_pd(b));
        return result;
    }

    /// Divide vector by scalar
    [[nodiscard]] friend simd_type operator/(simd_type const& a, value_type b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_mul_pd(a.vec_, _mm_set1_pd(1.0 / b));
        return result;
    }

    // Binary Operators(Scalar + Vector) ====================

    /// Add vector to scalar
    [[nodiscard]] friend simd_type operator+(value_type a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_add_pd(_mm_set1_pd(a), b.vec_);
        return result;
    }

    /// Subtract vector from scalar
    [[nodiscard]] friend simd_type operator-(value_type a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_sub_pd(_mm_set1_pd(a), b.vec_);
        return result;
    }

    /// Multiply scalar by vector
    [[nodiscard]] friend simd_type operator*(value_type a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_mul_pd(_mm_set1_pd(a), b.vec_);
        return result;
    }

    /// Divide scalar by vector
    [[nodiscard]] friend simd_type operator/(value_type a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_div_pd(_mm_set1_pd(a), b.vec_);
        return result;
    }

    // Mathematical Functions

    /// Element-wise square root using SSE2
    [[nodiscard]] friend simd_type sqrt(simd_type const& a) noexcept
    {
        simd_type result;
        result.vec_ = _mm_sqrt_pd(a.vec_);
        return result;
    }

    /// Element-wise absolute value using SSE2 bit masking
    [[nodiscard]] friend simd_type abs(simd_type const& a) noexcept
    {
        simd_type result;
        // Clear the sign bit by ANDing with 0x7FFFFFFFFFFFFFFF
        __m128d sign_mask = _mm_castsi128_pd(_mm_set1_epi64x(0x7FFFFFFFFFFFFFFF));
        result.vec_ = _mm_and_pd(a.vec_, sign_mask);
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

    /// Element-wise reciprocal using SSE2 division
    [[nodiscard]] friend simd_type reciprocal(simd_type const& a) noexcept
    {
        simd_type result;
        result.vec_ = _mm_div_pd(_mm_set1_pd(1.0), a.vec_);
        return result;
    }

    /// Element-wise reciprocal square root using SSE2 sqrt and division
    [[nodiscard]] friend simd_type reciprocal_sqrt(simd_type const& a) noexcept
    {
        simd_type result;
        result.vec_ = _mm_div_pd(_mm_set1_pd(1.0), _mm_sqrt_pd(a.vec_));
        return result;
    }

    // Comparison Functions

    /// Element-wise equality comparison (returns 1.0 or 0.0)
    [[nodiscard]] friend simd_type equal_to(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        __m128d cmp = _mm_cmpeq_pd(a.vec_, b.vec_);
        result.vec_ = _mm_and_pd(cmp, _mm_set1_pd(1.0));
        return result;
    }

    /// Element-wise inequality comparison (returns 1.0 or 0.0)
    [[nodiscard]] friend simd_type not_equal_to(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        __m128d cmp = _mm_cmpneq_pd(a.vec_, b.vec_);
        result.vec_ = _mm_and_pd(cmp, _mm_set1_pd(1.0));
        return result;
    }

    /// Element-wise less-than comparison (returns 1.0 or 0.0)
    [[nodiscard]] friend simd_type less(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        __m128d cmp = _mm_cmplt_pd(a.vec_, b.vec_);
        result.vec_ = _mm_and_pd(cmp, _mm_set1_pd(1.0));
        return result;
    }

    /// Element-wise less-than-or-equal comparison (returns 1.0 or 0.0)
    [[nodiscard]] friend simd_type less_equal(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        __m128d cmp = _mm_cmple_pd(a.vec_, b.vec_);
        result.vec_ = _mm_and_pd(cmp, _mm_set1_pd(1.0));
        return result;
    }

    /// Element-wise greater-than comparison (returns 1.0 or 0.0)
    [[nodiscard]] friend simd_type greater(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        __m128d cmp = _mm_cmpgt_pd(a.vec_, b.vec_);
        result.vec_ = _mm_and_pd(cmp, _mm_set1_pd(1.0));
        return result;
    }

    /// Element-wise greater-than-or-equal comparison (returns 1.0 or 0.0)
    [[nodiscard]] friend simd_type greater_equal(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        __m128d cmp = _mm_cmpge_pd(a.vec_, b.vec_);
        result.vec_ = _mm_and_pd(cmp, _mm_set1_pd(1.0));
        return result;
    }

    // Min/Max/Clamp Functions

    /// Element-wise minimum using SSE2
    [[nodiscard]] friend simd_type min(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_min_pd(a.vec_, b.vec_);
        return result;
    }

    /// Element-wise maximum using SSE2
    [[nodiscard]] friend simd_type max(simd_type const& a, simd_type const& b) noexcept
    {
        simd_type result;
        result.vec_ = _mm_max_pd(a.vec_, b.vec_);
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
        result.vec_ = _mm_fmadd_pd(a.vec_, b.vec_, c.vec_);
#    else
        // Fallback to separate multiply and add
        result.vec_ = _mm_add_pd(_mm_mul_pd(a.vec_, b.vec_), c.vec_);
#    endif
        return result;
    }

    /// Negative multiply-subtract: c - a * b
    [[nodiscard]] friend simd_type nmsub(simd_type const& a, simd_type const& b, simd_type const& c) noexcept
    {
        simd_type result;
#    if defined(__FMA__)
        // Use hardware FMA if available: c - a*b
        result.vec_ = _mm_fnmadd_pd(a.vec_, b.vec_, c.vec_);
#    else
        // Fallback to separate multiply and subtract
        result.vec_ = _mm_sub_pd(c.vec_, _mm_mul_pd(a.vec_, b.vec_));
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

/// Splat a value into an SSE2 vector (for compatibility with generic code)
inline SIMDVec<double, 2>& splat(SIMDVec<double, 2>& v, double a) noexcept
{
    v.vec_ = _mm_set1_pd(a);
    return v;
}

/// Zero an SSE2 vector (for compatibility with generic code)
inline void zero(SIMDVec<double, 2>& v) noexcept
{
    v.vec_ = _mm_setzero_pd();
}

/// Set an SSE2 vector to all ones (for compatibility with generic code)
inline SIMDVec<double, 2>& one(SIMDVec<double, 2>& v) noexcept
{
    v.vec_ = _mm_set1_pd(1.0);
    return v;
}

namespace constants {

template <>
[[nodiscard]] constexpr simd_float64x2 zero<simd_float64x2>() noexcept
{
    return simd_float64x2::zero();
}

template <>
[[nodiscard]] constexpr simd_float64x2 one<simd_float64x2>() noexcept
{
    return simd_float64x2::one();
}

}  // namespace constants

}  // namespace statusbar::dsp

#endif  // #if defined(__AVX__)
