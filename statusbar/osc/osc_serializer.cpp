// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/osc/osc_serializer.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <variant>

namespace statusbar::osc {

auto store_int32(std::span<uint8_t> buffer, int32_t value) noexcept -> StatusValue<size_t>
{
    if (buffer.size() < 4) {
        return failure(OscError::buffer_overflow);
    }

    auto const u = static_cast<uint32_t>(value);
    buffer[0] = static_cast<uint8_t>((u >> 24) & 0xFF);
    buffer[1] = static_cast<uint8_t>((u >> 16) & 0xFF);
    buffer[2] = static_cast<uint8_t>((u >> 8) & 0xFF);
    buffer[3] = static_cast<uint8_t>(u & 0xFF);

    return success(size_t{4});
}

auto store_float32(std::span<uint8_t> buffer, float value) noexcept -> StatusValue<size_t>
{
    if (buffer.size() < 4) {
        return failure(OscError::buffer_overflow);
    }

    uint32_t const bits = std::bit_cast<uint32_t>(value);

    buffer[0] = static_cast<uint8_t>((bits >> 24) & 0xFF);
    buffer[1] = static_cast<uint8_t>((bits >> 16) & 0xFF);
    buffer[2] = static_cast<uint8_t>((bits >> 8) & 0xFF);
    buffer[3] = static_cast<uint8_t>(bits & 0xFF);

    return success(size_t{4});
}

auto store_int64(std::span<uint8_t> buffer, int64_t value) noexcept -> StatusValue<size_t>
{
    if (buffer.size() < 8) {
        return failure(OscError::buffer_overflow);
    }

    auto const u = static_cast<uint64_t>(value);
    buffer[0] = static_cast<uint8_t>((u >> 56) & 0xFF);
    buffer[1] = static_cast<uint8_t>((u >> 48) & 0xFF);
    buffer[2] = static_cast<uint8_t>((u >> 40) & 0xFF);
    buffer[3] = static_cast<uint8_t>((u >> 32) & 0xFF);
    buffer[4] = static_cast<uint8_t>((u >> 24) & 0xFF);
    buffer[5] = static_cast<uint8_t>((u >> 16) & 0xFF);
    buffer[6] = static_cast<uint8_t>((u >> 8) & 0xFF);
    buffer[7] = static_cast<uint8_t>(u & 0xFF);

    return success(size_t{8});
}

auto store_float64(std::span<uint8_t> buffer, double value) noexcept -> StatusValue<size_t>
{
    if (buffer.size() < 8) {
        return failure(OscError::buffer_overflow);
    }

    uint64_t const bits = std::bit_cast<uint64_t>(value);

    buffer[0] = static_cast<uint8_t>((bits >> 56) & 0xFF);
    buffer[1] = static_cast<uint8_t>((bits >> 48) & 0xFF);
    buffer[2] = static_cast<uint8_t>((bits >> 40) & 0xFF);
    buffer[3] = static_cast<uint8_t>((bits >> 32) & 0xFF);
    buffer[4] = static_cast<uint8_t>((bits >> 24) & 0xFF);
    buffer[5] = static_cast<uint8_t>((bits >> 16) & 0xFF);
    buffer[6] = static_cast<uint8_t>((bits >> 8) & 0xFF);
    buffer[7] = static_cast<uint8_t>(bits & 0xFF);

    return success(size_t{8});
}

auto store_timetag(std::span<uint8_t> buffer, NtpTimetag value) noexcept -> StatusValue<size_t>
{
    if (buffer.size() < 8) {
        return failure(OscError::buffer_overflow);
    }

    // Timetag is already in network byte order (using quadlet_t)
    span_store(buffer.subspan(0, 4), value.seconds);
    span_store(buffer.subspan(4, 4), value.fraction);

    return success(size_t{8});
}

auto store_string(std::span<uint8_t> buffer, std::string_view str) noexcept -> StatusValue<size_t>
{
    size_t const padded_size = osc_padded_string_size(str.size());

    if (buffer.size() < padded_size) {
        return failure(OscError::buffer_overflow);
    }

    // Copy string content
    span_copy(buffer.subspan(0, str.size()), make_const_span(str));

    // Fill remaining bytes with zeros (null terminator + padding)
    span_zero(buffer.subspan(str.size(), padded_size - str.size()));

    return success(padded_size);
}

auto store_blob(std::span<uint8_t> buffer, std::span<uint8_t const> data) noexcept -> StatusValue<size_t>
{
    size_t const padded_size = osc_padded_blob_size(data.size());

    if (buffer.size() < padded_size) {
        return failure(OscError::buffer_overflow);
    }

    // Store length prefix (big-endian 32-bit)
    auto const len = static_cast<int32_t>(data.size());
    auto result = store_int32(buffer, len);
    if (!result) {
        return result;
    }

    // Copy blob data
    span_copy(buffer.subspan(4, data.size()), data);

    // Pad with zeros
    size_t const pad_bytes = padded_size - 4 - data.size();
    if (pad_bytes > 0) {
        span_zero(buffer.subspan(4 + data.size(), pad_bytes));
    }

    return success(padded_size);
}

auto osc_serialize(std::span<uint8_t> buffer, OscArgument const& arg) noexcept -> StatusValue<size_t>
{
    switch (arg.get_type_tag()) {
        case type_tag::int32: {
            auto val = arg.as_int32();
            if (!val) {
                return failure(val.error());
            }
            return store_int32(buffer, *val);
        }

        case type_tag::float32: {
            auto val = arg.as_float();
            if (!val) {
                return failure(val.error());
            }
            return store_float32(buffer, *val);
        }

        case type_tag::float64: {
            auto val = arg.as_double();
            if (!val) {
                return failure(val.error());
            }
            return store_float64(buffer, *val);
        }

        case type_tag::int64: {
            auto val = arg.as_int64();
            if (!val) {
                return failure(val.error());
            }
            return store_int64(buffer, *val);
        }

        case type_tag::string: {
            auto val = arg.as_string();
            if (!val) {
                return failure(val.error());
            }
            return store_string(buffer, *val);
        }

        case type_tag::blob: {
            auto val = arg.as_blob();
            if (!val) {
                return failure(val.error());
            }
            return store_blob(buffer, *val);
        }

        case type_tag::timetag:
        case type_tag::avb_time: {
            auto val = arg.as_timetag();
            if (!val) {
                return failure(val.error());
            }
            return store_timetag(buffer, *val);
        }

        case type_tag::true_val:
        case type_tag::false_val:
        case type_tag::nil:
        case type_tag::impulse:
            // No data bytes for these types
            return success(size_t{0});

        default:
            return failure(OscError::unsupported_type);
    }
}

auto osc_serialize(std::span<uint8_t> buffer, OscMessage const& msg) -> StatusValue<size_t>
{
    if (!msg.is_valid()) {
        return failure(OscError::invalid_address);
    }

    size_t pos = 0;

    // Serialize address pattern
    auto result = store_string(buffer, msg.address());
    if (!result) {
        return result;
    }
    pos += *result;

    // Serialize type tag string
    auto const type_tags = msg.type_tags();
    result = store_string(buffer.subspan(pos), type_tags);
    if (!result) {
        return result;
    }
    pos += *result;

    // Serialize arguments
    for (auto const& arg : msg.arguments()) {
        result = osc_serialize(buffer.subspan(pos), arg);
        if (!result) {
            return result;
        }
        pos += *result;
    }

    return success(pos);
}

auto osc_serialize(std::span<uint8_t> buffer, OscBundle const& bundle) -> StatusValue<size_t>
{
    size_t pos = 0;

    // Write bundle identifier "#bundle\0"
    if (buffer.size() < 8) {
        return failure(OscError::buffer_overflow);
    }
    span_copy(buffer.subspan(0, 8), make_const_span(OSC_BUNDLE_ID));
    pos += 8;

    // Write timetag
    auto result = store_timetag(buffer.subspan(pos), bundle.timetag());
    if (!result) {
        return result;
    }
    pos += *result;

    // Write elements
    for (auto const& elem : bundle.elements()) {
        // Calculate element size
        size_t const elem_size = std::visit([](auto const& e) -> size_t { return e.wire_size(); }, elem);

        // Write size prefix
        result = store_int32(buffer.subspan(pos), static_cast<int32_t>(elem_size));
        if (!result) {
            return result;
        }
        pos += *result;

        // Write element data
        auto const serialize_result = std::visit(
            [&buffer, pos](auto const& e) -> StatusValue<size_t> { return osc_serialize(buffer.subspan(pos), e); }, elem);

        if (!serialize_result) {
            return serialize_result;
        }
        pos += *serialize_result;
    }

    return success(pos);
}

}  // namespace statusbar::osc
