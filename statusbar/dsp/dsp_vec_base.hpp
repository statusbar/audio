#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/dsp/dsp_fatal.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace statusbar::dsp {

/// Compute optimal alignment for SIMD vector of N elements of type T
///
/// Determines the appropriate memory alignment based on vector size:
/// - 32-byte alignment for AVX-sized vectors (256-bit)
/// - 16-byte alignment minimum, even for sub-128-bit SIMDVecs — guards
///   against future platform specializations using SIMD intrinsics on
///   small vectors (e.g. NEON 64-bit registers on SIMDVec<float, 2>)
///   that would fault or perform poorly on naturally-aligned (4 or 8
///   byte) storage. Costs at most 12 bytes of padding per instance for
///   the smallest SIMDVecs, which are rare in practice since the whole
///   point of SIMD is wider lanes.
///
/// @tparam T Element type (e.g., float, double)
/// @tparam N Number of elements in the vector
/// @return Alignment in bytes suitable for SIMD operations
template <typename T, std::size_t N>
consteval auto simd_alignment() noexcept -> std::size_t
{
    constexpr std::size_t total_size = N * sizeof(T);
    if constexpr (total_size >= 32) {
        return 32;  // AVX alignment
    } else {
        // SSE/NEON-or-larger alignment, with 16-byte floor for safety
        // even when sizeof(T) is smaller (alignof(float) = 4).
        return std::max(alignof(T), std::size_t{16});
    }
}

/// SIMD-ready vector template for DSP operations
///
/// A fixed-size vector class optimized for SIMD operations. This is the scalar
/// fallback implementation that can be specialized for actual SIMD architectures
/// (SSE, AVX, NEON). The class provides:
///
/// - Standard container interface (size, iterators, element access)
/// - Arithmetic operators (element-wise and scalar)
/// - Mathematical functions (sqrt, sin, cos, exp, log, etc.)
/// - Reduction operations (horizontal sum, product, min, max)
/// - SIMD-style operations (splat, madd, lerp, clamp)
///
/// Memory is aligned for optimal SIMD performance based on vector size.
///
/// @tparam T Element type (typically float or double)
/// @tparam N Number of elements (typically 2, 4, or 8)
///
/// Example usage:
/// @code
/// simd_float32x4 a{1.0F, 2.0F, 3.0F, 4.0F};
/// simd_float32x4 b = simd_float32x4::splat(2.0F);
/// simd_float32x4 c = a * b;  // {2.0F, 4.0F, 6.0F, 8.0F}
/// float sum = c.hsum();      // 20.0f
/// @endcode
/// Arch-independent container / iterator interface shared by every SIMDVec
/// intrinsic specialization. A specialization supplies the
/// `union { internal_type vec_; value_type item_[N]; }` and the intrinsic ops;
/// everything here operates only on the scalar `item_[]` view, so this boilerplate
/// lives once instead of being copy-pasted into each arch header. Uses C++23
/// explicit object parameters ("deducing this") so the accessors reach the
/// derived's `item_` directly — no CRTP type parameter, no static_cast.
template <typename ValueType, std::size_t N>
class SIMDVecContainer
{
  public:
    using value_type = ValueType;
    using pointer = value_type*;
    using const_pointer = value_type const*;
    using reference = value_type&;
    using const_reference = value_type const&;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    static constexpr size_type vector_size = N;

    [[nodiscard]] static constexpr size_type size() noexcept { return N; }
    [[nodiscard]] static constexpr size_type max_size() noexcept { return N; }
    [[nodiscard]] static constexpr bool empty() noexcept { return false; }

    [[nodiscard]] constexpr auto data(this auto&& self) noexcept { return self.item_; }

    [[nodiscard]] constexpr decltype(auto) operator[](this auto&& self, size_type index) noexcept { return self.item_[index]; }

    [[nodiscard]] constexpr decltype(auto) at(this auto&& self, size_type index)
    {
        if (index >= N) {
            detail::out_of_range("SIMDVec index out of range");
        }
        return self.item_[index];
    }

    [[nodiscard]] constexpr decltype(auto) front(this auto&& self) noexcept { return self.item_[0]; }
    [[nodiscard]] constexpr decltype(auto) back(this auto&& self) noexcept { return self.item_[N - 1]; }

    [[nodiscard]] constexpr auto begin(this auto&& self) noexcept { return self.item_; }
    [[nodiscard]] constexpr auto end(this auto&& self) noexcept { return self.item_ + N; }
    // cbegin/cend always yield a const pointer, regardless of object constness.
    [[nodiscard]] constexpr auto cbegin(this auto&& self) noexcept -> const_iterator { return self.item_; }
    [[nodiscard]] constexpr auto cend(this auto&& self) noexcept -> const_iterator { return self.item_ + N; }
};

template <typename T, std::size_t N>
class alignas(simd_alignment<T, N>()) SIMDVec
{
    // back() and similar would be UB for N == 0; rule out the
    // degenerate case at the type level. Every real DSP use has N >= 1.
    static_assert(N > 0, "SIMDVec requires N >= 1");

  public:
    using simd_type = SIMDVec<T, N>;
    using value_type = T;
    using pointer = value_type*;
    using const_pointer = value_type const*;
    using reference = value_type&;
    using const_reference = value_type const&;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    /// Number of elements in the vector
    static constexpr size_type vector_size = N;

    /// The items of the vector (aligned for SIMD)
    value_type item_[vector_size];

    /// Default constructor
    ///
    /// Leaves values uninitialized for performance. Use zero() or splat()
    /// for initialized vectors.
    constexpr SIMDVec() noexcept = default;

    /// Construct from initializer list
    ///
    /// Elements are copied from the list. If the list has fewer elements
    /// than the vector size, remaining elements are zero-filled.
    ///
    /// @param list Initializer list of values
    ///
    /// Example:
    /// @code
    /// simd_float32x4 v{1.0F, 2.0F, 3.0F, 4.0F};
    /// simd_float32x4 partial{1.0F, 2.0F};  // {1.0F, 2.0F, 0.0F, 0.0F}
    /// @endcode
    constexpr SIMDVec(std::initializer_list<value_type> list) noexcept
    {
        size_type n = 0;
        for (auto it = list.begin(); it != list.end() && n < vector_size; ++it) {
            item_[n++] = *it;
        }
        // Zero-fill remainder
        for (; n < vector_size; ++n) {
            item_[n] = value_type{};
        }
    }

