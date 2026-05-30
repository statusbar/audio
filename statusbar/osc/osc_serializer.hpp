#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// OSC Serializer - Open Sound Control Protocol Serialization
/// Based on OSC 1.0 specification

#include "statusbar/buffer/buffer.hpp"
#include "statusbar/ieee/ieee.hpp"
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

namespace statusbar::osc {

//
// Low-Level Serialization Helpers
//
/// Store a 32-bit integer in big-endian format
/// @param buffer Destination buffer for big-endian output
/// @param value The 32-bit integer to store
[[nodiscard]] auto store_int32(std::span<uint8_t> buffer, int32_t value) noexcept -> StatusValue<size_t>;

/// Store a 32-bit float in big-endian format
/// @param buffer Destination buffer for big-endian output
/// @param value The 32-bit float to store
[[nodiscard]] auto store_float32(std::span<uint8_t> buffer, float value) noexcept -> StatusValue<size_t>;

/// Store a 64-bit integer in big-endian format
/// @param buffer Destination buffer for big-endian output
/// @param value The 64-bit integer to store
[[nodiscard]] auto store_int64(std::span<uint8_t> buffer, int64_t value) noexcept -> StatusValue<size_t>;

/// Store a 64-bit double in big-endian format
/// @param buffer Destination buffer for big-endian output
/// @param value The 64-bit double to store
[[nodiscard]] auto store_float64(std::span<uint8_t> buffer, double value) noexcept -> StatusValue<size_t>;

/// Store an NTP timetag
/// @param buffer Destination buffer for big-endian output
/// @param value The NTP timetag to store
[[nodiscard]] auto store_timetag(std::span<uint8_t> buffer, NtpTimetag value) noexcept -> StatusValue<size_t>;

/// Store a null-terminated, 4-byte aligned string
/// @param buffer Destination buffer for padded string output
/// @param str The string to store
[[nodiscard]] auto store_string(std::span<uint8_t> buffer, std::string_view str) noexcept -> StatusValue<size_t>;

/// Store a blob (length prefix + data + padding)
/// @param buffer Destination buffer for blob output
/// @param data The binary data to store
[[nodiscard]] auto store_blob(std::span<uint8_t> buffer, std::span<uint8_t const> data) noexcept -> StatusValue<size_t>;

//
// Argument Serialization
//
/// Serialize an OSC argument to a buffer
/// @param buffer Destination buffer for serialized output
/// @param arg The OSC argument to serialize
[[nodiscard]] auto osc_serialize(std::span<uint8_t> buffer, OscArgument const& arg) noexcept -> StatusValue<size_t>;

//
// Message Serialization
//
/// Serialize an OSC message to a buffer
/// @param buffer Destination buffer for serialized output
/// @param msg The OSC message to serialize
[[nodiscard]] auto osc_serialize(std::span<uint8_t> buffer, OscMessage const& msg) -> StatusValue<size_t>;

//
// Bundle Serialization
//
/// Serialize an OSC bundle to a buffer
/// @param buffer Destination buffer for serialized output
/// @param bundle The OSC bundle to serialize
[[nodiscard]] auto osc_serialize(std::span<uint8_t> buffer, OscBundle const& bundle) -> StatusValue<size_t>;

}  // namespace statusbar::osc
