#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// OSC Argument Types - Open Sound Control Protocol Argument Handling
/// Based on OSC 1.0 specification

#include "statusbar/osc/osc_base.hpp"
#include "statusbar/osc/osc_error.hpp"
#include "statusbar/status/status.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace statusbar::osc {

//
// OSC Argument Value Type
//
/// Variant type for OSC argument values
/// std::monostate represents types with no data (T, F, N, I)
using OscArgumentValue = std::variant<
    std::monostate,        // For T, F, N, I (no data)
    int32_t,               // 'i' - 32-bit integer
    float,                 // 'f' - 32-bit float
    double,                // 'd' - 64-bit double
    int64_t,               // 'h' - 64-bit integer
    std::string,           // 's' - string
    std::vector<uint8_t>,  // 'b' - blob
    NtpTimetag             // 't', 'a' - timetag
    >;

//
// OSC Argument Class
//
/// Type-safe OSC argument with tagged value storage
class OscArgument
{
  public:
    /// Default constructor - creates nil argument
    constexpr OscArgument() noexcept = default;

    //===
    // Factory Functions
    //===

    /// Create a 32-bit integer argument
    /// @param value The 32-bit integer value
    [[nodiscard]] static auto make_int32(int32_t value) noexcept -> OscArgument
    {
        OscArgument arg;
        arg.type_tag_ = type_tag::int32;
        arg.value_ = value;
        return arg;
    }

    /// Create a 32-bit float argument
    /// @param value The 32-bit float value
    [[nodiscard]] static auto make_float(float value) noexcept -> OscArgument
    {
        OscArgument arg;
        arg.type_tag_ = type_tag::float32;
        arg.value_ = value;
        return arg;
    }

    /// Create a 64-bit double argument
    /// @param value The 64-bit double value
    [[nodiscard]] static auto make_double(double value) noexcept -> OscArgument
    {
        OscArgument arg;
        arg.type_tag_ = type_tag::float64;
        arg.value_ = value;
        return arg;
    }

    /// Create a 64-bit integer argument
    /// @param value The 64-bit integer value
    [[nodiscard]] static auto make_int64(int64_t value) noexcept -> OscArgument
    {
        OscArgument arg;
        arg.type_tag_ = type_tag::int64;
        arg.value_ = value;
        return arg;
    }

    /// Create a string argument
    /// @param value The string value
    [[nodiscard]] static auto make_string(std::string_view value) -> OscArgument
    {
        OscArgument arg;
        arg.type_tag_ = type_tag::string;
        arg.value_ = std::string{value};
        return arg;
    }

    /// Create a blob argument
    /// @param data The binary data for the blob
    [[nodiscard]] static auto make_blob(std::span<uint8_t const> data) -> OscArgument
    {
        OscArgument arg;
        arg.type_tag_ = type_tag::blob;
        arg.value_ = std::vector<uint8_t>{data.begin(), data.end()};
        return arg;
    }

    /// Create an NTP timetag argument
    /// @param value The NTP timetag value
    [[nodiscard]] static auto make_timetag(NtpTimetag value) noexcept -> OscArgument
    {
        OscArgument arg;
        arg.type_tag_ = type_tag::timetag;
        arg.value_ = value;
        return arg;
    }

    /// Create an AVB timetag argument (custom extension)
    /// @param value The AVB timetag value
    [[nodiscard]] static auto make_avb_timetag(NtpTimetag value) noexcept -> OscArgument
    {
        OscArgument arg;
        arg.type_tag_ = type_tag::avb_time;
        arg.value_ = value;
        return arg;
    }

    /// Create a true argument
    [[nodiscard]] static auto make_true() noexcept -> OscArgument
    {
        OscArgument arg;
        arg.type_tag_ = type_tag::true_val;
        arg.value_ = std::monostate{};
        return arg;
    }

    /// Create a false argument
    [[nodiscard]] static auto make_false() noexcept -> OscArgument
    {
        OscArgument arg;
        arg.type_tag_ = type_tag::false_val;
        arg.value_ = std::monostate{};
        return arg;
    }

    /// Create a nil argument
    [[nodiscard]] static auto make_nil() noexcept -> OscArgument
    {
        OscArgument arg;
        arg.type_tag_ = type_tag::nil;
        arg.value_ = std::monostate{};
        return arg;
    }

    /// Create an impulse/bang argument
    [[nodiscard]] static auto make_impulse() noexcept -> OscArgument
    {
        OscArgument arg;
        arg.type_tag_ = type_tag::impulse;
        arg.value_ = std::monostate{};
        return arg;
    }

    //===
    // Accessors
    //===

    /// Get the type tag character
    [[nodiscard]] constexpr auto get_type_tag() const noexcept -> char { return type_tag_; }