    /// Broadcast constructor
    ///
    /// Fills all lanes with the same value. Equivalent to splat().
    ///
    /// @param value Value to broadcast to all lanes
    ///
    /// Example:
    /// @code
    /// simd_float32x4 v(3.14F);  // {3.14F, 3.14F, 3.14F, 3.14F}
    /// @endcode
    constexpr explicit SIMDVec(value_type value) noexcept
    {
        for (size_type i = 0; i < vector_size; ++i) {
            item_[i] = value;
        }
    }

    /// Copy constructor (trivially copyable)
    constexpr SIMDVec(SIMDVec const&) noexcept = default;

    /// Copy assignment (trivially copyable)
    constexpr auto operator=(SIMDVec const&) noexcept -> SIMDVec& = default;

    /// Move constructor (trivially movable)
    constexpr SIMDVec(SIMDVec&&) noexcept = default;

    /// Move assignment (trivially movable)
    constexpr auto operator=(SIMDVec&&) noexcept -> SIMDVec& = default;

    /// Destructor (trivially destructible)
    ~SIMDVec() = default;

    // Container interface

    /// Get the number of elements in the vector
    /// @return Number of elements (compile-time constant)
    [[nodiscard]] static constexpr auto size() noexcept -> size_type { return vector_size; }

    /// Get the maximum number of elements
    /// @return Maximum size (same as size() for fixed-size vectors)
    [[nodiscard]] static constexpr auto max_size() noexcept -> size_type { return vector_size; }

    /// Check if the vector is empty
    ///
    /// Always false — SIMDVec has fixed non-zero size known at compile
    /// time (enforced by the class-level `static_assert(N > 0)`). This
    /// differs from std::vector::empty() which is non-static; here
    /// emptiness is a property of the type, not the instance, so making
    /// it static lets callers query it without an object.
    ///
    /// @return Always false
    [[nodiscard]] static constexpr auto empty() noexcept -> bool { return false; }

    /// Fill all elements with a value
    /// @param value Value to assign to all elements
    constexpr void fill(value_type const& value) noexcept
    {
        for (size_type i = 0; i < vector_size; ++i) {
            item_[i] = value;
        }
    }

    /// Swap contents with another vector
    /// @param other Vector to swap with
    constexpr void swap(simd_type& other) noexcept
    {
        for (size_type i = 0; i < vector_size; ++i) {
            std::swap(item_[i], other.item_[i]);
        }
    }

    /// Get pointer to underlying data
    /// @return Pointer to first element (aligned)
    [[nodiscard]] constexpr auto data() noexcept -> pointer { return item_; }

    /// Get const pointer to underlying data
    /// @return Const pointer to first element (aligned)
    [[nodiscard]] constexpr auto data() const noexcept -> const_pointer { return item_; }

    /// Access element by index (no bounds checking)
    /// @param index Element index (0 to N-1)
    /// @return Reference to element
    [[nodiscard]] constexpr auto operator[](size_type index) noexcept -> reference { return item_[index]; }

    /// Access element by index (no bounds checking, const)
    /// @param index Element index (0 to N-1)
    /// @return Const reference to element
    [[nodiscard]] constexpr auto operator[](size_type index) const noexcept -> const_reference { return item_[index]; }

    /// Access element by index with bounds checking
    /// @param index Element index (0 to N-1)
    /// @return Reference to element
    /// @throws std::out_of_range if index >= vector_size
    [[nodiscard]] constexpr auto at(size_type index) -> reference
    {
        if (index >= vector_size) {
            detail::out_of_range("SIMDVec::at");
        }
        return item_[index];
    }

    /// Access element by index with bounds checking (const)
    /// @param index Element index (0 to N-1)
    /// @return Const reference to element
    /// @throws std::out_of_range if index >= vector_size
    [[nodiscard]] constexpr auto at(size_type index) const -> const_reference
    {
        if (index >= vector_size) {
            detail::out_of_range("SIMDVec::at");
        }
        return item_[index];
    }

    /// Get reference to first element
    /// @return Reference to element at index 0
    [[nodiscard]] constexpr auto front() noexcept -> reference { return item_[0]; }

    /// Get const reference to first element
    /// @return Const reference to element at index 0
    [[nodiscard]] constexpr auto front() const noexcept -> const_reference { return item_[0]; }

    /// Get reference to last element
    /// @return Reference to element at index N-1
    [[nodiscard]] constexpr auto back() noexcept -> reference { return item_[vector_size - 1]; }

    /// Get const reference to last element
    /// @return Const reference to element at index N-1
    [[nodiscard]] constexpr auto back() const noexcept -> const_reference { return item_[vector_size - 1]; }

    /// Get iterator to beginning
    /// @return Iterator to first element
    [[nodiscard]] constexpr auto begin() noexcept -> iterator { return item_; }

    /// Get const iterator to beginning
    /// @return Const iterator to first element
    [[nodiscard]] constexpr auto begin() const noexcept -> const_iterator { return item_; }

    /// Get const iterator to beginning
    /// @return Const iterator to first element
    [[nodiscard]] constexpr auto cbegin() const noexcept -> const_iterator { return item_; }

    /// Get iterator to end
    /// @return Iterator past the last element
    [[nodiscard]] constexpr auto end() noexcept -> iterator { return item_ + vector_size; }

    /// Get const iterator to end
    /// @return Const iterator past the last element
    [[nodiscard]] constexpr auto end() const noexcept -> const_iterator { return item_ + vector_size; }

    /// Get const iterator to end
    /// @return Const iterator past the last element
    [[nodiscard]] constexpr auto cend() const noexcept -> const_iterator { return item_ + vector_size; }

