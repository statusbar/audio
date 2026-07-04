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

/// ARM NEON specialization of SIMDVec for float32x4
///
/// This template specialization provides optimized SIMD operations
/// using ARM NEON intrinsics for 4-element float vectors.
template <>
class alignas(simd_alignment<float, 4>()) SIMDVec<float, 4> : public SIMDVecContainer<float, 4>
{
  public:
    using simd_type = SIMDVec<float, 4>;
    using internal_type = float32x4_t;
    // value_type / size_type / vector_size and the container/iterator interface
    // come from SIMDVecContainer.

    union
    {
        internal_type vec_;
        value_type item_[vector_size];
    };

    /// Default constructor - does not initialize values for performance
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init) - intentionally uninitialized for performance
    SIMDVec() noexcept = default;

    /// Broadcast constructor - fills all elements with the same value
    explicit SIMDVec(value_type v) noexcept { vec_ = vdupq_n_f32(v); }

    /// Construct from four scalar values
    constexpr SIMDVec(value_type v0, value_type v1, value_type v2, value_type v3) noexcept
        : item_{v0, v1, v2, v3}
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

    /// Create a zero-initialized vector
    [[nodiscard]] static auto zero() noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vdupq_n_f32(0.0F);
        return result;
    }

    /// Create a vector with all elements set to one
    [[nodiscard]] static auto one() noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vdupq_n_f32(1.0F);
        return result;
    }

    /// Create a vector with all elements set to the given value
    [[nodiscard]] static auto splat(value_type v) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vdupq_n_f32(v);
        return result;
    }

    /// Fill all elements with a specific value
    void fill(value_type v) noexcept { vec_ = vdupq_n_f32(v); }

    /// Swap values with another vector
    void swap(simd_type& other) noexcept
    {
        internal_type const temp = vec_;
        vec_ = other.vec_;
        other.vec_ = temp;
    }

    // Horizontal Reduction Operations

    /// Horizontal sum of all lanes using NEON pairwise addition
    [[nodiscard]] auto hsum() const noexcept -> value_type
    {
        float32x2_t sum_pairs = vadd_f32(vget_low_f32(vec_), vget_high_f32(vec_));
        sum_pairs = vpadd_f32(sum_pairs, sum_pairs);
        return vget_lane_f32(sum_pairs, 0);
    }

    /// Horizontal product of all lanes
    [[nodiscard]] constexpr auto hprod() const noexcept -> value_type { return item_[0] * item_[1] * item_[2] * item_[3]; }

    /// Horizontal minimum of all lanes using NEON pairwise min
    [[nodiscard]] auto hmin() const noexcept -> value_type
    {
        float32x2_t min_pairs = vpmin_f32(vget_low_f32(vec_), vget_high_f32(vec_));
        min_pairs = vpmin_f32(min_pairs, min_pairs);
        return vget_lane_f32(min_pairs, 0);
    }

    /// Horizontal maximum of all lanes using NEON pairwise max
    [[nodiscard]] auto hmax() const noexcept -> value_type
    {
        float32x2_t max_pairs = vpmax_f32(vget_low_f32(vec_), vget_high_f32(vec_));
        max_pairs = vpmax_f32(max_pairs, max_pairs);
        return vget_lane_f32(max_pairs, 0);
    }

    // Compound Assignment Operators(SIMD) ====================

    /// Add another vector to this one
    auto operator+=(simd_type const& other) noexcept -> simd_type&
    {
        vec_ = vaddq_f32(vec_, other.vec_);
        return *this;
    }

    /// Subtract another vector from this one
    auto operator-=(simd_type const& other) noexcept -> simd_type&
    {
        vec_ = vsubq_f32(vec_, other.vec_);
        return *this;
    }

    /// Multiply this vector by another
    auto operator*=(simd_type const& other) noexcept -> simd_type&
    {
        vec_ = vmulq_f32(vec_, other.vec_);
        return *this;
    }

    /// Divide this vector by another
    auto operator/=(simd_type const& other) noexcept -> simd_type&
    {
        // ARM64 has native SIMD division
        vec_ = vdivq_f32(vec_, other.vec_);
        return *this;
    }

    // Compound Assignment Operators(Scalar) ====================

    /// Add a scalar to all elements
    auto operator+=(value_type scalar) noexcept -> simd_type&
    {
        vec_ = vaddq_f32(vec_, vdupq_n_f32(scalar));
        return *this;
    }

    /// Subtract a scalar from all elements
    auto operator-=(value_type scalar) noexcept -> simd_type&
    {
        vec_ = vsubq_f32(vec_, vdupq_n_f32(scalar));
        return *this;
    }

    /// Multiply all elements by a scalar
    auto operator*=(value_type scalar) noexcept -> simd_type&
    {
        vec_ = vmulq_f32(vec_, vdupq_n_f32(scalar));
        return *this;
    }

    /// Divide all elements by a scalar
    auto operator/=(value_type scalar) noexcept -> simd_type&
    {
        vec_ = vmulq_f32(vec_, vdupq_n_f32(1.0F / scalar));
        return *this;
    }

    // Unary Operators

    /// Unary negation
    [[nodiscard]] friend auto operator-(simd_type const& a) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vnegq_f32(a.vec_);
        return result;
    }

    /// Unary plus (identity)
    [[nodiscard]] friend auto operator+(simd_type const& a) noexcept -> simd_type { return a; }

    // Binary Operators(SIMD) ====================

    /// Add two vectors
    [[nodiscard]] friend auto operator+(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vaddq_f32(a.vec_, b.vec_);
        return result;
    }

    /// Subtract two vectors
    [[nodiscard]] friend auto operator-(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vsubq_f32(a.vec_, b.vec_);
        return result;
    }

    /// Multiply two vectors
    [[nodiscard]] friend auto operator*(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vmulq_f32(a.vec_, b.vec_);
        return result;
    }

    /// Divide two vectors
    [[nodiscard]] friend auto operator/(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vdivq_f32(a.vec_, b.vec_);
        return result;
    }

    // Binary Operators(Vector + Scalar) ====================

    /// Add scalar to vector
    [[nodiscard]] friend auto operator+(simd_type const& a, value_type b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vaddq_f32(a.vec_, vdupq_n_f32(b));
        return result;
    }

    /// Subtract scalar from vector
    [[nodiscard]] friend auto operator-(simd_type const& a, value_type b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vsubq_f32(a.vec_, vdupq_n_f32(b));
        return result;
    }

    /// Multiply vector by scalar
    [[nodiscard]] friend auto operator*(simd_type const& a, value_type b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vmulq_f32(a.vec_, vdupq_n_f32(b));
        return result;
    }

    /// Divide vector by scalar
    [[nodiscard]] friend auto operator/(simd_type const& a, value_type b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vmulq_f32(a.vec_, vdupq_n_f32(1.0F / b));
        return result;
    }

    // Binary Operators(Scalar + Vector) ====================

    /// Add vector to scalar
    [[nodiscard]] friend auto operator+(value_type a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vaddq_f32(vdupq_n_f32(a), b.vec_);
        return result;
    }

    /// Subtract vector from scalar
    [[nodiscard]] friend auto operator-(value_type a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vsubq_f32(vdupq_n_f32(a), b.vec_);
        return result;
    }

    /// Multiply scalar by vector
    [[nodiscard]] friend auto operator*(value_type a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vmulq_f32(vdupq_n_f32(a), b.vec_);
        return result;
    }

    /// Divide scalar by vector
    [[nodiscard]] friend auto operator/(value_type a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vdivq_f32(vdupq_n_f32(a), b.vec_);
        return result;
    }

    // Mathematical Functions

    /// Element-wise square root using ARM64 native sqrt
    [[nodiscard]] friend auto sqrt(simd_type const& a) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vsqrtq_f32(a.vec_);
        return result;
    }

    /// Element-wise absolute value using NEON intrinsic
    [[nodiscard]] friend auto abs(simd_type const& a) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vabsq_f32(a.vec_);
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
        result.vec_ = vdivq_f32(vdupq_n_f32(1.0F), a.vec_);
        return result;
    }

    /// Element-wise reciprocal square root using ARM64 native sqrt and division
    [[nodiscard]] friend auto reciprocal_sqrt(simd_type const& a) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vdivq_f32(vdupq_n_f32(1.0F), vsqrtq_f32(a.vec_));
        return result;
    }

    // Comparison Functions

    /// Element-wise equality comparison (returns 1.0F or 0.0F)
    [[nodiscard]] friend auto equal_to(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        uint32x4_t const cmp = vceqq_f32(a.vec_, b.vec_);
        result.vec_ = vreinterpretq_f32_u32(vandq_u32(cmp, vreinterpretq_u32_f32(vdupq_n_f32(1.0F))));
        return result;
    }

    /// Element-wise inequality comparison (returns 1.0F or 0.0F)
    [[nodiscard]] friend auto not_equal_to(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        uint32x4_t const cmp = vceqq_f32(a.vec_, b.vec_);
        result.vec_ = vreinterpretq_f32_u32(vandq_u32(vmvnq_u32(cmp), vreinterpretq_u32_f32(vdupq_n_f32(1.0F))));
        return result;
    }

    /// Element-wise less-than comparison (returns 1.0F or 0.0F)
    [[nodiscard]] friend auto less(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        uint32x4_t const cmp = vcltq_f32(a.vec_, b.vec_);
        result.vec_ = vreinterpretq_f32_u32(vandq_u32(cmp, vreinterpretq_u32_f32(vdupq_n_f32(1.0F))));
        return result;
    }

    /// Element-wise less-than-or-equal comparison (returns 1.0F or 0.0F)
    [[nodiscard]] friend auto less_equal(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        uint32x4_t const cmp = vcleq_f32(a.vec_, b.vec_);
        result.vec_ = vreinterpretq_f32_u32(vandq_u32(cmp, vreinterpretq_u32_f32(vdupq_n_f32(1.0F))));
        return result;
    }

    /// Element-wise greater-than comparison (returns 1.0F or 0.0F)
    [[nodiscard]] friend auto greater(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        uint32x4_t const cmp = vcgtq_f32(a.vec_, b.vec_);
        result.vec_ = vreinterpretq_f32_u32(vandq_u32(cmp, vreinterpretq_u32_f32(vdupq_n_f32(1.0F))));
        return result;
    }

    /// Element-wise greater-than-or-equal comparison (returns 1.0F or 0.0F)
    [[nodiscard]] friend auto greater_equal(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        uint32x4_t const cmp = vcgeq_f32(a.vec_, b.vec_);
        result.vec_ = vreinterpretq_f32_u32(vandq_u32(cmp, vreinterpretq_u32_f32(vdupq_n_f32(1.0F))));
        return result;
    }

    // Min/Max/Clamp Functions

    /// Element-wise minimum using NEON vminq
    [[nodiscard]] friend auto min(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vminq_f32(a.vec_, b.vec_);
        return result;
    }

    /// Element-wise maximum using NEON vmaxq
    [[nodiscard]] friend auto max(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vmaxq_f32(a.vec_, b.vec_);
        return result;
    }

    /// Clamp values to range [lo, hi] element-wise
    [[nodiscard]] friend auto clamp(simd_type const& v, simd_type const& lo, simd_type const& hi) noexcept -> simd_type
    {
        return min(max(v, lo), hi);
    }

    // FMA and Lerp Functions

    /// Fused multiply-add: a * b + c using NEON vmlaq
    [[nodiscard]] friend auto madd(simd_type const& a, simd_type const& b, simd_type const& c) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vmlaq_f32(c.vec_, a.vec_, b.vec_);  // vmlaq computes c + a*b
        return result;
    }

    /// Negative multiply-subtract: c - a * b using NEON vmlsq (AltiVec vec_nmsub equivalent)
    [[nodiscard]] friend auto nmsub(simd_type const& a, simd_type const& b, simd_type const& c) noexcept -> simd_type
    {
        simd_type result;
        result.vec_ = vmlsq_f32(c.vec_, a.vec_, b.vec_);  // vmlsq computes c - a*b
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
inline auto splat(SIMDVec<float, 4>& v, float a) noexcept -> SIMDVec<float, 4>&
{
    v.vec_ = vdupq_n_f32(a);
    return v;
}

/// Zero a NEON vector (for compatibility with generic code)
inline void zero(SIMDVec<float, 4>& v) noexcept
{
    v.vec_ = vdupq_n_f32(0.0F);
}

/// Set a NEON vector to all ones (for compatibility with generic code)
inline auto one(SIMDVec<float, 4>& v) noexcept -> SIMDVec<float, 4>&
{
    v.vec_ = vdupq_n_f32(1.0F);
    return v;
}

namespace constants {

template <>
[[nodiscard]] constexpr auto zero<simd_float32x4>() noexcept -> simd_float32x4
{
    return simd_float32x4::zero();
}

template <>
[[nodiscard]] constexpr auto one<simd_float32x4>() noexcept -> simd_float32x4
{
    return simd_float32x4::one();
}

}  // namespace constants

}  // namespace statusbar::dsp

#endif  // #if defined(__ARM_NEON) || defined(__ARM_NEON__)
