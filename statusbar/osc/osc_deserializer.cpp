// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/osc/osc_deserializer.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <variant>

namespace statusbar::osc {

auto osc_is_bundle(std::span<uint8_t const> data) noexcept -> bool
{
    if (data.size() < 8) {
        return false;
    }
    return span_compare(data.subspan(0, 8), make_const_span(OSC_BUNDLE_ID));
}

auto osc_is_message(std::span<uint8_t const> data) noexcept -> bool
{
    if (data.empty()) {
        return false;
    }
    return data[0] == '/';
}

// OSC integers/floats are big-endian on the wire. ieee::quadlet_t / octlet_t
// hold network byte order and convert to host on read; span_load fills their
// raw bytes. Same primitives the timetag path already uses.
auto load_int32(std::span<uint8_t const> buffer, int32_t& value) noexcept -> StatusValue<size_t>
{
    if (buffer.size() < 4) {
        return failure(OscError::buffer_underflow);
    }
    statusbar::ieee::quadlet_t q{};
    span_load(q, buffer.subspan(0, 4));
    value = static_cast<int32_t>(static_cast<uint32_t>(q));
    return success(size_t{4});
}

auto load_float32(std::span<uint8_t const> buffer, float& value) noexcept -> StatusValue<size_t>
{
    if (buffer.size() < 4) {
        return failure(OscError::buffer_underflow);
    }
    statusbar::ieee::quadlet_t q{};
    span_load(q, buffer.subspan(0, 4));
    value = std::bit_cast<float>(static_cast<uint32_t>(q));
    return success(size_t{4});
}

auto load_int64(std::span<uint8_t const> buffer, int64_t& value) noexcept -> StatusValue<size_t>
{
    if (buffer.size() < 8) {
        return failure(OscError::buffer_underflow);
    }
    statusbar::ieee::octlet_t q{};
    span_load(q, buffer.subspan(0, 8));
    value = static_cast<int64_t>(static_cast<uint64_t>(q));
    return success(size_t{8});
}

auto load_float64(std::span<uint8_t const> buffer, double& value) noexcept -> StatusValue<size_t>
{
    if (buffer.size() < 8) {
        return failure(OscError::buffer_underflow);
    }
    statusbar::ieee::octlet_t q{};
    span_load(q, buffer.subspan(0, 8));
    value = std::bit_cast<double>(static_cast<uint64_t>(q));
    return success(size_t{8});
}

auto load_timetag(std::span<uint8_t const> buffer, NtpTimetag& value) noexcept -> StatusValue<size_t>
{
    if (buffer.size() < 8) {
        return failure(OscError::buffer_underflow);
    }

    // Timetag is stored in network byte order (using quadlet_t)
    span_load(value.seconds, buffer.subspan(0, 4));
    span_load(value.fraction, buffer.subspan(4, 4));

    return success(size_t{8});
}

auto load_string(std::span<uint8_t const> buffer, std::string_view& value) noexcept -> StatusValue<size_t>
{
    if (buffer.empty()) {
        return failure(OscError::buffer_underflow);
    }

    // Find null terminator. memchr returns void*, and span::data() is
    // uint8_t const*; compute the offset in bytes to get the length.
    auto const* end = static_cast<uint8_t const*>(std::memchr(buffer.data(), '\0', buffer.size()));
    if (end == nullptr) {
        return failure(OscError::string_not_terminated);
    }

    auto const str_len = static_cast<size_t>(end - buffer.data());
    size_t const padded_size = osc_round_up(str_len + 1);  // +1 for null terminator

    if (buffer.size() < padded_size) {
        return failure(OscError::buffer_underflow);
    }

    value = as_string_view(buffer.first(str_len));
    return success(padded_size);
}

auto load_blob(std::span<uint8_t const> buffer, std::span<uint8_t const>& value) noexcept -> StatusValue<size_t>
{
    if (buffer.size() < 4) {
        return failure(OscError::buffer_underflow);
    }

    // Read length prefix
    int32_t len{};
    auto const result = load_int32(buffer, len);
    if (!result) {
        return result;
    }

    if (len < 0) {
        return failure(OscError::invalid_message);
    }

    size_t const blob_len = static_cast<size_t>(len);
    size_t const padded_size = osc_padded_blob_size(blob_len);

    if (buffer.size() < padded_size) {
        return failure(OscError::buffer_underflow);
    }

    value = buffer.subspan(4, blob_len);
    return success(padded_size);
}

auto osc_deserialize_argument(std::span<uint8_t const> buffer, char type_tag, size_t& bytes_consumed) noexcept
    -> StatusValue<OscArgument>
{
    bytes_consumed = 0;

    switch (type_tag) {
        case type_tag::int32: {
            int32_t val{};
            auto result = load_int32(buffer, val);
            if (!result) {
                return failure(result.error());
            }
            bytes_consumed = *result;
            return success(OscArgument::make_int32(val));
        }

        case type_tag::float32: {
            float val{};
            auto result = load_float32(buffer, val);
            if (!result) {
                return failure(result.error());
            }
            bytes_consumed = *result;
            return success(OscArgument::make_float(val));
        }

        case type_tag::float64: {
            double val{};
            auto result = load_float64(buffer, val);
            if (!result) {
                return failure(result.error());
            }
            bytes_consumed = *result;
            return success(OscArgument::make_double(val));
        }

        case type_tag::int64: {
            int64_t val{};
            auto result = load_int64(buffer, val);
            if (!result) {
                return failure(result.error());
            }
            bytes_consumed = *result;
            return success(OscArgument::make_int64(val));
        }

        case type_tag::string: {
            std::string_view val;
            auto result = load_string(buffer, val);
            if (!result) {
                return failure(result.error());
            }
            bytes_consumed = *result;
            return success(OscArgument::make_string(val));
        }

        case type_tag::blob: {
            std::span<uint8_t const> val;
            auto result = load_blob(buffer, val);
            if (!result) {
                return failure(result.error());
            }
            bytes_consumed = *result;
            return success(OscArgument::make_blob(val));
        }

        case type_tag::timetag: {
            NtpTimetag val{};
            auto result = load_timetag(buffer, val);
            if (!result) {
                return failure(result.error());
            }
            bytes_consumed = *result;
            return success(OscArgument::make_timetag(val));
        }

        case type_tag::avb_time: {
            NtpTimetag val{};
            auto result = load_timetag(buffer, val);
            if (!result) {
                return failure(result.error());
            }
            bytes_consumed = *result;
            return success(OscArgument::make_avb_timetag(val));
        }

        case type_tag::true_val:
            bytes_consumed = 0;
            return success(OscArgument::make_true());

        case type_tag::false_val:
            bytes_consumed = 0;
            return success(OscArgument::make_false());

        case type_tag::nil:
            bytes_consumed = 0;
            return success(OscArgument::make_nil());

        case type_tag::impulse:
            bytes_consumed = 0;
            return success(OscArgument::make_impulse());

        default:
            return failure(OscError::unsupported_type);
    }
}

auto osc_deserialize_message(std::span<uint8_t const> buffer, OscMessage& msg, size_t& bytes_consumed) -> Status
{
    bytes_consumed = 0;
    size_t pos = 0;

    // Clear the message but preserve allocated capacity
    msg.clear_arguments();

    // Read address pattern
    std::string_view address;
    auto result = load_string(buffer, address);
    if (!result) {
        return failure(result.error());
    }
    pos += *result;

    // Validate address starts with '/'
    if (address.empty() || address[0] != '/') {
        return failure(OscError::invalid_address);
    }

    // Set address (may allocate if new address is longer than existing capacity)
    msg.set_address(address);

    // Read type tag string
    std::string_view type_tags;
    result = load_string(buffer.subspan(pos), type_tags);
    if (!result) {
        return failure(result.error());
    }
    pos += *result;

    // Validate type tags start with ','
    if (type_tags.empty() || type_tags[0] != ',') {
        return failure(OscError::invalid_type_tag);
    }

    // Parse arguments (skip leading ',')
    for (size_t i = 1; i < type_tags.size(); ++i) {
        size_t arg_consumed{};
        auto arg_result = osc_deserialize_argument(buffer.subspan(pos), type_tags[i], arg_consumed);
        if (!arg_result) {
            return failure(arg_result.error());
        }
        msg.append(std::move(*arg_result));
        pos += arg_consumed;
    }

    bytes_consumed = pos;
    return success();
}

auto osc_deserialize_message(std::span<uint8_t const> buffer, size_t& bytes_consumed) -> StatusValue<OscMessage>
{
    OscMessage msg;
    auto status = osc_deserialize_message(buffer, msg, bytes_consumed);
    if (!status) {
        return failure(status.error());
    }
    return success(std::move(msg));
}

namespace {

auto deserialize_bundle_at_depth(std::span<uint8_t const> buffer, size_t& bytes_consumed, size_t depth)
    -> StatusValue<OscBundle>
{
    if (depth > OSC_MAX_BUNDLE_DEPTH) {
        return failure(OscError::bundle_too_deep);
    }

    bytes_consumed = 0;
    size_t pos = 0;

    // Check bundle identifier
    if (buffer.size() < 8) {
        return failure(OscError::buffer_underflow);
    }

    if (!span_compare(buffer.subspan(0, 8), make_const_span(OSC_BUNDLE_ID))) {
        return failure(OscError::invalid_bundle);
    }
    pos += 8;

    // Read timetag
    NtpTimetag timetag{};
    auto result = load_timetag(buffer.subspan(pos), timetag);
    if (!result) {
        return failure(result.error());
    }
    pos += *result;

    OscBundle bundle{timetag};

    // Parse elements
    while (pos < buffer.size()) {
        // Read element size
        int32_t elem_size{};
        result = load_int32(buffer.subspan(pos), elem_size);
        if (!result) {
            return failure(result.error());
        }
        pos += *result;

        if (elem_size <= 0 || static_cast<size_t>(elem_size) > buffer.size() - pos) {
            return failure(OscError::buffer_underflow);
        }

        // OSC data is 4-byte aligned: an element size that isn't a multiple of 4
        // desyncs alignment for every following element.
        if ((static_cast<size_t>(elem_size) % 4) != 0) {
            return failure(OscError::invalid_alignment);
        }

        auto const elem_data = buffer.subspan(pos, static_cast<size_t>(elem_size));

        // Determine if element is a message or nested bundle
        if (osc_is_bundle(elem_data)) {
            size_t elem_consumed{};
            auto bundle_result = deserialize_bundle_at_depth(elem_data, elem_consumed, depth + 1);
            if (!bundle_result) {
                return failure(bundle_result.error());
            }
            bundle.append(std::move(*bundle_result));
        } else if (osc_is_message(elem_data)) {
            size_t elem_consumed{};
            auto msg_result = osc_deserialize_message(elem_data, elem_consumed);
            if (!msg_result) {
                return failure(msg_result.error());
            }
            bundle.append(std::move(*msg_result));
        } else {
            return failure(OscError::invalid_message);
        }

        pos += static_cast<size_t>(elem_size);
    }

    bytes_consumed = pos;
    return success(std::move(bundle));
}

}  // namespace

auto osc_deserialize_bundle(std::span<uint8_t const> buffer, size_t& bytes_consumed) -> StatusValue<OscBundle>
{
    return deserialize_bundle_at_depth(buffer, bytes_consumed, 0);
}

auto osc_parse(std::span<uint8_t const> buffer) -> StatusValue<std::variant<OscMessage, OscBundle>>
{
    using ResultType = std::variant<OscMessage, OscBundle>;

    if (buffer.empty()) {
        return failure(OscError::buffer_underflow);
    }

    if (osc_is_bundle(buffer)) {
        size_t consumed{};
        auto result = osc_deserialize_bundle(buffer, consumed);
        if (!result) {
            return failure(result.error());
        }
        return success(ResultType{std::move(*result)});
    }
    if (osc_is_message(buffer)) {
        size_t consumed{};
        auto result = osc_deserialize_message(buffer, consumed);
        if (!result) {
            return failure(result.error());
        }
        return success(ResultType{std::move(*result)});
    }
    return failure(OscError::invalid_message);
}

}  // namespace statusbar::osc