    // SIMD-style operations

    /// Create a vector with all lanes set to the same value
    ///
    /// @param value Value to broadcast to all lanes
    /// @return Vector with all elements set to value
    ///
    /// Example:
    /// @code
    /// auto v = simd_float32x4::splat(1.5F);  // {1.5F, 1.5F, 1.5F, 1.5F}
    /// @endcode
    [[nodiscard]] static constexpr auto splat(value_type value) noexcept -> simd_type { return simd_type(value); }

    /// Create a zero vector
    /// @return Vector with all elements set to zero
    [[nodiscard]] static constexpr auto zero() noexcept -> simd_type { return simd_type(value_type{0}); }

    /// Create a vector of ones
    /// @return Vector with all elements set to one
    [[nodiscard]] static constexpr auto one() noexcept -> simd_type { return simd_type(value_type{1}); }

    // Unary operators

    /// Unary negation (element-wise)
    /// @return Vector with all elements negated
    [[nodiscard]] constexpr auto operator-() const noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = -item_[i];
        }
        return r;
    }

    /// Unary plus (identity)
    /// @return Copy of this vector
    [[nodiscard]] constexpr auto operator+() const noexcept -> simd_type { return *this; }

    // Compound assignment with scalar

    /// Add scalar to all elements
    /// @param b Scalar value to add
    /// @return Reference to this vector
    constexpr auto operator+=(value_type const& b) noexcept -> simd_type&
    {
        for (size_type i = 0; i < vector_size; ++i) {
            item_[i] += b;
        }
        return *this;
    }

    /// Subtract scalar from all elements
    /// @param b Scalar value to subtract
    /// @return Reference to this vector
    constexpr auto operator-=(value_type const& b) noexcept -> simd_type&
    {
        for (size_type i = 0; i < vector_size; ++i) {
            item_[i] -= b;
        }
        return *this;
    }

    /// Multiply all elements by scalar
    /// @param b Scalar multiplier
    /// @return Reference to this vector
    constexpr auto operator*=(value_type const& b) noexcept -> simd_type&
    {
        for (size_type i = 0; i < vector_size; ++i) {
            item_[i] *= b;
        }
        return *this;
    }

    /// Divide all elements by scalar
    /// @param b Scalar divisor
    /// @return Reference to this vector
    constexpr auto operator/=(value_type const& b) noexcept -> simd_type&
    {
        for (size_type i = 0; i < vector_size; ++i) {
            item_[i] /= b;
        }
        return *this;
    }

    // Compound assignment with vector

    /// Add another vector element-wise
    /// @param b Vector to add
    /// @return Reference to this vector
    constexpr auto operator+=(simd_type const& b) noexcept -> simd_type&
    {
        for (size_type i = 0; i < vector_size; ++i) {
            item_[i] += b[i];
        }
        return *this;
    }

    /// Subtract another vector element-wise
    /// @param b Vector to subtract
    /// @return Reference to this vector
    constexpr auto operator-=(simd_type const& b) noexcept -> simd_type&
    {
        for (size_type i = 0; i < vector_size; ++i) {
            item_[i] -= b[i];
        }
        return *this;
    }

    /// Multiply by another vector element-wise
    /// @param b Vector multiplier
    /// @return Reference to this vector
    constexpr auto operator*=(simd_type const& b) noexcept -> simd_type&
    {
        for (size_type i = 0; i < vector_size; ++i) {
            item_[i] *= b[i];
        }
        return *this;
    }

    /// Divide by another vector element-wise
    /// @param b Vector divisor
    /// @return Reference to this vector
    constexpr auto operator/=(simd_type const& b) noexcept -> simd_type&
    {
        for (size_type i = 0; i < vector_size; ++i) {
            item_[i] /= b[i];
        }
        return *this;
    }

    // Binary operators with scalar

    /// Add scalar to vector
    /// @param a Vector operand
    /// @param b Scalar operand
    /// @return Result vector with b added to each element of a
    [[nodiscard]] friend constexpr auto operator+(simd_type a, value_type const& b) noexcept -> simd_type { return a += b; }

    /// Subtract scalar from vector
    /// @param a Vector operand
    /// @param b Scalar operand
    /// @return Result vector with b subtracted from each element of a
    [[nodiscard]] friend constexpr auto operator-(simd_type a, value_type const& b) noexcept -> simd_type { return a -= b; }

    /// Multiply vector by scalar
    /// @param a Vector operand
    /// @param b Scalar operand
    /// @return Result vector with each element of a multiplied by b
    [[nodiscard]] friend constexpr auto operator*(simd_type a, value_type const& b) noexcept -> simd_type { return a *= b; }

    /// Divide vector by scalar
    /// @param a Vector operand
    /// @param b Scalar operand
    /// @return Result vector with each element of a divided by b
    [[nodiscard]] friend constexpr auto operator/(simd_type a, value_type const& b) noexcept -> simd_type { return a /= b; }

    /// Add vector to scalar (scalar on left)
    /// @param a Scalar operand
    /// @param b Vector operand
    /// @return Result vector with a added to each element of b
    [[nodiscard]] friend constexpr auto operator+(value_type const& a, simd_type b) noexcept -> simd_type { return b += a; }

    /// Subtract vector from scalar (scalar on left)
    /// @param a Scalar operand
    /// @param b Vector operand
    /// @return Result vector with each element of b subtracted from a
    [[nodiscard]] friend constexpr auto operator-(value_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = a - b[i];
        }
        return r;
    }

    /// Multiply scalar by vector (scalar on left)
    /// @param a Scalar operand
    /// @param b Vector operand
    /// @return Result vector with each element of b multiplied by a
    [[nodiscard]] friend constexpr auto operator*(value_type const& a, simd_type b) noexcept -> simd_type { return b *= a; }

    /// Divide scalar by vector (scalar on left)
    /// @param a Scalar operand
    /// @param b Vector operand
    /// @return Result vector with a divided by each element of b
    [[nodiscard]] friend constexpr auto operator/(value_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = a / b[i];
        }
        return r;
    }

    // Binary operators with vector

    /// Add two vectors element-wise
    /// @param a First vector operand
    /// @param b Second vector operand
    /// @return Result vector with element-wise sum
    [[nodiscard]] friend constexpr auto operator+(simd_type a, simd_type const& b) noexcept -> simd_type { return a += b; }

    /// Subtract two vectors element-wise
    /// @param a First vector operand
    /// @param b Second vector operand
    /// @return Result vector with element-wise difference
    [[nodiscard]] friend constexpr auto operator-(simd_type a, simd_type const& b) noexcept -> simd_type { return a -= b; }

    /// Multiply two vectors element-wise
    /// @param a First vector operand
    /// @param b Second vector operand
    /// @return Result vector with element-wise product
    [[nodiscard]] friend constexpr auto operator*(simd_type a, simd_type const& b) noexcept -> simd_type { return a *= b; }

    /// Divide two vectors element-wise
    /// @param a First vector operand
    /// @param b Second vector operand
    /// @return Result vector with element-wise quotient
    [[nodiscard]] friend constexpr auto operator/(simd_type a, simd_type const& b) noexcept -> simd_type { return a /= b; }

    // Math functions

    /// Compute square root element-wise
    /// @param a Input vector
    /// @return Vector with square root of each element
    [[nodiscard]] friend auto sqrt(simd_type const& a) noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = std::sqrt(a[i]);
        }
        return r;
    }

    /// Compute absolute value element-wise
    /// @param a Input vector
    /// @return Vector with absolute value of each element
    [[nodiscard]] friend auto abs(simd_type const& a) noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = std::abs(a[i]);
        }
        return r;
    }

    /// Compute sine element-wise
    /// @param a Input vector (radians)
    /// @return Vector with sine of each element
    [[nodiscard]] friend auto sin(simd_type const& a) noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = std::sin(a[i]);
        }
        return r;
    }

    /// Compute cosine element-wise
    /// @param a Input vector (radians)
    /// @return Vector with cosine of each element
    [[nodiscard]] friend auto cos(simd_type const& a) noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = std::cos(a[i]);
        }
        return r;
    }

    /// Compute exponential (e^x) element-wise
    /// @param a Input vector
    /// @return Vector with e^x for each element
    [[nodiscard]] friend auto exp(simd_type const& a) noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = std::exp(a[i]);
        }
        return r;
    }

    /// Compute natural logarithm element-wise
    /// @param a Input vector
    /// @return Vector with ln(x) for each element
    [[nodiscard]] friend auto log(simd_type const& a) noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = std::log(a[i]);
        }
        return r;
    }

    /// Compute reciprocal (1/x) element-wise
    /// @param a Input vector
    /// @return Vector with 1/x for each element
    [[nodiscard]] friend auto reciprocal(simd_type const& a) noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = value_type{1} / a[i];
        }
        return r;
    }

    /// Compute reciprocal square root (1/sqrt(x)) element-wise
    /// @param a Input vector
    /// @return Vector with 1/sqrt(x) for each element
    [[nodiscard]] friend auto reciprocal_sqrt(simd_type const& a) noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = value_type{1} / std::sqrt(a[i]);
        }
        return r;
    }

    /// Compute phase angle element-wise
    /// @param a Input vector
    /// @return Vector with std::arg of each element
    [[nodiscard]] friend auto arg(simd_type const& a) noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = std::arg(a[i]);
        }
        return r;
    }

    // Comparison functions (return 1 or 0 per lane)

    /// Element-wise equality comparison
    /// @return Vector with 1 where a[i] == b[i], 0 otherwise
    [[nodiscard]] friend constexpr auto equal_to(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = (a[i] == b[i]) ? value_type{1} : value_type{0};
        }
        return r;
    }

    /// Element-wise inequality comparison
    /// @return Vector with 1 where a[i] != b[i], 0 otherwise
    [[nodiscard]] friend constexpr auto not_equal_to(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = (a[i] != b[i]) ? value_type{1} : value_type{0};
        }
        return r;
    }

    /// Element-wise less-than comparison
    /// @return Vector with 1 where a[i] < b[i], 0 otherwise
    [[nodiscard]] friend constexpr auto less(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = (a[i] < b[i]) ? value_type{1} : value_type{0};
        }
        return r;
    }

    /// Element-wise less-than-or-equal comparison
    /// @return Vector with 1 where a[i] <= b[i], 0 otherwise
    [[nodiscard]] friend constexpr auto less_equal(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = (a[i] <= b[i]) ? value_type{1} : value_type{0};
        }
        return r;
    }

    /// Element-wise greater-than comparison
    /// @return Vector with 1 where a[i] > b[i], 0 otherwise
    [[nodiscard]] friend constexpr auto greater(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = (a[i] > b[i]) ? value_type{1} : value_type{0};
        }
        return r;
    }

    /// Element-wise greater-than-or-equal comparison
    /// @return Vector with 1 where a[i] >= b[i], 0 otherwise
    [[nodiscard]] friend constexpr auto greater_equal(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = (a[i] >= b[i]) ? value_type{1} : value_type{0};
        }
        return r;
    }

    // Dot product

    /// Dot product of two vectors
    /// @return Scalar sum of element-wise products
    [[nodiscard]] friend constexpr auto dot(simd_type const& a, simd_type const& b) noexcept -> value_type
    {
        return (a * b).hsum();
    }

    // Reduction operations

    /// Horizontal sum of all lanes
    ///
    /// Computes the sum of all elements in the vector.
    ///
    /// @return Sum of all elements
    ///
    /// Example:
    /// @code
    /// simd_float32x4 v{1.0F, 2.0F, 3.0F, 4.0F};
    /// float sum = v.hsum();  // 10.0f
    /// @endcode
    [[nodiscard]] constexpr auto hsum() const noexcept -> value_type
    {
        value_type sum{0};
        for (size_type i = 0; i < vector_size; ++i) {
            sum += item_[i];
        }
        return sum;
    }

    /// Horizontal product of all lanes
    ///
    /// Computes the product of all elements in the vector.
    ///
    /// @return Product of all elements
    ///
    /// Example:
    /// @code
    /// simd_float32x4 v{1.0F, 2.0F, 3.0F, 4.0F};
    /// float prod = v.hprod();  // 24.0f
    /// @endcode
    [[nodiscard]] constexpr auto hprod() const noexcept -> value_type
    {
        value_type prod{1};
        for (size_type i = 0; i < vector_size; ++i) {
            prod *= item_[i];
        }
        return prod;
    }

    /// Horizontal minimum of all lanes
    ///
    /// Finds the minimum element in the vector.
    ///
    /// @return Minimum element value
    ///
    /// Example:
    /// @code
    /// simd_float32x4 v{3.0F, 1.0F, 4.0F, 2.0F};
    /// float m = v.hmin();  // 1.0f
    /// @endcode
    [[nodiscard]] constexpr auto hmin() const noexcept -> value_type
    {
        value_type m = item_[0];
        for (size_type i = 1; i < vector_size; ++i) {
            if (item_[i] < m) {
                m = item_[i];
            }
        }
        return m;
    }

    /// Horizontal maximum of all lanes
    ///
    /// Finds the maximum element in the vector.
    ///
    /// @return Maximum element value
    ///
    /// Example:
    /// @code
    /// simd_float32x4 v{3.0F, 1.0F, 4.0F, 2.0F};
    /// float m = v.hmax();  // 4.0f
    /// @endcode
    [[nodiscard]] constexpr auto hmax() const noexcept -> value_type
    {
        value_type m = item_[0];
        for (size_type i = 1; i < vector_size; ++i) {
            if (item_[i] > m) {
                m = item_[i];
            }
        }
        return m;
    }

    // Comparison operations

    /// Element-wise minimum of two vectors
    /// @param a First vector
    /// @param b Second vector
    /// @return Vector containing min(a[i], b[i]) for each element
    [[nodiscard]] friend constexpr auto min(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = (a[i] < b[i]) ? a[i] : b[i];
        }
        return r;
    }

    /// Element-wise maximum of two vectors
    /// @param a First vector
    /// @param b Second vector
    /// @return Vector containing max(a[i], b[i]) for each element
    [[nodiscard]] friend constexpr auto max(simd_type const& a, simd_type const& b) noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = (a[i] > b[i]) ? a[i] : b[i];
        }
        return r;
    }

    /// Clamp values to range [lo, hi] element-wise
    ///
    /// @param v Input vector
    /// @param lo Lower bound vector
    /// @param hi Upper bound vector
    /// @return Vector with each element clamped to [lo[i], hi[i]]
    ///
    /// Example:
    /// @code
    /// simd_float32x4 v{-1.0F, 0.5F, 1.5F, 2.0F};
    /// auto lo = simd_float32x4::zero();
    /// auto hi = simd_float32x4::one();
    /// auto clamped = clamp(v, lo, hi);  // {0.0F, 0.5F, 1.0F, 1.0F}
    /// @endcode
    [[nodiscard]] friend constexpr auto clamp(simd_type const& v, simd_type const& lo, simd_type const& hi) noexcept -> simd_type
    {
        return min(max(v, lo), hi);
    }

    /// Multiply-add: a * b + c
    ///
    /// Computes (a * b + c) element-wise. Specialisations may use hardware
    /// FMA where available; the scalar fallback computes (a*b) + c with
    /// two roundings, so the name avoids promising the single-rounding
    /// "fused" semantics that std::fma guarantees.
    ///
    /// @param a First multiplicand
    /// @param b Second multiplicand
    /// @param c Addend
    /// @return Vector with a[i] * b[i] + c[i] for each element
    ///
    /// Example:
    /// @code
    /// simd_float32x4 a{1.0F, 2.0F, 3.0F, 4.0F};
    /// simd_float32x4 b{2.0F, 2.0F, 2.0F, 2.0F};
    /// simd_float32x4 c{1.0F, 1.0F, 1.0F, 1.0F};
    /// auto r = madd(a, b, c);  // {3.0F, 5.0F, 7.0F, 9.0F}
    /// @endcode
    [[nodiscard]] friend constexpr auto madd(simd_type const& a, simd_type const& b, simd_type const& c) noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = (a[i] * b[i]) + c[i];
        }
        return r;
    }

    /// Negative multiply-subtract: c - a * b (AltiVec vec_nmsub equivalent)
    ///
    /// Computes (c - a * b) element-wise. This is the AltiVec-style negative
    /// multiply-subtract operation. On ARM NEON this maps to vmlsq/vfmsq,
    /// on AVX FMA3 this maps to _mm256_fnmadd.
    ///
    /// @param a First multiplicand
    /// @param b Second multiplicand
    /// @param c Minuend
    /// @return Vector with c[i] - a[i] * b[i] for each element
    ///
    /// Example:
    /// @code
    /// simd_float32x4 a{1.0F, 2.0F, 3.0F, 4.0F};
    /// simd_float32x4 b{2.0F, 2.0F, 2.0F, 2.0F};
    /// simd_float32x4 c{10.0F, 10.0F, 10.0F, 10.0F};
    /// auto r = nmsub(a, b, c);  // {8.0F, 6.0F, 4.0F, 2.0F}
    /// @endcode
    [[nodiscard]] friend constexpr auto nmsub(simd_type const& a, simd_type const& b, simd_type const& c) noexcept -> simd_type
    {
        simd_type r;
        for (size_type i = 0; i < vector_size; ++i) {
            r[i] = c[i] - (a[i] * b[i]);
        }
        return r;
    }

    /// Linear interpolation: a + t * (b - a)
    ///
    /// Interpolates between two vectors based on parameter t.
    /// When t=0, returns a. When t=1, returns b.
    ///
    /// @param a Start vector (t=0)
    /// @param b End vector (t=1)
    /// @param t Interpolation parameter vector
    /// @return Interpolated vector
    ///
    /// Example:
    /// @code
    /// simd_float32x4 a{0.0F, 0.0F, 0.0F, 0.0F};
    /// simd_float32x4 b{1.0F, 2.0F, 3.0F, 4.0F};
    /// simd_float32x4 t{0.5F, 0.5F, 0.5F, 0.5F};
    /// auto r = lerp(a, b, t);  // {0.5F, 1.0F, 1.5F, 2.0F}
    /// @endcode
    [[nodiscard]] friend constexpr auto lerp(simd_type const& a, simd_type const& b, simd_type const& t) noexcept -> simd_type
    {
        return madd(t, b - a, a);
    }
};

