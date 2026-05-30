#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// OSC - Open Sound Control Protocol Module
/// Based on OSC 1.0 specification
///
/// This module provides a modern C++23 implementation of the Open Sound Control
/// protocol. OSC is a content format for messaging among computers, sound
/// synthesizers, and other multimedia devices that is optimized for modern
/// networking technology.
///
/// Module Structure:
/// - :error       - Error types and error category
/// - :base        - Base types (NtpTimetag, constants, type tags)
/// - :argument    - OscArgument class with variant storage
/// - :message     - OscMessage class
/// - :bundle      - OscBundle class with nested elements
/// - :serializer  - Serialization functions
/// - :deserializer - Deserialization functions
///
/// Example usage:
/// @code
/// import statusbar.osc;
///
/// // Create a message
/// statusbar::osc::OscMessage msg{"/synth/frequency"};
/// msg.append_float(440.0f);
/// msg.append_string("sine");
///
/// // Serialize to buffer
/// std::array<uint8_t, 256> buffer{};
/// auto result = statusbar::osc::osc_serialize(buffer, msg);
/// if (result) {
///     // buffer now contains the OSC message wire format
///     size_t bytes_written = *result;
/// }
///
/// // Deserialize a message
/// size_t consumed{};
/// auto msg_result = statusbar::osc::osc_deserialize_message(buffer, consumed);
/// if (msg_result) {
///     auto const& parsed = *msg_result;
///     // Access: parsed.address(), parsed.argument(0)->as_float(), etc.
/// }
/// @endcode

#include "statusbar/osc/osc_argument.hpp"
#include "statusbar/osc/osc_base.hpp"
#include "statusbar/osc/osc_bundle.hpp"
#include "statusbar/osc/osc_deserializer.hpp"
#include "statusbar/osc/osc_error.hpp"
#include "statusbar/osc/osc_message.hpp"
#include "statusbar/osc/osc_serializer.hpp"
