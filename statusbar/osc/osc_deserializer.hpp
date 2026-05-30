#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// OSC Deserializer - Open Sound Control Protocol Deserialization
/// Based on OSC 1.0 specification

#include "statusbar/buffer/buffer.hpp"
#include "statusbar/osc/osc_argument.hpp"
#include "statusbar/osc/osc_base.hpp"
#include "statusbar/osc/osc_bundle.hpp"
#include "statusbar/osc/osc_error.hpp"
#include "statusbar/osc/osc_message.hpp"
#include "statusbar/status/status.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <variant>

namespace statusbar::osc {

//
// Detection Helpers
//
/// Check if data starts with bundle identifier "#bundle"
/// @param data Raw OSC data to inspect
[[nodiscard]] auto osc_is_bundle(std::span<uint8_t const> data) noexcept -> bool;

/// Check if data appears to be an OSC message (starts with '/')
/// @param data Raw OSC data to inspect
[[nodiscard]] auto osc_is_message(std::span<uint8_t const> data) noexcept -> bool;

//
// Low-Level Deserialization Helpers
//
/// Load a 32-bit integer from big-endian format
/// @param buffer Source buffer containing big-endian data
/// @param value Output: the deserialized integer
[[nodiscard]] auto load_int32(std::span<uint8_t const> buffer, int32_t& value) noexcept -> StatusValue<size_t>;

/// Load a 32-bit float from big-endian format
/// @param buffer Source buffer containing big-endian data
/// @param value Output: the deserialized float
[[nodiscard]] auto load_float32(std::span<uint8_t const> buffer, float& value) noexcept -> StatusValue<size_t>;

/// Load a 64-bit integer from big-endian format
/// @param buffer Source buffer containing big-endian data
/// @param value Output: the deserialized 64-bit integer
[[nodiscard]] auto load_int64(std::span<uint8_t const> buffer, int64_t& value) noexcept -> StatusValue<size_t>;

/// Load a 64-bit double from big-endian format
/// @param buffer Source buffer containing big-endian data
/// @param value Output: the deserialized double
[[nodiscard]] auto load_float64(std::span<uint8_t const> buffer, double& value) noexcept -> StatusValue<size_t>;

/// Load an NTP timetag
/// @param buffer Source buffer containing big-endian data
/// @param value Output: the deserialized timetag
[[nodiscard]] auto load_timetag(std::span<uint8_t const> buffer, NtpTimetag& value) noexcept -> StatusValue<size_t>;

/// Load a null-terminated, 4-byte aligned string
/// Returns the string content and advances past the padded data
/// @param buffer Source buffer containing padded string data
/// @param value Output: view into the deserialized string
[[nodiscard]] auto load_string(std::span<uint8_t const> buffer, std::string_view& value) noexcept -> StatusValue<size_t>;

/// Load a blob (length prefix + data + padding)
/// @param buffer Source buffer containing blob data
/// @param value Output: span referencing the deserialized blob data
[[nodiscard]] auto load_blob(std::span<uint8_t const> buffer, std::span<uint8_t const>& value) noexcept -> StatusValue<size_t>;

//
// Argument Deserialization
//
/// Deserialize an OSC argument from a buffer
/// @param buffer The buffer to read from
/// @param type_tag The type tag character for this argument
/// @param bytes_consumed Output: number of bytes consumed from buffer
/// @return The deserialized argument or error
[[nodiscard]] auto osc_deserialize_argument(std::span<uint8_t const> buffer, char type_tag, size_t& bytes_consumed) noexcept
    -> StatusValue<OscArgument>;

//
// Message Deserialization
//
/// Deserialize an OSC message into a pre-allocated message object
/// @param buffer The buffer to read from
/// @param msg Output: the message to populate (cleared first, uses existing capacity)
/// @param bytes_consumed Output: number of bytes consumed from buffer
/// @return Success or error status
///
/// This overload allows allocation-free parsing by reusing a pre-allocated OscMessage.
/// The client can call msg.reserve_arguments(n) at init time to pre-allocate storage.
[[nodiscard]] auto osc_deserialize_message(std::span<uint8_t const> buffer, OscMessage& msg, size_t& bytes_consumed) -> Status;

/// Deserialize an OSC message from a buffer
/// @param buffer The buffer to read from
/// @param bytes_consumed Output: number of bytes consumed from buffer
/// @return The deserialized message or error
[[nodiscard]] auto osc_deserialize_message(std::span<uint8_t const> buffer, size_t& bytes_consumed) -> StatusValue<OscMessage>;

//
// Bundle Deserialization
//
/// Deserialize an OSC bundle from a buffer
/// @param buffer The buffer to read from
/// @param bytes_consumed Output: number of bytes consumed from buffer
/// @return The deserialized bundle or error
[[nodiscard]] auto osc_deserialize_bundle(std::span<uint8_t const> buffer, size_t& bytes_consumed) -> StatusValue<OscBundle>;

//
// Generic Parsing
//
/// Parse either an OSC message or bundle from a buffer
/// @param buffer The buffer to parse
/// @return The parsed message or bundle, or error
[[nodiscard]] auto osc_parse(std::span<uint8_t const> buffer) -> StatusValue<std::variant<OscMessage, OscBundle>>;

}  // namespace statusbar::osc