// Common type aliases

/// 4-element single-precision floating-point SIMD vector (128-bit)
using simd_float32x4 = SIMDVec<float, 4>;

/// 8-element single-precision floating-point SIMD vector (256-bit)
using simd_float32x8 = SIMDVec<float, 8>;

/// 2-element double-precision floating-point SIMD vector (128-bit)
using simd_float64x2 = SIMDVec<double, 2>;

/// 4-element double-precision floating-point SIMD vector (256-bit)
using simd_float64x4 = SIMDVec<double, 4>;

/// Type traits to determine if a specified type is a simd type
template <typename T>
struct is_simd : std::false_type
{};

/// Partial template specialization for SIMDVec<T,N> to be shown as is_simd
/// true
template <typename ItemT, std::size_t N>
struct is_simd<SIMDVec<ItemT, N>> : std::true_type
{};

/// Apply function Func on a scalar destination, storing f(args...) into r.
template <typename T, typename Func, typename... Args>
    requires(!is_simd<T>::value)
auto apply(T& r, Func f, Args&&... args) -> T&
{
    r = f(std::forward<Args>(args)...);
    return r;
}

/// Apply function Func element-wise across SIMD vectors.
///
/// For each lane i in r, computes r[i] = f(args[i]...). All trailing
/// `args` arguments must support `operator[](size_t)` returning the
/// per-lane value — typically other SIMDVecs of the same width. Mixing
/// scalar args (which don't have `[]`) is not supported here; callers
/// that want to broadcast a scalar should splat it into a SIMDVec first.
template <typename SimdT, typename Func, typename... Args>
    requires(is_simd<SimdT>::value)