    /// Get as 32-bit integer
    [[nodiscard]] auto as_int32() const noexcept -> StatusValue<int32_t>
    {
        if (auto const* ptr = std::get_if<int32_t>(&value_)) {
            return success(*ptr);
        }
        return failure(OscError::type_mismatch);
    }

    /// Get as 32-bit float
    [[nodiscard]] auto as_float() const noexcept -> StatusValue<float>
    {
        if (auto const* ptr = std::get_if<float>(&value_)) {
            return success(*ptr);
        }
        return failure(OscError::type_mismatch);
    }

    /// Get as 64-bit double
    [[nodiscard]] auto as_double() const noexcept -> StatusValue<double>
    {
        if (auto const* ptr = std::get_if<double>(&value_)) {
            return success(*ptr);
        }
        return failure(OscError::type_mismatch);
    }

    /// Get as 64-bit integer
    [[nodiscard]] auto as_int64() const noexcept -> StatusValue<int64_t>
    {
        if (auto const* ptr = std::get_if<int64_t>(&value_)) {
            return success(*ptr);
        }
        return failure(OscError::type_mismatch);
    }

    /// Get as string view
    [[nodiscard]] auto as_string() const noexcept -> StatusValue<std::string_view>
    {
        if (auto const* ptr = std::get_if<std::string>(&value_)) {
            return success(std::string_view{*ptr});
        }
        return failure(OscError::type_mismatch);
    }

    /// Get as blob span
    [[nodiscard]] auto as_blob() const noexcept -> StatusValue<std::span<uint8_t const>>
    {
        if (auto const* ptr = std::get_if<std::vector<uint8_t>>(&value_)) {
            return success(std::span<uint8_t const>{*ptr});
        }
        return failure(OscError::type_mismatch);
    }

    /// Get as timetag
    [[nodiscard]] auto as_timetag() const noexcept -> StatusValue<NtpTimetag>
    {
        if (auto const* ptr = std::get_if<NtpTimetag>(&value_)) {
            return success(*ptr);
        }
        return failure(OscError::type_mismatch);
    }

    /// Get as boolean (supports T, F, and integer conversion)
    [[nodiscard]] auto as_bool() const noexcept -> StatusValue<bool>;

    //===
    // Type Queries
    //===

    /// Check if this argument has data bytes on the wire
    [[nodiscard]] constexpr auto has_data() const noexcept -> bool
    {
        switch (type_tag_) {
            case type_tag::true_val:
            case type_tag::false_val:
            case type_tag::nil:
            case type_tag::impulse:
                return false;
            default:
                return true;
        }
    }

    /// Check if this is a numeric type (int32, float, double, int64)
    [[nodiscard]] constexpr auto is_numeric() const noexcept -> bool
    {
        switch (type_tag_) {
            case type_tag::int32:
            case type_tag::float32:
            case type_tag::float64:
            case type_tag::int64:
                return true;
            default:
                return false;
        }
    }

    //===
    // Visitor
    //===

    /// Visit the argument value with a callable overload set.
    /// Dispatches to the appropriate overload based on the stored type:
    ///   int32_t, float, double, int64_t, std::string_view,
    ///   std::span<uint8_t const>, NtpTimetag, bool (for T/F),
    ///   std::monostate (for nil/impulse)
    template <typename Visitor>
    auto visit(Visitor&& vis) const  // NOLINT(cppcoreguidelines-missing-std-forward)
    {
        switch (type_tag_) {
            case type_tag::int32:
                return vis(*std::get_if<int32_t>(&value_));
            case type_tag::float32:
                return vis(*std::get_if<float>(&value_));
            case type_tag::float64:
                return vis(*std::get_if<double>(&value_));
            case type_tag::int64:
                return vis(*std::get_if<int64_t>(&value_));
            case type_tag::string:
                return vis(std::string_view{*std::get_if<std::string>(&value_)});
            case type_tag::blob:
                return vis(std::span<uint8_t const>{*std::get_if<std::vector<uint8_t>>(&value_)});
            case type_tag::timetag:
            case type_tag::avb_time:
                return vis(*std::get_if<NtpTimetag>(&value_));
            case type_tag::true_val:
                return vis(true);
            case type_tag::false_val:
                return vis(false);
            default:
                return vis(std::monostate{});
        }
    }

    //===
    // Wire Size Calculation
    //===

    /// Calculate the wire size for this argument's data (not including type tag)
    [[nodiscard]] auto wire_size() const noexcept -> size_t;

  private:
    char type_tag_{type_tag::nil};  ///< OSC type tag character
    OscArgumentValue value_{};      ///< Variant holding the actual value
};

}  // namespace statusbar::osc
