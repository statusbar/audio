#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// OSC Error Types - Open Sound Control Protocol Error Handling
/// Based on OSC 1.0 specification

#include <string>
#include <system_error>

namespace statusbar::osc {

//
// OSC Error Codes
//
/// Error codes for OSC parsing and serialization
enum class OscError
{
    invalid_address = 1,        ///< Address doesn't start with '/'
    invalid_type_tag = 2,       ///< Type tag doesn't start with ','
    unsupported_type = 3,       ///< Unknown type tag character
    buffer_overflow = 4,        ///< Insufficient buffer space for serialization
    buffer_underflow = 5,       ///< Unexpected end of data during parsing
    invalid_bundle = 6,         ///< Bundle doesn't start with "#bundle"
    invalid_alignment = 7,      ///< Data not 4-byte aligned
    string_not_terminated = 8,  ///< String missing null terminator
    type_mismatch = 9,          ///< Argument type doesn't match requested type
    invalid_message = 10,       ///< Message format is invalid
    bundle_too_deep = 11,       ///< Bundle nesting exceeds OSC_MAX_BUNDLE_DEPTH
};

//
// OSC Error Category
//
/// Custom error category for OSC errors
class OscErrorCategory : public std::error_category
{
  public:
    [[nodiscard]] auto name() const noexcept -> char const* override { return "statusbar.osc"; }

    [[nodiscard]] auto message(int ev) const -> std::string override;
};

/// Get the singleton OSC error category instance
[[nodiscard]] auto osc_error_category() noexcept -> OscErrorCategory const&;

/// Create an error_code from an OscError
/// @param e The OSC error code to convert
[[nodiscard]] auto make_error_code(OscError e) noexcept -> std::error_code;

}  // namespace statusbar::osc

// Register OscError as an error code enum
template <>
struct std::is_error_code_enum<statusbar::osc::OscError> : std::true_type
{};