auto apply(SimdT& r, Func f, Args&&... args) -> SimdT&
{
    for (size_t i = 0; i < r.size(); ++i) {
        apply(r[i], f, std::forward<Args>(args)[i]...);
    }
    return r;
}

template <typename T>
struct simd_value_type
{
    using type = T;
};

template <typename T, std::size_t N>
struct simd_value_type<SIMDVec<T, N>>
{
    using type = T;
};

template <typename T>
struct simd_size : public std::integral_constant<size_t, 1>
{};

template <typename T, std::size_t X>
struct simd_size<SIMDVec<T, X>> : public std::integral_constant<size_t, X>
{};

template <typename T>
struct simd_flattened_size : public std::integral_constant<size_t, 1>
{};

template <typename T, std::size_t X>
struct simd_flattened_size<SIMDVec<T, X>> : public std::integral_constant<size_t, X>
{};

template <typename T, std::size_t X, std::size_t Y>
struct simd_flattened_size<SIMDVec<SIMDVec<T, Y>, X>> : public std::integral_constant<size_t, X * Y>
{};

template <typename T, std::size_t X, std::size_t Y, std::size_t Z>
struct simd_flattened_size<SIMDVec<SIMDVec<SIMDVec<T, Z>, Y>, X>> : public std::integral_constant<size_t, X * Y * Z>
{};

