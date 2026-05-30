#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// OSC Message - Open Sound Control Protocol Message Type
/// Based on OSC 1.0 specification

#include "statusbar/osc/osc_argument.hpp"
#include "statusbar/osc/osc_base.hpp"

#include <cstddef>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

namespace statusbar::osc {

//
// OSC Message Class
//
/// An OSC message consisting of an address pattern and zero or more arguments
class OscMessage
{
  public:
    /// Default constructor - creates empty message with root address.
    /// Uses std::pmr::get_default_resource() for argument storage.
    OscMessage() = default;

    /// Construct with address pattern.
    /// @param address The OSC address pattern (must start with '/')
    /// @param memory_resource Memory resource for the arguments vector.
    ///        nullptr is treated as std::pmr::get_default_resource().
    explicit OscMessage(std::string_view address, std::pmr::memory_resource* memory_resource = nullptr)
        : address_{address}
        , arguments_{memory_resource != nullptr ? memory_resource : std::pmr::get_default_resource()}
    {}

    //===
    // Address Pattern
    //===

    /// Get the address pattern
    [[nodiscard]] auto address() const noexcept -> std::string_view { return address_; }

    /// Set the address pattern
    /// @param addr The OSC address pattern (should start with '/')
    void set_address(std::string_view addr) { address_ = std::string{addr}; }

    /// Check if the address pattern is valid (starts with '/')
    [[nodiscard]] auto has_valid_address() const noexcept -> bool { return !address_.empty() && address_[0] == '/'; }

    //===
    // Arguments
    //===

    /// Get the number of arguments
    [[nodiscard]] auto argument_count() const noexcept -> size_t { return arguments_.size(); }

    /// Check if the message has any arguments
    [[nodiscard]] auto has_arguments() const noexcept -> bool { return !arguments_.empty(); }

    /// Get argument at index (returns nullptr if out of range)
    /// @param index Zero-based argument index
    [[nodiscard]] auto argument(size_t index) const noexcept -> OscArgument const*
    {
        if (index < arguments_.size()) {
            return &arguments_[index];
        }
        return nullptr;
    }

    /// Get all arguments
    [[nodiscard]] auto arguments() const noexcept -> std::pmr::vector<OscArgument> const& { return arguments_; }

    /// Append an argument
    /// @param arg The OSC argument to append
    void append(OscArgument arg) { arguments_.push_back(std::move(arg)); }

    /// Append a 32-bit integer
    /// @param value The 32-bit integer value to append
    void append_int32(int32_t value) { arguments_.push_back(OscArgument::make_int32(value)); }

    /// Append a 32-bit float
    /// @param value The 32-bit float value to append
    void append_float(float value) { arguments_.push_back(OscArgument::make_float(value)); }

    /// Append a 64-bit double
    /// @param value The 64-bit double value to append
    void append_double(double value) { arguments_.push_back(OscArgument::make_double(value)); }

    /// Append a 64-bit integer
    /// @param value The 64-bit integer value to append
    void append_int64(int64_t value) { arguments_.push_back(OscArgument::make_int64(value)); }

    /// Append a string
    /// @param value The string value to append
    void append_string(std::string_view value) { arguments_.push_back(OscArgument::make_string(value)); }

    /// Append a blob
    /// @param data The binary data to append
    void append_blob(std::span<uint8_t const> data) { arguments_.push_back(OscArgument::make_blob(data)); }

    /// Append a timetag
    /// @param value The NTP timetag value to append
    void append_timetag(NtpTimetag value) { arguments_.push_back(OscArgument::make_timetag(value)); }

    /// Append true
    void append_true() { arguments_.push_back(OscArgument::make_true()); }

    /// Append false
    void append_false() { arguments_.push_back(OscArgument::make_false()); }

    /// Append nil
    void append_nil() { arguments_.push_back(OscArgument::make_nil()); }

    /// Append impulse/bang
    void append_impulse() { arguments_.push_back(OscArgument::make_impulse()); }

    /// Clear all arguments
    void clear_arguments() noexcept { arguments_.clear(); }

    /// Reserve capacity for arguments
    /// @param capacity Number of arguments to pre-allocate space for
    void reserve_arguments(size_t capacity) { arguments_.reserve(capacity); }

    //===
    // Type Tag String
    //===

    /// Generate the type tag string (including leading ',')
    [[nodiscard]] auto type_tags() const -> std::string;

    //===
    // Wire Size Calculation
    //===

    /// Calculate the total wire size for this message
    [[nodiscard]] auto wire_size() const noexcept -> size_t;

    //===
    // Validation
    //===

    /// Check if this message is valid for serialization
    [[nodiscard]] auto is_valid() const noexcept -> bool { return has_valid_address(); }

  private:
    std::string address_{"/"};                 ///< OSC address pattern
    std::pmr::vector<OscArgument> arguments_;  ///< Message arguments
};

}  // namespace statusbar::osc
