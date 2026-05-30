#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#    include "dsp_constants.hpp"
#    include "dsp_vec_base.hpp"

#    include <arm_neon.h>

#    include <cmath>
#    include <cstddef>
#    include <initializer_list>
#    include <stdexcept>
#    include <type_traits>
#    include <utility>

namespace statusbar::dsp {

/// ARM NEON specialization of SIMDVec for float64x2
///
/// This template specialization provides optimized SIMD operations
/// using ARM NEON intrinsics for 2-element double vectors.
template <>
class alignas(simd_alignment<double, 2>()) SIMDVec<double, 2>
{
  public:
    using simd_type = SIMDVec<double, 2>;
    using internal_type = float64x2_t;
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
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init) - intentionally uninitialized for performance
    SIMDVec() noexcept = default;

    /// Broadcast constructor - fills all elements with the same value
    explicit SIMDVec(value_type v) noexcept { vec_ = vdupq_n_f64(v); }

    /// Construct from two scalar values
    constexpr SIMDVec(value_type v0, value_type v1) noexcept
        : item_{v0, v1}
    {}

    /// Initializer list constructor with zero-fill for missing elements
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init) - members filled in loop body
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
    constexpr auto operator=(simd_type const& other) noexcept -> simd_type& = default;

    /// Move constructor
    constexpr SIMDVec(simd_type&& other) noexcept = default;

    /// Move assignment operator
    constexpr auto operator=(simd_type&& other) noexcept -> simd_type& = default;

    /// Destructor
    ~SIMDVec() = default;

    /// Get the vector size
    [[nodiscard]] static constexpr auto size() noexcept -> size_type { return vector_size; }

    /// Get the vector maximum size
    [[nodiscard]] static constexpr auto max_size() noexcept -> size_type { return vector_size; }

    /// Vector is never empty
    [[nodiscard]] static constexpr auto empty() noexcept -> bool { return false; }

    /// Create a zero-initialized vector
    [[nodiscard]] static auto zero() noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vdupq_n_f64(0.0);
        return result;
    }

    /// Create a vector with all elements set to one
    [[nodiscard]] static auto one() noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vdupq_n_f64(1.0);
        return result;
    }

    /// Create a vector with all elements set to the given value
    [[nodiscard]] static auto splat(value_type v) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vdupq_n_f64(v);
        return result;
    }

    /// Fill all elements with a specific value
    void fill(value_type v) noexcept { vec_ = vdupq_n_f64(v); }

    /// Swap values with another vector
    void swap(simd_type& other) noexcept
    {
        internal_type const temp = vec_;
        vec_ = other.vec_;
        other.vec_ = temp;
    }

    /// Get pointer to underlying array
    [[nodiscard]] constexpr auto data() noexcept -> pointer { return item_; }

    /// Get const pointer to underlying array
    [[nodiscard]] constexpr auto data() const noexcept -> const_pointer { return item_; }

    /// Array subscript operator (const)
    [[nodiscard]] constexpr auto operator[](size_type index) const noexcept -> const_reference { return item_[index]; }

    /// Array subscript operator (non-const)
    [[nodiscard]] constexpr auto operator[](size_type index) noexcept -> reference { return item_[index]; }

    /// Bounds-checked element access (const)
    [[nodiscard]] auto at(size_type index) const -> const_reference
    {
        if (index >= vector_size) {
            detail::out_of_range("SIMDVec index out of range");
        }
        return item_[index];
    }

    /// Bounds-checked element access (non-const)
    [[nodiscard]] auto at(size_type index) -> reference
    {
        if (index >= vector_size) {
            detail::out_of_range("SIMDVec index out of range");
        }
        return item_[index];
    }

    /// Get the first element
    [[nodiscard]] constexpr auto front() noexcept -> reference { return item_[0]; }

    /// Get the first element (const)
    [[nodiscard]] constexpr auto front() const noexcept -> const_reference { return item_[0]; }

    /// Get the last element
    [[nodiscard]] constexpr auto back() noexcept -> reference { return item_[vector_size - 1]; }

    /// Get the last element (const)
    [[nodiscard]] constexpr auto back() const noexcept -> const_reference { return item_[vector_size - 1]; }

    /// Iterator to beginning
    [[nodiscard]] constexpr auto begin() noexcept -> iterator { return item_; }

    /// Const iterator to beginning
    [[nodiscard]] constexpr auto begin() const noexcept -> const_iterator { return item_; }

    /// Const iterator to beginning
    [[nodiscard]] constexpr auto cbegin() const noexcept -> const_iterator { return item_; }

    /// Iterator to end
    [[nodiscard]] constexpr auto end() noexcept -> iterator { return item_ + vector_size; }

    /// Const iterator to end
    [[nodiscard]] constexpr auto end() const noexcept -> const_iterator { return item_ + vector_size; }

    /// Const iterator to end
    [[nodiscard]] constexpr auto cend() const noexcept -> const_iterator { return item_ + vector_size; }

    // Horizontal Reduction Operations

    /// Horizontal sum of all lanes using NEON pairwise addition
    [[nodiscard]] auto hsum() const noexcept -> value_type { return vaddvq_f64(vec_); }

    /// Horizontal product of all lanes
    [[nodiscard]] constexpr auto hprod() const noexcept -> value_type { return item_[0] * item_[1]; }

    /// Horizontal minimum of all lanes
    [[nodiscard]] auto hmin() const noexcept -> value_type { return vminvq_f64(vec_); }

    /// Horizontal maximum of all lanes
    [[nodiscard]] auto hmax() const noexcept -> value_type { return vmaxvq_f64(vec_); }

    // Compound Assignment Operators(SIMD) ====================

    /// Add another vector to this one
    auto operator+=(simd_type const& other) noexcept -> simd_type&
    {
        vec_ = vaddq_f64(vec_, other.vec_);
        return *this;
    }

    /// Subtract another vector from this one
    auto operator-=(simd_type const& other) noexcept -> simd_type&
    {
        vec_ = vsubq_f64(vec_, other.vec_);
        return *this;
    }

    /// Multiply this vector by another
    auto operator*=(simd_type const& other) noexcept -> simd_type&
    {
        vec_ = vmulq_f64(vec_, other.vec_);
        return *this;
    }

    /// Divide this vector by another
    auto operator/=(simd_type const& other) noexcept -> simd_type&
    {
        vec_ = vdivq_f64(vec_, other.vec_);
        return *this;
    }

    // Compound Assignment Operators(Scalar) ====================

    /// Add a scalar to all elements
    auto operator+=(value_type scalar) noexcept -> simd_type&
    {
        vec_ = vaddq_f64(vec_, vdupq_n_f64(scalar));
        return *this;
    }

    /// Subtract a scalar from all elements
    auto operator-=(value_type scalar) noexcept -> simd_type&
    {
        vec_ = vsubq_f64(vec_, vdupq_n_f64(scalar));
        return *this;
    }

    /// Multiply all elements by a scalar
    auto operator*=(value_type scalar) noexcept -> simd_type&
    {
        vec_ = vmulq_f64(vec_, vdupq_n_f64(scalar));
        return *this;
    }

    /// Divide all elements by a scalar
    auto operator/=(value_type scalar) noexcept -> simd_type&
    {
        vec_ = vdivq_f64(vec_, vdupq_n_f64(scalar));
        return *this;
    }

    // Unary Operators

    /// Unary negation
    [[nodiscard]] friend auto operator-(simd_type const& a) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vnegq_f64(a.vec_);
        return result;
    }

    /// Unary plus (identity)
    [[nodiscard]] friend auto operator+(simd_type const& a) noexcept -> simd_type { return a; }

    // Binary Operators(SIMD) ====================

    /// Add two vectors
    [[nodiscard]] friend auto operator+(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vaddq_f64(a.vec_, b.vec_);
        return result;
    }

    /// Subtract two vectors
    [[nodiscard]] friend auto operator-(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vsubq_f64(a.vec_, b.vec_);
        return result;
    }

    /// Multiply two vectors
    [[nodiscard]] friend auto operator*(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vmulq_f64(a.vec_, b.vec_);
        return result;
    }

    /// Divide two vectors
    [[nodiscard]] friend auto operator/(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vdivq_f64(a.vec_, b.vec_);
        return result;
    }

    // Binary Operators(Vector + Scalar) ====================

    /// Add scalar to vector
    [[nodiscard]] friend auto operator+(simd_type const& a, value_type b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vaddq_f64(a.vec_, vdupq_n_f64(b));
        return result;
    }

    /// Subtract scalar from vector
    [[nodiscard]] friend auto operator-(simd_type const& a, value_type b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vsubq_f64(a.vec_, vdupq_n_f64(b));
        return result;
    }

    /// Multiply vector by scalar
    [[nodiscard]] friend auto operator*(simd_type const& a, value_type b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vmulq_f64(a.vec_, vdupq_n_f64(b));
        return result;
    }

    /// Divide vector by scalar
    [[nodiscard]] friend auto operator/(simd_type const& a, value_type b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vdivq_f64(a.vec_, vdupq_n_f64(b));
        return result;
    }

    // Binary Operators(Scalar + Vector) ====================

    /// Add vector to scalar
    [[nodiscard]] friend auto operator+(value_type a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vaddq_f64(vdupq_n_f64(a), b.vec_);
        return result;
    }

    /// Subtract vector from scalar
    [[nodiscard]] friend auto operator-(value_type a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vsubq_f64(vdupq_n_f64(a), b.vec_);
        return result;
    }

    /// Multiply scalar by vector
    [[nodiscard]] friend auto operator*(value_type a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vmulq_f64(vdupq_n_f64(a), b.vec_);
        return result;
    }

    /// Divide scalar by vector
    [[nodiscard]] friend auto operator/(value_type a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vdivq_f64(vdupq_n_f64(a), b.vec_);
        return result;
    }

    // Mathematical Functions

    /// Element-wise square root using ARM64 native sqrt
    [[nodiscard]] friend auto sqrt(simd_type const& a) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vsqrtq_f64(a.vec_);
        return result;
    }

    /// Element-wise absolute value using NEON intrinsic
    [[nodiscard]] friend auto abs(simd_type const& a) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vabsq_f64(a.vec_);
        return result;
    }

    /// Element-wise argument (phase angle) - falls back to scalar
    [[nodiscard]] friend auto arg(simd_type const& a) noexcept -> simd_type
    {
        simd_type result;
        for (size_type i = 0; i < vector_size; ++i) {
            result.item_[i] = std::arg(a.item_[i]);
        }
        return result;
    }

    /// Element-wise sine - falls back to scalar
    [[nodiscard]] friend auto sin(simd_type const& a) noexcept -> simd_type
    {
        simd_type result;
        for (size_type i = 0; i < vector_size; ++i) {
            result.item_[i] = std::sin(a.item_[i]);
        }
        return result;
    }

    /// Element-wise cosine - falls back to scalar
    [[nodiscard]] friend auto cos(simd_type const& a) noexcept -> simd_type
    {
        simd_type result;
        for (size_type i = 0; i < vector_size; ++i) {
            result.item_[i] = std::cos(a.item_[i]);
        }
        return result;
    }

    /// Element-wise exponential - falls back to scalar
    [[nodiscard]] friend auto exp(simd_type const& a) noexcept -> simd_type
    {
        simd_type result;
        for (size_type i = 0; i < vector_size; ++i) {
            result.item_[i] = std::exp(a.item_[i]);
        }
        return result;
    }

    /// Element-wise natural logarithm - falls back to scalar
    [[nodiscard]] friend auto log(simd_type const& a) noexcept -> simd_type
    {
        simd_type result;
        for (size_type i = 0; i < vector_size; ++i) {
            result.item_[i] = std::log(a.item_[i]);
        }
        return result;
    }

    /// Element-wise reciprocal using ARM64 native division
    [[nodiscard]] friend auto reciprocal(simd_type const& a) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vdivq_f64(vdupq_n_f64(1.0), a.vec_);
        return result;
    }

    /// Element-wise reciprocal square root using ARM64 native sqrt and division
    [[nodiscard]] friend auto reciprocal_sqrt(simd_type const& a) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vdivq_f64(vdupq_n_f64(1.0), vsqrtq_f64(a.vec_));
        return result;
    }

    // Comparison Functions

    /// Element-wise equality comparison (returns 1.0 or 0.0)
    [[nodiscard]] friend auto equal_to(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        uint64x2_t const cmp = vceqq_f64(a.vec_, b.vec_);
        result.vec_ = vreinterpretq_f64_u64(vandq_u64(cmp, vreinterpretq_u64_f64(vdupq_n_f64(1.0))));
        return result;
    }

    /// Element-wise inequality comparison (returns 1.0 or 0.0)
    [[nodiscard]] friend auto not_equal_to(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        uint64x2_t const cmp = vceqq_f64(a.vec_, b.vec_);
        // ARM64 doesn't have vmvnq_u64, use XOR with all-ones
        uint64x2_t const all_ones = vdupq_n_u64(~0ULL);
        result.vec_ = vreinterpretq_f64_u64(vandq_u64(veorq_u64(cmp, all_ones), vreinterpretq_u64_f64(vdupq_n_f64(1.0))));
        return result;
    }

    /// Element-wise less-than comparison (returns 1.0 or 0.0)
    [[nodiscard]] friend auto less(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        uint64x2_t const cmp = vcltq_f64(a.vec_, b.vec_);
        result.vec_ = vreinterpretq_f64_u64(vandq_u64(cmp, vreinterpretq_u64_f64(vdupq_n_f64(1.0))));
        return result;
    }

    /// Element-wise less-than-or-equal comparison (returns 1.0 or 0.0)
    [[nodiscard]] friend auto less_equal(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        uint64x2_t const cmp = vcleq_f64(a.vec_, b.vec_);
        result.vec_ = vreinterpretq_f64_u64(vandq_u64(cmp, vreinterpretq_u64_f64(vdupq_n_f64(1.0))));
        return result;
    }

    /// Element-wise greater-than comparison (returns 1.0 or 0.0)
    [[nodiscard]] friend auto greater(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        uint64x2_t const cmp = vcgtq_f64(a.vec_, b.vec_);
        result.vec_ = vreinterpretq_f64_u64(vandq_u64(cmp, vreinterpretq_u64_f64(vdupq_n_f64(1.0))));
        return result;
    }

    /// Element-wise greater-than-or-equal comparison (returns 1.0 or 0.0)
    [[nodiscard]] friend auto greater_equal(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        uint64x2_t const cmp = vcgeq_f64(a.vec_, b.vec_);
        result.vec_ = vreinterpretq_f64_u64(vandq_u64(cmp, vreinterpretq_u64_f64(vdupq_n_f64(1.0))));
        return result;
    }

    // Min/Max/Clamp Functions

    /// Element-wise minimum using NEON vminq
    [[nodiscard]] friend auto min(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vminq_f64(a.vec_, b.vec_);
        return result;
    }

    /// Element-wise maximum using NEON vmaxq
    [[nodiscard]] friend auto max(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vmaxq_f64(a.vec_, b.vec_);
        return result;
    }

    /// Clamp values to range [lo, hi] element-wise
    [[nodiscard]] friend auto clamp(simd_type const& v, simd_type const& lo, simd_type const& hi) noexcept -> simd_type
    {
        return min(max(v, lo), hi);
    }

    // FMA and Lerp Functions

    /// Fused multiply-add: a * b + c using NEON vfmaq
    [[nodiscard]] friend auto madd(simd_type const& a, simd_type const& b, simd_type const& c) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vfmaq_f64(c.vec_, a.vec_, b.vec_);  // vfmaq computes c + a*b
        return result;
    }

    /// Negative multiply-subtract: c - a * b using NEON vfmsq (AltiVec vec_nmsub equivalent)
    [[nodiscard]] friend auto nmsub(simd_type const& a, simd_type const& b, simd_type const& c) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vfmsq_f64(c.vec_, a.vec_, b.vec_);  // vfmsq computes c - a*b
        return result;
    }

    /// Linear interpolation: a + t * (b - a)
    [[nodiscard]] friend auto lerp(simd_type const& a, simd_type const& b, simd_type const& t) noexcept -> simd_type
    {
        return madd(t, b - a, a);
    }

    // Dot Product

    /// Dot product of two vectors
    [[nodiscard]] friend auto dot(simd_type const& a, simd_type const& b) noexcept -> value_type
    {
        simd_type const prod = a * b;
        return prod.hsum();
    }
};

// Free Function Overloads for ADL

/// Splat a value into a NEON vector (for compatibility with generic code)
inline auto splat(SIMDVec<double, 2>& v, double a) noexcept -> SIMDVec<double, 2>&
{
    v.vec_ = vdupq_n_f64(a);
    return v;
}

/// Zero a NEON vector (for compatibility with generic code)
inline void zero(SIMDVec<double, 2>& v) noexcept
{
    v.vec_ = vdupq_n_f64(0.0);
}

/// Set a NEON vector to all ones (for compatibility with generic code)
inline auto one(SIMDVec<double, 2>& v) noexcept -> SIMDVec<double, 2>&
{
    v.vec_ = vdupq_n_f64(1.0);
    return v;
}

namespace constants {

template <>
[[nodiscard]] constexpr auto zero<simd_float64x2>() noexcept -> simd_float64x2
{
    return simd_float64x2::zero();
}

template <>
[[nodiscard]] constexpr auto one<simd_float64x2>() noexcept -> simd_float64x2
{
    return simd_float64x2::one();
}

}  // namespace constants

}  // namespace statusbar::dsp

#endif  // #if defined(__ARM_NEON) || defined(__ARM_NEON__)