template <typename T, std::size_t X, std::size_t Y, std::size_t Z, std::size_t W>
struct simd_flattened_size<SIMDVec<SIMDVec<SIMDVec<SIMDVec<T, W>, Z>, Y>, X>> : public std::integral_constant<size_t, X * Y * Z * W>
{};

template <typename T>
struct simd_flattened_type
{
    using type = T;
};

template <typename T, std::size_t X>
struct simd_flattened_type<SIMDVec<T, X>>
{
    using type = T;
};

template <typename T, std::size_t X, std::size_t Y>
struct simd_flattened_type<SIMDVec<SIMDVec<T, Y>, X>>
{
    using type = T;
};

template <typename T, std::size_t X, std::size_t Y, std::size_t Z>
struct simd_flattened_type<SIMDVec<SIMDVec<SIMDVec<T, Z>, Y>, X>>
{
    using type = T;
};

template <typename T, std::size_t X, std::size_t Y, std::size_t Z, std::size_t W>
struct simd_flattened_type<SIMDVec<SIMDVec<SIMDVec<SIMDVec<T, W>, Z>, Y>, X>>
{
    using type = T;
};

// Helper functions for BiQuad and other DSP components

/// Zero a scalar value
template <typename T>
    requires(!is_simd<T>::value)
constexpr void zero(T& value) noexcept
{
    value = T{0};
}

/// Zero a SIMD vector
/// @param value SIMD vector to set to zero
template <typename T>
    requires(is_simd<T>::value)
constexpr void zero(T& value) noexcept
{
    value = T::zero();
}

/// Set a flattened item in a scalar (channel must be 0)
template <typename T, typename ItemT>
    requires(!is_simd<T>::value)
constexpr void set_flattened_item(T& value, ItemT const item, [[maybe_unused]] size_t const channel) noexcept
{
    value = static_cast<T>(item);
}

/// Set a flattened item in a SIMD vector by flattened channel index.
///
/// Recurses into nested SIMD: for SIMDVec<X, OuterN> where X is itself a
/// SIMDVec, the channel index is divided by the inner type's flattened
/// size to pick the outer lane, then the remainder selects within. This
/// makes the addressing orthogonal across any nesting depth — channel
/// 5 of a SIMDVec<SIMDVec<float, 4>, 2> selects outer index 1, inner
/// index 1, and writes to value[1][1].
template <typename T, typename ItemT>
    requires(is_simd<T>::value)
constexpr void set_flattened_item(T& value, ItemT const item, size_t const channel) noexcept
{
    constexpr size_t inner_flattened = simd_flattened_size<typename T::value_type>::value;
    set_flattened_item(value[channel / inner_flattened], item, channel % inner_flattened);
}

/// Get a flattened item from a scalar (channel must be 0)
template <typename T>
    requires(!is_simd<T>::value)
[[nodiscard]] constexpr auto get_flattened_item(T const& value, [[maybe_unused]] size_t const channel) noexcept -> T
{
    return value;
}

/// Get a flattened item from a SIMD vector by flattened channel index.
/// Recurses into nested SIMD using the same divide/modulo addressing as
/// set_flattened_item.
template <typename T>
    requires(is_simd<T>::value)
[[nodiscard]] constexpr auto get_flattened_item(T const& value, size_t const channel) noexcept ->
    typename simd_flattened_type<T>::type
{
    constexpr size_t inner_flattened = simd_flattened_size<typename T::value_type>::value;
    return get_flattened_item(value[channel / inner_flattened], channel % inner_flattened);
}

/** \addtogroup simd_arith add/sub/mul/madd/nmsub */
/**@{*/
//
// Generic free-function arithmetic that works uniformly across:
//   - Scalars (float, double)
//   - One-level SIMDVec<T, N>
//   - Nested SIMDVec<SIMDVec<T, N>, M> and deeper
//
// For SIMD operands the existing SIMDVec friend operators / madd already
// handle the work (and recurse element-wise into nested SIMDVecs via the
// scalar fallback). These free templates are thin wrappers in the dsp
// namespace so generic templated code can write `dsp::add(x, y)` instead
// of `x + y` regardless of whether T is scalar or SIMD — useful when the
// concrete arithmetic operator may be ambiguous or when readers prefer
// named function calls.
//
// `madd(a, b, c)` returns a*b + c. The scalar fallback uses two
// roundings; SIMDVec specializations may use hardware FMA. The name
// avoids "fma" precisely because the scalar path is not fused.
//
// `nmsub(a, b, c)` returns c - a*b (AltiVec-style negative
// multiply-subtract; matches NEON vmlsq / AVX FNMADD intrinsics).
//
// The scalar-broadcast variant `mul(T, Scalar)` recurses through any
// nesting depth, broadcasting the scalar to every leaf — solves the
// `nested * float` case where SIMDVec's friend `operator*(simd, value_type)`
// can't help (its value_type is the inner SIMDVec, not the leaf scalar).

template <typename T>
[[nodiscard]] constexpr auto add(T const& a, T const& b) noexcept -> T
{
    return a + b;
}

template <typename T>
[[nodiscard]] constexpr auto sub(T const& a, T const& b) noexcept -> T
{
    return a - b;
}

template <typename T>
[[nodiscard]] constexpr auto mul(T const& a, T const& b) noexcept -> T
{
    return a * b;
}

// madd / nmsub: scalar specializations. SIMDVec specializations exist as
// friend methods on each SIMDVec instantiation (via ADL) and win
// overload resolution on SIMD operands.
template <typename T>
    requires(!is_simd<T>::value)
[[nodiscard]] constexpr auto madd(T const& a, T const& b, T const& c) noexcept -> T
{
    return (a * b) + c;
}

template <typename T>
    requires(!is_simd<T>::value)
[[nodiscard]] constexpr auto nmsub(T const& a, T const& b, T const& c) noexcept -> T
{
    return c - (a * b);
}

// Scalar-broadcast multiply: `mul(a, s)` where s is a scalar (typically
// the deeply-flattened scalar type, e.g. float or double). Recurses
// through any nesting depth so this works for
// `mul(simd_float32x4, float)`, `mul(SIMDVec<simd_float32x4, 2>, float)`,
// etc.  Disambiguated from the same-type `mul(T, T)` above via the
// `!is_same_v<T, Scalar>` constraint — same-type calls go to that
// overload, mixed-type calls land here.
template <typename T, typename Scalar>
    requires(!std::is_same_v<T, Scalar> && !is_simd<T>::value)
[[nodiscard]] constexpr auto mul(T const& a, Scalar s) noexcept -> T
{
    return a * static_cast<T>(s);
}

template <typename T, typename Scalar>
    requires(!std::is_same_v<T, Scalar> && is_simd<T>::value)
[[nodiscard]] constexpr auto mul(T const& a, Scalar s) noexcept -> T
{
    T r;
    for (size_t i = 0; i < T::vector_size; ++i) {
        r[i] = mul(a[i], s);
    }
    return r;
}

/**@}*/

/** \addtogroup simd_splat splat */
/**@{*/

inline auto splat(float& v, float const a) -> float&
{
    v = a;
    return v;
}

inline auto splat(double& v, float const a) -> double&
{
    v = static_cast<double>(a);
    return v;
}

template <typename T>
inline auto splat(std::complex<T>& v, std::complex<T> const& a) -> std::complex<T>&
{
    v = a;
    return v;
}

/**@}*/

/** \addtogroup simd_zero zero */
/**@{*/

inline void zero(float& v) noexcept
{
    v = 0.0F;
}

inline void zero(double& v) noexcept
{
    v = 0.0;
}

template <typename T>
inline void zero(std::complex<T>& v) noexcept
{
    v = std::complex<T>(T(0), T(0));
}

/**@}*/

/** \addtogroup simd_one one */
/**@{*/

inline auto one(float& v) -> float&
{
    v = 1.0F;
    return v;
}

inline auto one(double& v) -> double&
{
    v = 1.0;
    return v;
}

template <typename T>
inline auto one(std::complex<T>& v) -> std::complex<T>&
{
    v = std::complex<T>(T(1), T(0));
    return v;
}

/**@}*/

/** \addtogroup simd_reciprocal reciprocal */
/**@{*/

inline auto reciprocal(float const v) -> float
{
    return 1.0F / v;
}

inline auto reciprocal(double const v) -> double
{
    return 1.0 / v;
}

template <typename T>
inline auto reciprocal(std::complex<T> const& v) -> std::complex<T>
{
    std::complex<T> a;
    one(a);
    a = a / v;
    return a;
}

/**@}*/

/** \addtogroup simd_reciprocal_sqrt reciprocal_sqrt */
/**@{*/

inline auto reciprocal_sqrt(float const v) -> float
{
    return 1.0F / std::sqrt(v);
}

inline auto reciprocal_sqrt(double const v) -> double
{
    return 1.0 / std::sqrt(v);
}

template <typename T>
inline auto reciprocal_sqrt(std::complex<T> const& v) -> std::complex<T>
{
    std::complex<T> a;
    one(a);
    a = a / std::sqrt(v);
    return a;
}

/**@}*/

/** \addtogroup simd_compare_equal_to equal_to */
/**@{*/

inline auto equal_to(float const a, float const b) -> float
{
    return a == b ? 1.0F : 0.0F;
}

inline auto equal_to(double const a, double const b) -> double
{
    return a == b ? 1.0 : 0.0;
}

template <typename T>
inline auto equal_to(std::complex<T> const& a, std::complex<T> const& b) -> std::complex<T>
{
    return (a == b) ? std::complex<T>(1, 0) : std::complex<T>(0, 0);
}

/**@}*/

/** \addtogroup simd_compare_not_equal_to not_equal_to */
/**@{*/

inline auto not_equal_to(float const a, float const b) -> float
{
    return a != b ? 1.0F : 0.0F;
}

inline auto not_equal_to(double const a, double const b) -> double
{
    return a != b ? 1.0 : 0.0;
}

template <typename T>
inline auto not_equal_to(std::complex<T> const& a, std::complex<T> const& b) -> std::complex<T>
{
    return (a != b) ? std::complex<T>(1, 0) : std::complex<T>(0, 0);
}

/**@}*/

// ─────────────────────────────────────────────────────────────────────────────
// Complex number ordering — semantic note
//
// Complex numbers have no natural total order in IEEE 754 / mathematics.
// The less / less_equal / greater / greater_equal overloads for
// std::complex<T> below DELIBERATELY use magnitude (abs) comparison
// rather than e.g. lexicographic (real, imag) ordering. This matches
// envelope/magnitude-based DSP intent — comparing complex signals by
// "how big" they are rather than by some arbitrary tuple order.
//
// Callers that need a different ordering should compare components
// explicitly (e.g. `a.real() < b.real()` or
// `std::pair{a.real(), a.imag()} < std::pair{b.real(), b.imag()}`).
//
// equal_to / not_equal_to use std::complex's own operator== (component-
// wise equality), which IS the standard / mathematical definition.
// ─────────────────────────────────────────────────────────────────────────────

/** \addtogroup simd_compare_less less */
/**@{*/

inline auto less(float const a, float const b) -> float
{
    return a < b ? 1.0F : 0.0F;
}

inline auto less(double const a, double const b) -> double
{
    return a < b ? 1.0 : 0.0;
}

template <typename T>
inline auto less(std::complex<T> const& a, std::complex<T> const& b) -> std::complex<T>
{
    return (abs(a) < abs(b)) ? std::complex<T>(1, 0) : std::complex<T>(0, 0);
}

/**@}*/

/** \addtogroup simd_compare_less_equal less_equal */
/**@{*/

inline auto less_equal(float const a, float const b) -> float
{
    return a <= b ? 1.0F : 0.0F;
}

inline auto less_equal(double const a, double const b) -> double
{
    return a <= b ? 1.0 : 0.0;
}

template <typename T>
inline auto less_equal(std::complex<T> const& a, std::complex<T> const& b) -> std::complex<T>
{
    return (abs(a) <= abs(b)) ? std::complex<T>(1, 0) : std::complex<T>(0, 0);
}

/**@}*/

/** \addtogroup simd_compare_greater greater */
/**@{*/

inline auto greater(float const a, float const b) -> float
{
    return a > b ? 1.0F : 0.0F;
}

inline auto greater(double const a, double const b) -> double
{
    return a > b ? 1.0 : 0.0;
}

template <typename T>
inline auto greater(std::complex<T> const& a, std::complex<T> const& b) -> std::complex<T>
{
    return (abs(a) > abs(b)) ? std::complex<T>(1, 0) : std::complex<T>(0, 0);
}

/**@}*/

/** \addtogroup simd_compare_greater_equal greater_equal */
/**@{*/

inline auto greater_equal(float const a, float const b) -> float
{
    return a >= b ? 1.0F : 0.0F;
}

inline auto greater_equal(double const a, double const b) -> double
{
    return a >= b ? 1.0 : 0.0;
}

template <typename T>
inline auto greater_equal(std::complex<T> const& a, std::complex<T> const& b) -> std::complex<T>
{
    return (abs(a) >= abs(b)) ? std::complex<T>(1, 0) : std::complex<T>(0, 0);
}

/**@}*/

/** \addtogroup simd_lround lround */
/**@{*/

/// Round to nearest integer element-wise for SIMD vectors (ties away from zero)
///
/// Computes the nearest integer for each element, with halfway cases
/// rounded away from zero. This matches the behavior of std::lround.
///
/// @tparam T Element type (float or double)
/// @tparam N Number of elements
/// @param a Input vector
/// @return Vector of int64_t with rounded values
///
/// Example:
/// @code
/// simd_float32x4 v{1.4F, 1.5F, -1.4F, -1.5F};
/// auto r = lround(v);  // {1, 2, -1, -2}
/// @endcode
template <typename T, std::size_t N>
[[nodiscard]] constexpr auto lround(SIMDVec<T, N> const& a) noexcept -> SIMDVec<int64_t, N>
{
    SIMDVec<int64_t, N> r;
    for (std::size_t i = 0; i < N; ++i) {
        // For positive: floor(x + 0.5), for negative: ceil(x - 0.5)
        r[i] = static_cast<int64_t>(a[i] >= T{0} ? a[i] + T{0.5} : a[i] - T{0.5});
    }
    return r;
}

/**@}*/

}  // namespace statusbar::dsp
