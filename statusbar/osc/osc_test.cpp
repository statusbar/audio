// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// OSC Unit Tests

#include "statusbar/osc/osc.hpp"

#include "statusbar/buffer/buffer.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

using namespace statusbar::osc;
using namespace statusbar;

//
// Static assertions for constexpr verification
//
// OSC type tag constants
static_assert(type_tag::int32 == 'i');
static_assert(type_tag::float32 == 'f');
static_assert(type_tag::string == 's');
static_assert(type_tag::blob == 'b');
static_assert(type_tag::int64 == 'h');
static_assert(type_tag::timetag == 't');
static_assert(type_tag::float64 == 'd');
static_assert(type_tag::true_val == 'T');
static_assert(type_tag::false_val == 'F');
static_assert(type_tag::nil == 'N');
static_assert(type_tag::impulse == 'I');
static_assert(type_tag::avb_time == 'a');

// OSC constants
static_assert(OSC_BUNDLE_ID[0] == '#');
static_assert(OSC_BUNDLE_ID[1] == 'b');
static_assert(OSC_BUNDLE_ID[7] == '\0');
static_assert(OSC_MAX_ADDRESS_LENGTH == 256);
static_assert(OSC_MAX_TYPETAGS == 128);

// NtpTimetag constexpr verification
static_assert(NtpTimetag::LENGTH == 8);
static_assert(sizeof(NtpTimetag) == 8);

// NtpTimetag::immediate() - constexpr verification
static_assert([]() constexpr {
    auto tt = NtpTimetag::immediate();
    return tt.get_seconds() == 0 && tt.get_fraction() == 1;
}());

// NtpTimetag::is_immediate() - constexpr verification
static_assert([]() constexpr {
    auto tt = NtpTimetag::immediate();
    return tt.is_immediate() && !tt.is_zero();
}());

// NtpTimetag::is_zero() - constexpr verification
static_assert([]() constexpr {
    NtpTimetag tt{};
    return tt.is_zero() && !tt.is_immediate();
}());

// NtpTimetag construction and getters - constexpr verification
static_assert([]() constexpr {
    NtpTimetag tt{100, 200};
    return tt.get_seconds() == 100 && tt.get_fraction() == 200;
}());

// NtpTimetag setters - constexpr verification
static_assert([]() constexpr {
    NtpTimetag tt{};
    tt.set_seconds(42);
    tt.set_fraction(99);
    return tt.get_seconds() == 42 && tt.get_fraction() == 99;
}());

// osc_round_up() - constexpr verification
static_assert(osc_round_up(0) == 0);
static_assert(osc_round_up(1) == 4);
static_assert(osc_round_up(2) == 4);
static_assert(osc_round_up(3) == 4);
static_assert(osc_round_up(4) == 4);
static_assert(osc_round_up(5) == 8);
static_assert(osc_round_up(8) == 8);
static_assert(osc_round_up(9) == 12);

// osc_padded_string_size() - constexpr verification
static_assert(osc_padded_string_size(0) == 4);   // Empty + null = 1 -> 4
static_assert(osc_padded_string_size(1) == 4);   // 1 char + null = 2 -> 4
static_assert(osc_padded_string_size(3) == 4);   // 3 chars + null = 4 -> 4
static_assert(osc_padded_string_size(4) == 8);   // 4 chars + null = 5 -> 8
static_assert(osc_padded_string_size(7) == 8);   // 7 chars + null = 8 -> 8
static_assert(osc_padded_string_size(8) == 12);  // 8 chars + null = 9 -> 12

// osc_padded_blob_size() - constexpr verification
static_assert(osc_padded_blob_size(0) == 4);   // 4 (length) + 0 = 4
static_assert(osc_padded_blob_size(1) == 8);   // 4 (length) + 4 (padded) = 8
static_assert(osc_padded_blob_size(4) == 8);   // 4 (length) + 4 = 8
static_assert(osc_padded_blob_size(5) == 12);  // 4 (length) + 8 (padded) = 12

//
// Base Types Tests
//
TEST(osc_timetag, immediate)
{
    auto const tt = NtpTimetag::immediate();
    EXPECT_TRUE(tt.is_immediate());
    EXPECT_FALSE(tt.is_zero());
    EXPECT_EQ(tt.get_seconds(), 0U);
    EXPECT_EQ(tt.get_fraction(), 1U);
}

TEST(osc_timetag, zero)
{
    NtpTimetag tt{};
    EXPECT_TRUE(tt.is_zero());
    EXPECT_FALSE(tt.is_immediate());
    EXPECT_EQ(tt.get_seconds(), 0U);
    EXPECT_EQ(tt.get_fraction(), 0U);
}

TEST(osc_timetag, custom_values)
{
    NtpTimetag tt{0x12345678, 0xABCDEF00};
    EXPECT_FALSE(tt.is_zero());
    EXPECT_FALSE(tt.is_immediate());
    EXPECT_EQ(tt.get_seconds(), 0x12345678U);
    EXPECT_EQ(tt.get_fraction(), 0xABCDEF00U);
}

TEST(osc_timetag, setters)
{
    NtpTimetag tt{};
    tt.set_seconds(100);
    tt.set_fraction(200);
    EXPECT_EQ(tt.get_seconds(), 100U);
    EXPECT_EQ(tt.get_fraction(), 200U);
}

TEST(osc_timetag, comparison)
{
    NtpTimetag tt1{100, 200};
    NtpTimetag tt2{100, 200};
    NtpTimetag tt3{100, 300};
    NtpTimetag tt4{200, 100};

    EXPECT_TRUE(tt1 == tt2);
    EXPECT_TRUE(tt1 < tt3);
    EXPECT_TRUE(tt1 < tt4);
    EXPECT_TRUE(tt3 < tt4);
}

//
// Alignment Helpers Tests
//
TEST(osc_alignment, round_up)
{
    EXPECT_EQ(osc_round_up(0), 0U);
    EXPECT_EQ(osc_round_up(1), 4U);
    EXPECT_EQ(osc_round_up(2), 4U);
    EXPECT_EQ(osc_round_up(3), 4U);
    EXPECT_EQ(osc_round_up(4), 4U);
    EXPECT_EQ(osc_round_up(5), 8U);
    EXPECT_EQ(osc_round_up(8), 8U);
    EXPECT_EQ(osc_round_up(9), 12U);
}

TEST(osc_alignment, padded_string_size)
{
    // Empty string: 1 null byte -> 4 bytes
    EXPECT_EQ(osc_padded_string_size(0), 4U);
    // 1 char + null = 2 -> 4 bytes
    EXPECT_EQ(osc_padded_string_size(1), 4U);
    // 3 chars + null = 4 -> 4 bytes
    EXPECT_EQ(osc_padded_string_size(3), 4U);
    // 4 chars + null = 5 -> 8 bytes
    EXPECT_EQ(osc_padded_string_size(4), 8U);
    // 7 chars + null = 8 -> 8 bytes
    EXPECT_EQ(osc_padded_string_size(7), 8U);
    // 8 chars + null = 9 -> 12 bytes
    EXPECT_EQ(osc_padded_string_size(8), 12U);
}

TEST(osc_alignment, padded_blob_size)
{
    // 0 bytes data: 4 (length) + 0 = 4
    EXPECT_EQ(osc_padded_blob_size(0), 4U);
    // 1 byte data: 4 (length) + 4 (padded) = 8
    EXPECT_EQ(osc_padded_blob_size(1), 8U);
    // 4 bytes data: 4 (length) + 4 = 8
    EXPECT_EQ(osc_padded_blob_size(4), 8U);
    // 5 bytes data: 4 (length) + 8 (padded) = 12
    EXPECT_EQ(osc_padded_blob_size(5), 12U);
}

//
// Argument Tests
//
TEST(osc_argument, int32)
{
    auto arg = OscArgument::make_int32(42);
    EXPECT_EQ(arg.get_type_tag(), type_tag::int32);
    EXPECT_TRUE(arg.has_data());
    EXPECT_TRUE(arg.is_numeric());
    EXPECT_EQ(arg.wire_size(), 4U);

    auto val = arg.as_int32();
    EXPECT_TRUE(val.has_value());
    EXPECT_EQ(*val, 42);

    // Type mismatch
    auto bad = arg.as_float();
    EXPECT_FALSE(bad.has_value());
}

TEST(osc_argument, float32)
{
    auto arg = OscArgument::make_float(3.14f);
    EXPECT_EQ(arg.get_type_tag(), type_tag::float32);
    EXPECT_TRUE(arg.has_data());
    EXPECT_TRUE(arg.is_numeric());
    EXPECT_EQ(arg.wire_size(), 4U);

    auto val = arg.as_float();
    EXPECT_TRUE(val.has_value());
    EXPECT_TRUE(std::abs(*val - 3.14f) < 0.001f);
}

TEST(osc_argument, float64)
{
    auto arg = OscArgument::make_double(3.14159265359);
    EXPECT_EQ(arg.get_type_tag(), type_tag::float64);
    EXPECT_EQ(arg.wire_size(), 8U);

    auto val = arg.as_double();
    EXPECT_TRUE(val.has_value());
    EXPECT_TRUE(std::abs(*val - 3.14159265359) < 0.0000001);
}

TEST(osc_argument, int64)
{
    auto arg = OscArgument::make_int64(0x123456789ABCDEF0LL);
    EXPECT_EQ(arg.get_type_tag(), type_tag::int64);
    EXPECT_EQ(arg.wire_size(), 8U);

    auto val = arg.as_int64();
    EXPECT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0x123456789ABCDEF0LL);
}

TEST(osc_argument, string)
{
    auto arg = OscArgument::make_string("hello");
    EXPECT_EQ(arg.get_type_tag(), type_tag::string);
    EXPECT_TRUE(arg.has_data());
    EXPECT_FALSE(arg.is_numeric());
    // "hello" = 5 chars + null = 6, padded to 8
    EXPECT_EQ(arg.wire_size(), 8U);

    auto val = arg.as_string();
    EXPECT_TRUE(val.has_value());
    EXPECT_EQ(*val, "hello");
}

TEST(osc_argument, blob)
{
    std::array<uint8_t, 5> data{1, 2, 3, 4, 5};
    auto arg = OscArgument::make_blob(data);
    EXPECT_EQ(arg.get_type_tag(), type_tag::blob);
    // 4 (length) + 8 (5 bytes padded) = 12
    EXPECT_EQ(arg.wire_size(), 12U);

    auto val = arg.as_blob();
    EXPECT_TRUE(val.has_value());
    EXPECT_EQ(val->size(), 5U);
    EXPECT_EQ((*val)[0], 1);
    EXPECT_EQ((*val)[4], 5);
}

TEST(osc_argument, timetag)
{
    NtpTimetag tt{100, 200};
    auto arg = OscArgument::make_timetag(tt);
    EXPECT_EQ(arg.get_type_tag(), type_tag::timetag);
    EXPECT_EQ(arg.wire_size(), 8U);

    auto val = arg.as_timetag();
    EXPECT_TRUE(val.has_value());
    EXPECT_EQ(val->get_seconds(), 100U);
    EXPECT_EQ(val->get_fraction(), 200U);
}

TEST(osc_argument, true_val)
{
    auto arg = OscArgument::make_true();
    EXPECT_EQ(arg.get_type_tag(), type_tag::true_val);
    EXPECT_FALSE(arg.has_data());
    EXPECT_EQ(arg.wire_size(), 0U);

    auto val = arg.as_bool();
    EXPECT_TRUE(val.has_value());
    EXPECT_TRUE(*val);
}

TEST(osc_argument, false_val)
{
    auto arg = OscArgument::make_false();
    EXPECT_EQ(arg.get_type_tag(), type_tag::false_val);
    EXPECT_FALSE(arg.has_data());
    EXPECT_EQ(arg.wire_size(), 0U);

    auto val = arg.as_bool();
    EXPECT_TRUE(val.has_value());
    EXPECT_FALSE(*val);
}

TEST(osc_argument, nil)
{
    auto arg = OscArgument::make_nil();
    EXPECT_EQ(arg.get_type_tag(), type_tag::nil);
    EXPECT_FALSE(arg.has_data());
    EXPECT_EQ(arg.wire_size(), 0U);
}

TEST(osc_argument, impulse)
{
    auto arg = OscArgument::make_impulse();
    EXPECT_EQ(arg.get_type_tag(), type_tag::impulse);
    EXPECT_FALSE(arg.has_data());
    EXPECT_EQ(arg.wire_size(), 0U);
}

//
// Message Tests
//
TEST(osc_message, default_constructor)
{
    OscMessage msg;
    EXPECT_EQ(msg.address(), "/");
    EXPECT_TRUE(msg.has_valid_address());
    EXPECT_EQ(msg.argument_count(), 0U);
    EXPECT_FALSE(msg.has_arguments());
}

TEST(osc_message, with_address)
{
    OscMessage msg{"/synth/frequency"};
    EXPECT_EQ(msg.address(), "/synth/frequency");
    EXPECT_TRUE(msg.has_valid_address());
    EXPECT_TRUE(msg.is_valid());
}

TEST(osc_message, invalid_address)
{
    OscMessage msg{"synth/frequency"};  // Missing leading '/'
    EXPECT_FALSE(msg.has_valid_address());
    EXPECT_FALSE(msg.is_valid());
}

TEST(osc_message, append_arguments)
{
    OscMessage msg{"/test"};
    msg.append_int32(42);
    msg.append_float(3.14f);
    msg.append_string("hello");

    EXPECT_EQ(msg.argument_count(), 3U);
    EXPECT_TRUE(msg.has_arguments());

    auto const* arg0 = msg.argument(0);
    EXPECT_TRUE(arg0 != nullptr);
    EXPECT_EQ(arg0->get_type_tag(), type_tag::int32);

    auto const* arg1 = msg.argument(1);
    EXPECT_TRUE(arg1 != nullptr);
    EXPECT_EQ(arg1->get_type_tag(), type_tag::float32);

    auto const* arg2 = msg.argument(2);
    EXPECT_TRUE(arg2 != nullptr);
    EXPECT_EQ(arg2->get_type_tag(), type_tag::string);

    // Out of range
    EXPECT_TRUE(msg.argument(3) == nullptr);
}

TEST(osc_message, type_tags)
{
    OscMessage msg{"/test"};
    msg.append_int32(1);
    msg.append_float(2.0f);
    msg.append_string("test");
    msg.append_true();
    msg.append_nil();

    auto tags = msg.type_tags();
    EXPECT_EQ(tags, ",ifsTN");
}

TEST(osc_message, wire_size)
{
    OscMessage msg{"/a"};  // 2 chars + null = 3 -> 4 bytes
    // Type tags ",i" = 2 chars + null = 3 -> 4 bytes
    msg.append_int32(42);  // 4 bytes

    // Total: 4 + 4 + 4 = 12 bytes
    EXPECT_EQ(msg.wire_size(), 12U);
}

TEST(osc_message, clear_arguments)
{
    OscMessage msg{"/test"};
    msg.append_int32(1);
    msg.append_int32(2);
    EXPECT_EQ(msg.argument_count(), 2U);

    msg.clear_arguments();
    EXPECT_EQ(msg.argument_count(), 0U);
    EXPECT_FALSE(msg.has_arguments());
}

//
// Bundle Tests
//
TEST(osc_bundle, default_constructor)
{
    OscBundle bundle;
    EXPECT_TRUE(bundle.is_immediate());
    EXPECT_EQ(bundle.element_count(), 0U);
    EXPECT_FALSE(bundle.has_elements());
}

TEST(osc_bundle, with_timetag)
{
    NtpTimetag tt{100, 200};
    OscBundle bundle{tt};
    EXPECT_FALSE(bundle.is_immediate());
    EXPECT_EQ(bundle.timetag().get_seconds(), 100U);
}

TEST(osc_bundle, append_messages)
{
    OscBundle bundle;

    OscMessage msg1{"/a"};
    msg1.append_int32(1);
    bundle.append(std::move(msg1));

    OscMessage msg2{"/b"};
    msg2.append_float(2.0f);
    bundle.append(std::move(msg2));

    EXPECT_EQ(bundle.element_count(), 2U);
    EXPECT_TRUE(bundle.has_elements());

    auto const* elem0 = bundle.element(0);
    EXPECT_TRUE(elem0 != nullptr);
    EXPECT_TRUE(std::holds_alternative<OscMessage>(*elem0));

    EXPECT_TRUE(bundle.element(2) == nullptr);
}

TEST(osc_bundle, nested_bundle)
{
    OscBundle inner;
    OscMessage msg{"/inner"};
    inner.append(std::move(msg));

    OscBundle outer;
    outer.append(std::move(inner));

    EXPECT_EQ(outer.element_count(), 1U);
    auto const* elem = outer.element(0);
    EXPECT_TRUE(std::holds_alternative<OscBundle>(*elem));
}

TEST(osc_bundle, wire_size)
{
    OscBundle bundle;
    // Header: "#bundle\0" (8) + timetag (8) = 16 bytes
    EXPECT_EQ(bundle.wire_size(), 16U);

    OscMessage msg{"/a"};  // 4 bytes address, 4 bytes type tags = 8 bytes
    bundle.append(std::move(msg));
    // Header (16) + size prefix (4) + message (8) = 28 bytes
    EXPECT_EQ(bundle.wire_size(), 28U);
}

//
// Serialization Tests
//
TEST(osc_serialize, simple_message)
{
    OscMessage msg{"/test"};
    msg.append_int32(42);
    msg.append_float(3.14f);

    std::array<uint8_t, 256> buffer{};
    auto result = osc_serialize(buffer, msg);

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, msg.wire_size());

    // Verify address pattern
    EXPECT_EQ(buffer[0], '/');
    EXPECT_EQ(buffer[1], 't');
    EXPECT_EQ(buffer[2], 'e');
    EXPECT_EQ(buffer[3], 's');
    EXPECT_EQ(buffer[4], 't');
    EXPECT_EQ(buffer[5], '\0');
    EXPECT_EQ(buffer[6], '\0');
    EXPECT_EQ(buffer[7], '\0');

    // Verify type tags
    EXPECT_EQ(buffer[8], ',');
    EXPECT_EQ(buffer[9], 'i');
    EXPECT_EQ(buffer[10], 'f');
    EXPECT_EQ(buffer[11], '\0');

    // Verify int32 (big-endian 42 = 0x0000002A)
    EXPECT_EQ(buffer[12], 0x00);
    EXPECT_EQ(buffer[13], 0x00);
    EXPECT_EQ(buffer[14], 0x00);
    EXPECT_EQ(buffer[15], 0x2A);
}

TEST(osc_serialize, string_argument)
{
    OscMessage msg{"/s"};
    msg.append_string("hi");

    std::array<uint8_t, 64> buffer{};
    auto result = osc_serialize(buffer, msg);
    EXPECT_TRUE(result.has_value());

    // Address "/s" + null = 3 -> 4 bytes
    // Type tags ",s" + null = 3 -> 4 bytes
    // String "hi" + null = 3 -> 4 bytes
    EXPECT_EQ(*result, 12U);
}

TEST(osc_serialize, blob_argument)
{
    OscMessage msg{"/b"};
    std::array<uint8_t, 3> data{0xAA, 0xBB, 0xCC};
    msg.append_blob(data);

    std::array<uint8_t, 64> buffer{};
    auto result = osc_serialize(buffer, msg);
    EXPECT_TRUE(result.has_value());

    // Verify blob length (big-endian 3)
    size_t const blob_offset = 8;  // After address (4) and type tags (4)
    EXPECT_EQ(buffer[blob_offset], 0x00);
    EXPECT_EQ(buffer[blob_offset + 1], 0x00);
    EXPECT_EQ(buffer[blob_offset + 2], 0x00);
    EXPECT_EQ(buffer[blob_offset + 3], 0x03);

    // Verify blob data
    EXPECT_EQ(buffer[blob_offset + 4], 0xAA);
    EXPECT_EQ(buffer[blob_offset + 5], 0xBB);
    EXPECT_EQ(buffer[blob_offset + 6], 0xCC);
}

TEST(osc_serialize, bundle)
{
    OscBundle bundle;

    OscMessage msg{"/a"};
    msg.append_int32(1);
    bundle.append(std::move(msg));

    std::array<uint8_t, 256> buffer{};
    auto result = osc_serialize(buffer, bundle);
    EXPECT_TRUE(result.has_value());

    // Verify bundle identifier
    EXPECT_TRUE(span_compare(make_const_span(buffer).first(7), make_const_span(std::string_view{"#bundle"})));
    EXPECT_EQ(buffer[7], '\0');

    // Verify immediate timetag (0x0000000000000001)
    EXPECT_EQ(buffer[8], 0x00);
    EXPECT_EQ(buffer[9], 0x00);
    EXPECT_EQ(buffer[10], 0x00);
    EXPECT_EQ(buffer[11], 0x00);
    EXPECT_EQ(buffer[12], 0x00);
    EXPECT_EQ(buffer[13], 0x00);
    EXPECT_EQ(buffer[14], 0x00);
    EXPECT_EQ(buffer[15], 0x01);
}

TEST(osc_serialize, buffer_overflow)
{
    OscMessage msg{"/test"};
    msg.append_int32(42);

    std::array<uint8_t, 4> small_buffer{};  // Too small
    auto result = osc_serialize(small_buffer, msg);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), make_error_code(OscError::buffer_overflow));
}

//
// Deserialization Tests
//
TEST(osc_deserialize, simple_message)
{
    // Serialize first
    OscMessage original{"/test"};
    original.append_int32(42);
    original.append_float(3.14f);
    original.append_string("hello");

    std::array<uint8_t, 256> buffer{};
    auto ser_result = osc_serialize(buffer, original);
    EXPECT_TRUE(ser_result.has_value());

    // Deserialize
    size_t consumed{};
    auto result = osc_deserialize_message(buffer, consumed);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(consumed, *ser_result);

    auto const& msg = *result;
    EXPECT_EQ(msg.address(), "/test");
    EXPECT_EQ(msg.argument_count(), 3U);

    auto i = msg.argument(0)->as_int32();
    EXPECT_TRUE(i.has_value());
    EXPECT_EQ(*i, 42);

    auto f = msg.argument(1)->as_float();
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(std::abs(*f - 3.14f) < 0.001f);

    auto s = msg.argument(2)->as_string();
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(*s, "hello");
}

TEST(osc_deserialize, blob_argument)
{
    OscMessage original{"/b"};
    std::array<uint8_t, 5> data{1, 2, 3, 4, 5};
    original.append_blob(data);

    std::array<uint8_t, 64> buffer{};
    auto ser_result = osc_serialize(buffer, original);
    EXPECT_TRUE(ser_result.has_value());

    size_t consumed{};
    auto result = osc_deserialize_message(buffer, consumed);
    EXPECT_TRUE(result.has_value());

    auto blob = result->argument(0)->as_blob();
    EXPECT_TRUE(blob.has_value());
    EXPECT_EQ(blob->size(), 5U);
    EXPECT_EQ((*blob)[0], 1);
    EXPECT_EQ((*blob)[4], 5);
}

TEST(osc_deserialize, no_data_types)
{
    OscMessage original{"/t"};
    original.append_true();
    original.append_false();
    original.append_nil();
    original.append_impulse();

    std::array<uint8_t, 64> buffer{};
    auto ser_result = osc_serialize(buffer, original);
    EXPECT_TRUE(ser_result.has_value());

    size_t consumed{};
    auto result = osc_deserialize_message(buffer, consumed);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->argument_count(), 4U);

    EXPECT_EQ(result->argument(0)->get_type_tag(), type_tag::true_val);
    EXPECT_EQ(result->argument(1)->get_type_tag(), type_tag::false_val);
    EXPECT_EQ(result->argument(2)->get_type_tag(), type_tag::nil);
    EXPECT_EQ(result->argument(3)->get_type_tag(), type_tag::impulse);
}

TEST(osc_deserialize, timetag)
{
    OscMessage original{"/t"};
    NtpTimetag tt{0x12345678, 0xABCDEF00};
    original.append_timetag(tt);

    std::array<uint8_t, 64> buffer{};
    auto ser_result = osc_serialize(buffer, original);
    EXPECT_TRUE(ser_result.has_value());

    size_t consumed{};
    auto result = osc_deserialize_message(buffer, consumed);
    EXPECT_TRUE(result.has_value());

    auto t = result->argument(0)->as_timetag();
    EXPECT_TRUE(t.has_value());
    EXPECT_EQ(t->get_seconds(), 0x12345678U);
    EXPECT_EQ(t->get_fraction(), 0xABCDEF00U);
}

TEST(osc_deserialize, bundle)
{
    OscBundle original;
    original.set_timetag(NtpTimetag{100, 200});

    OscMessage msg1{"/a"};
    msg1.append_int32(1);
    original.append(std::move(msg1));

    OscMessage msg2{"/b"};
    msg2.append_float(2.0f);
    original.append(std::move(msg2));

    std::array<uint8_t, 256> buffer{};
    auto ser_result = osc_serialize(buffer, original);
    EXPECT_TRUE(ser_result.has_value());

    // Only pass the portion of buffer that contains valid data
    size_t consumed{};
    auto result = osc_deserialize_bundle(std::span{buffer.data(), *ser_result}, consumed);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(consumed, *ser_result);

    auto const& bundle = *result;
    EXPECT_EQ(bundle.timetag().get_seconds(), 100U);
    EXPECT_EQ(bundle.element_count(), 2U);

    auto const* elem0 = bundle.element(0);
    EXPECT_TRUE(std::holds_alternative<OscMessage>(*elem0));
    EXPECT_EQ(std::get<OscMessage>(*elem0).address(), "/a");

    auto const* elem1 = bundle.element(1);
    EXPECT_TRUE(std::holds_alternative<OscMessage>(*elem1));
    EXPECT_EQ(std::get<OscMessage>(*elem1).address(), "/b");
}

TEST(osc_deserialize, nested_bundle)
{
    OscBundle inner;
    OscMessage msg{"/inner"};
    msg.append_int32(42);
    inner.append(std::move(msg));

    OscBundle outer;
    outer.append(std::move(inner));

    std::array<uint8_t, 256> buffer{};
    auto ser_result = osc_serialize(buffer, outer);
    EXPECT_TRUE(ser_result.has_value());

    // Only pass the portion of buffer that contains valid data
    size_t consumed{};
    auto result = osc_deserialize_bundle(std::span{buffer.data(), *ser_result}, consumed);
    EXPECT_TRUE(result.has_value());

    EXPECT_EQ(result->element_count(), 1U);
    auto const* elem = result->element(0);
    EXPECT_TRUE(std::holds_alternative<OscBundle>(*elem));

    auto const& nested = std::get<OscBundle>(*elem);
    EXPECT_EQ(nested.element_count(), 1U);
}

//
// Generic Parse Tests
//
TEST(osc_parse, message)
{
    OscMessage original{"/test"};
    original.append_int32(42);

    std::array<uint8_t, 64> buffer{};
    auto ser_result = osc_serialize(buffer, original);
    EXPECT_TRUE(ser_result.has_value());

    auto result = osc_parse(buffer);
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<OscMessage>(*result));

    auto const& msg = std::get<OscMessage>(*result);
    EXPECT_EQ(msg.address(), "/test");
}

TEST(osc_parse, bundle)
{
    OscBundle original;
    OscMessage msg{"/a"};
    original.append(std::move(msg));

    std::array<uint8_t, 64> buffer{};
    auto ser_result = osc_serialize(buffer, original);
    EXPECT_TRUE(ser_result.has_value());

    // Only pass the portion of buffer that contains valid data
    auto result = osc_parse(std::span{buffer.data(), *ser_result});
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<OscBundle>(*result));
}

TEST(osc_parse, detection)
{
    // Bundle detection
    std::array<uint8_t, 16> bundle_data{'#', 'b', 'u', 'n', 'd', 'l', 'e', '\0'};
    EXPECT_TRUE(osc_is_bundle(bundle_data));
    EXPECT_FALSE(osc_is_message(bundle_data));

    // Message detection
    std::array<uint8_t, 8> message_data{'/', 't', 'e', 's', 't', '\0', '\0', '\0'};
    EXPECT_FALSE(osc_is_bundle(message_data));
    EXPECT_TRUE(osc_is_message(message_data));
}

//
// Error Cases
//
TEST(osc_error, invalid_address)
{
    // Create invalid message data (address not starting with '/')
    std::array<uint8_t, 16> buffer{'t', 'e', 's', 't', '\0', '\0', '\0', '\0', ',', '\0', '\0', '\0', 0, 0, 0, 0};

    size_t consumed{};
    auto result = osc_deserialize_message(buffer, consumed);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), make_error_code(OscError::invalid_address));
}

TEST(osc_error, invalid_type_tag)
{
    // Create message with invalid type tag (not starting with ',')
    std::array<uint8_t, 16> buffer{
        '/',
        't',
        '\0',
        '\0',
        'i',
        '\0',
        '\0',
        '\0',  // Type tags without ','
        0,
        0,
        0,
        42,
        0,
        0,
        0,
        0};

    size_t consumed{};
    auto result = osc_deserialize_message(buffer, consumed);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), make_error_code(OscError::invalid_type_tag));
}

TEST(osc_error, buffer_underflow)
{
    std::array<uint8_t, 2> tiny{'/', '\0'};

    size_t consumed{};
    auto result = osc_deserialize_message(tiny, consumed);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), make_error_code(OscError::buffer_underflow));
}

TEST(osc_error, category)
{
    auto const& cat = osc_error_category();
    EXPECT_EQ(std::string_view{cat.name()}, "statusbar.osc");

    auto ec = make_error_code(OscError::invalid_address);
    EXPECT_TRUE(ec.message().find("address") != std::string::npos);
}

//
// Tests: int64 and float64 serialization roundtrips
//
TEST(osc_int64_serial, store_and_load_roundtrip)
{
    std::array<uint8_t, 8> buf{};
    int64_t const original = 0x0123456789ABCDEFLL;

    auto store_result = store_int64(std::span{buf}, original);
    EXPECT_TRUE(store_result.has_value());
    EXPECT_EQ(*store_result, 8U);

    int64_t loaded = 0;
    auto load_result = load_int64(std::span<uint8_t const>{buf}, loaded);
    EXPECT_TRUE(load_result.has_value());
    EXPECT_EQ(*load_result, 8U);
    EXPECT_EQ(loaded, original);
}

TEST(osc_int64_serial, negative_value)
{
    std::array<uint8_t, 8> buf{};
    int64_t const original = -42;

    auto store_result = store_int64(std::span{buf}, original);
    EXPECT_TRUE(store_result.has_value());

    int64_t loaded = 0;
    auto load_result = load_int64(std::span<uint8_t const>{buf}, loaded);
    EXPECT_TRUE(load_result.has_value());
    EXPECT_EQ(loaded, original);
}

TEST(osc_int64_serial, zero)
{
    std::array<uint8_t, 8> buf{};
    int64_t const original = 0;

    (void)store_int64(std::span{buf}, original);
    int64_t loaded = 99;
    (void)load_int64(std::span<uint8_t const>{buf}, loaded);
    EXPECT_EQ(loaded, 0);
}

TEST(osc_int64_serial, buffer_too_small)
{
    std::array<uint8_t, 4> buf{};
    auto store_result = store_int64(std::span{buf}, 42);
    EXPECT_FALSE(store_result.has_value());

    int64_t loaded = 0;
    auto load_result = load_int64(std::span<uint8_t const>{buf}, loaded);
    EXPECT_FALSE(load_result.has_value());
}

TEST(osc_float64_serial, store_and_load_roundtrip)
{
    std::array<uint8_t, 8> buf{};
    double const original = 3.14159265358979;

    auto store_result = store_float64(std::span{buf}, original);
    EXPECT_TRUE(store_result.has_value());
    EXPECT_EQ(*store_result, 8U);

    double loaded = 0.0;
    auto load_result = load_float64(std::span<uint8_t const>{buf}, loaded);
    EXPECT_TRUE(load_result.has_value());
    EXPECT_EQ(*load_result, 8U);
    EXPECT_TRUE(std::fabs(loaded - original) < 1e-15);
}

TEST(osc_float64_serial, negative_value)
{
    std::array<uint8_t, 8> buf{};
    double const original = -999.999;

    (void)store_float64(std::span{buf}, original);
    double loaded = 0.0;
    (void)load_float64(std::span<uint8_t const>{buf}, loaded);
    EXPECT_TRUE(std::fabs(loaded - original) < 1e-10);
}

TEST(osc_float64_serial, buffer_too_small)
{
    std::array<uint8_t, 4> buf{};
    auto store_result = store_float64(std::span{buf}, 1.0);
    EXPECT_FALSE(store_result.has_value());

    double loaded = 0.0;
    auto load_result = load_float64(std::span<uint8_t const>{buf}, loaded);
    EXPECT_FALSE(load_result.has_value());
}

TEST(osc_message_int64, append_and_retrieve)
{
    OscMessage msg("/test");
    msg.append_int64(0x0123456789ABCDEFLL);
    msg.append_int64(-42);

    EXPECT_EQ(msg.argument_count(), 2U);
    auto val0 = msg.argument(0)->as_int64();
    EXPECT_TRUE(val0.has_value());
    EXPECT_EQ(*val0, 0x0123456789ABCDEFLL);

    auto val1 = msg.argument(1)->as_int64();
    EXPECT_TRUE(val1.has_value());
    EXPECT_EQ(*val1, -42);
}

TEST(osc_message_float64, append_and_retrieve)
{
    OscMessage msg("/test");
    msg.append_double(3.14159265358979);
    msg.append_double(-999.5);

    EXPECT_EQ(msg.argument_count(), 2U);
    auto val0 = msg.argument(0)->as_double();
    EXPECT_TRUE(val0.has_value());
    EXPECT_TRUE(std::fabs(*val0 - 3.14159265358979) < 1e-12);

    auto val1 = msg.argument(1)->as_double();
    EXPECT_TRUE(val1.has_value());
    EXPECT_TRUE(std::fabs(*val1 - (-999.5)) < 1e-12);
}

//
// Bundle and argument coverage tests
//
TEST(osc_bundle_ops, clear_elements)
{
    OscBundle bundle{NtpTimetag::immediate()};
    OscMessage msg{"/test"};
    msg.append_int32(42);
    bundle.append(std::move(msg));
    EXPECT_EQ(bundle.element_count(), size_t{1});

    bundle.clear_elements();
    EXPECT_EQ(bundle.element_count(), size_t{0});
}

TEST(osc_bundle_ops, reserve_elements)
{
    OscBundle bundle{NtpTimetag::immediate()};
    // reserve_elements should not throw and should not change count
    bundle.reserve_elements(100);
    EXPECT_EQ(bundle.element_count(), size_t{0});

    // After reserve, adding elements should work
    OscMessage msg{"/a"};
    bundle.append(std::move(msg));
    EXPECT_EQ(bundle.element_count(), size_t{1});
}

TEST(osc_msg_ops, reserve_arguments)
{
    OscMessage msg{"/synth/freq"};
    // reserve_arguments should not throw
    msg.reserve_arguments(50);
    EXPECT_EQ(msg.argument_count(), size_t{0});

    msg.append_float(440.0f);
    msg.append_string("sine");
    EXPECT_EQ(msg.argument_count(), size_t{2});
}

TEST(osc_arg_ops, make_avb_timetag)
{
    NtpTimetag tt{12345, 67890};
    auto arg = OscArgument::make_avb_timetag(tt);
    EXPECT_TRUE(arg.get_type_tag() == type_tag::avb_time);

    auto val = arg.as_timetag();
    EXPECT_TRUE(val.has_value());
    EXPECT_EQ(val->seconds.get(), uint32_t{12345});
    EXPECT_EQ(val->fraction.get(), uint32_t{67890});
}

TEST(osc_timetag_ops, copy_assignment)
{
    NtpTimetag tt1{100, 200};
    NtpTimetag tt2{0, 0};
    tt2 = tt1;
    EXPECT_EQ(tt2.seconds.get(), uint32_t{100});
    EXPECT_EQ(tt2.fraction.get(), uint32_t{200});
}

//
// Exhaustive error message coverage
//

TEST(osc_error, all_errors_have_messages)
{
    // Verify every OscError code produces a non-"Unknown" message
    auto check = [](OscError e) {
        std::error_code ec = e;
        EXPECT_TRUE(!ec.message().empty());
        EXPECT_TRUE(ec.message().find("Unknown") == std::string::npos);
    };
    check(OscError::invalid_address);
    check(OscError::invalid_type_tag);
    check(OscError::unsupported_type);
    check(OscError::buffer_overflow);
    check(OscError::buffer_underflow);
    check(OscError::invalid_bundle);
    check(OscError::invalid_alignment);
    check(OscError::string_not_terminated);
    check(OscError::type_mismatch);
    check(OscError::invalid_message);
    check(OscError::bundle_too_deep);
}

TEST(osc_error, category_name)
{
    EXPECT_EQ(std::string_view{osc_error_category().name()}, "statusbar.osc");
}

//
// OSC is a UDP wire-format parser fed from the network; it is a fuzz
// target. These tests exercise the decoder's failure paths — each
// corresponds to a concrete shape a malformed packet can take.
//

namespace {

auto deserialize_msg(std::span<uint8_t const> buf) -> Status
{
    OscMessage msg;
    size_t consumed{};
    return osc_deserialize_message(buf, msg, consumed);
}

auto deserialize_bundle(std::span<uint8_t const> buf)
{
    size_t consumed{};
    return osc_deserialize_bundle(buf, consumed);
}

}  // namespace

TEST(osc_decoder_safety, blob_with_negative_length)
{
    std::array<uint8_t, 16> buf{
        '/',
        'b',
        '\0',
        '\0',
        ',',
        'b',
        '\0',
        '\0',
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0x00,
        0x00,
        0x00,
        0x00,
    };
    auto const result = deserialize_msg(buf);
    EXPECT_FALSE(result);
}

TEST(osc_decoder_safety, blob_length_exceeds_remaining)
{
    std::array<uint8_t, 16> buf{
        '/',
        'b',
        '\0',
        '\0',
        ',',
        'b',
        '\0',
        '\0',
        0x00,
        0x00,
        0x00,
        0x64,  // len = 100
        0x01,
        0x02,
        0x03,
        0x04,
    };
    auto const result = deserialize_msg(buf);
    EXPECT_FALSE(result);
}

TEST(osc_decoder_safety, string_address_missing_null_terminator)
{
    std::array<uint8_t, 16> buf{
        '/',
        'a',
        'b',
        'c',
        'd',
        'e',
        'f',
        'g',
        ',',
        '\0',
        '\0',
        '\0',
        0x00,
        0x00,
        0x00,
        0x00,
    };
    auto const result = deserialize_msg(buf);
    EXPECT_FALSE(result);
}

TEST(osc_decoder_safety, string_arg_missing_null_terminator)
{
    std::array<uint8_t, 16> buf{
        '/',
        's',
        '\0',
        '\0',
        ',',
        's',
        '\0',
        '\0',
        'a',
        'b',
        'c',
        'd',
        'e',
        'f',
        'g',
        'h',
    };
    auto const result = deserialize_msg(buf);
    EXPECT_FALSE(result);
}

TEST(osc_decoder_safety, unknown_type_tag_character)
{
    std::array<uint8_t, 12> buf{
        '/',
        'x',
        '\0',
        '\0',
        ',',
        'Z',
        '\0',
        '\0',
        0x00,
        0x00,
        0x00,
        0x00,
    };
    auto const result = deserialize_msg(buf);
    EXPECT_FALSE(result);
}

TEST(osc_decoder_safety, type_tag_declares_more_args_than_payload)
{
    // Tag says ",ii" but only 4 bytes of payload.
    std::array<uint8_t, 12> buf{
        '/',
        'x',
        '\0',
        '\0',
        ',',
        'i',
        'i',
        '\0',
        0x00,
        0x00,
        0x00,
        0x01,
    };
    auto const result = deserialize_msg(buf);
    EXPECT_FALSE(result);
}

TEST(osc_decoder_safety, address_without_leading_slash)
{
    std::array<uint8_t, 12> buf{
        'f',
        'o',
        'o',
        '\0',
        ',',
        '\0',
        '\0',
        '\0',
        0x00,
        0x00,
        0x00,
        0x00,
    };
    auto const result = deserialize_msg(buf);
    EXPECT_FALSE(result);
}

TEST(osc_decoder_safety, truncated_before_type_tag)
{
    std::array<uint8_t, 4> buf{'/', 'a', '\0', '\0'};
    auto const result = deserialize_msg(buf);
    EXPECT_FALSE(result);
}

TEST(osc_decoder_safety, truncated_mid_int32_argument)
{
    std::array<uint8_t, 10> buf{
        '/',
        'x',
        '\0',
        '\0',
        ',',
        'i',
        '\0',
        '\0',
        0x00,
        0x01,
    };
    auto const result = deserialize_msg(buf);
    EXPECT_FALSE(result);
}

TEST(osc_decoder_safety, bundle_element_size_exceeds_remaining)
{
    std::array<uint8_t, 24> buf{
        '#',  'b',  'u',  'n',  'd', 'l', 'e', '\0', 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x00, 0x00, 0x64,  // elem_size = 100 (past end)
        0xAA, 0xBB, 0xCC, 0xDD,
    };
    auto const result = deserialize_bundle(buf);
    EXPECT_FALSE(result.has_value());
}

TEST(osc_decoder_safety, bundle_element_size_not_multiple_of_4_rejected)
{
    // elem_size = 6 is within bounds but not 4-byte aligned; must be rejected
    // with invalid_alignment (all OSC data is 4-byte aligned).
    std::array<uint8_t, 26> buf{
        '#',  'b',  'u',  'n',  'd',  'l',  'e', '\0', 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x00, 0x00, 0x06,  // elem_size = 6
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
    };
    auto const result = deserialize_bundle(buf);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), make_error_code(OscError::invalid_alignment));
}

TEST(osc_serialize, string_with_embedded_nul_rejected)
{
    // An address/string containing an embedded NUL would reparse as a shorter
    // string plus misaligned residue; serialization must reject it.
    OscMessage msg{"/ok"};
    msg.append_string(std::string_view("ab\0cd", 5));
    std::array<uint8_t, 128> buf{};
    auto const r = osc_serialize(std::span<uint8_t>(buf), msg);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), make_error_code(OscError::invalid_message));
}

TEST(osc_decoder_safety, bundle_element_size_zero_rejected)
{
    std::array<uint8_t, 24> buf{
        '#',  'b',  'u',  'n',  'd', 'l', 'e', '\0', 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x00, 0x00, 0x00,  // elem_size = 0
        0x00, 0x00, 0x00, 0x00,
    };
    auto const result = deserialize_bundle(buf);
    EXPECT_FALSE(result.has_value());
}

TEST(osc_decoder_safety, bundle_truncated_before_timetag)
{
    std::array<uint8_t, 8> buf{'#', 'b', 'u', 'n', 'd', 'l', 'e', '\0'};
    auto const result = deserialize_bundle(buf);
    EXPECT_FALSE(result.has_value());
}

TEST(osc_decoder_safety, bundle_element_neither_bundle_nor_message)
{
    std::array<uint8_t, 28> buf{
        '#', 'b', 'u', 'n', 'd',  'l',  'e',  '\0', 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x00, 0x00, 0x08,  // elem_size = 8
        'x', 'x', 'x', 'x', 0x00, 0x00, 0x00, 0x00,
    };
    auto const result = deserialize_bundle(buf);
    EXPECT_FALSE(result.has_value());
}

TEST(osc_decoder_safety, deeply_nested_bundles_rejected)
{
    // Build depth (OSC_MAX_BUNDLE_DEPTH + 5) bundles nested one inside the
    // next; the parser must reject rather than recurse into a stack overflow.
    std::vector<uint8_t> inner{'#', 'b', 'u', 'n', 'd', 'l', 'e', '\0', 0, 0, 0, 0, 0, 0, 0, 0};
    for (size_t i = 0; i < OSC_MAX_BUNDLE_DEPTH + 5; ++i) {
        auto const child_size = static_cast<uint32_t>(inner.size());
        std::vector<uint8_t> outer{'#', 'b', 'u', 'n', 'd', 'l', 'e', '\0', 0, 0, 0, 0, 0, 0, 0, 0};
        outer.push_back(static_cast<uint8_t>((child_size >> 24) & 0xFF));
        outer.push_back(static_cast<uint8_t>((child_size >> 16) & 0xFF));
        outer.push_back(static_cast<uint8_t>((child_size >> 8) & 0xFF));
        outer.push_back(static_cast<uint8_t>(child_size & 0xFF));
        outer.insert(outer.end(), inner.begin(), inner.end());
        inner = std::move(outer);
    }
    auto const result = deserialize_bundle(inner);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), make_error_code(OscError::bundle_too_deep));
}

TEST(osc_decoder_safety, wrong_bundle_magic)
{
    // 8 bytes with '#' prefix but not exactly "#bundle\0".
    std::array<uint8_t, 16> buf{
        '#',
        'b',
        'u',
        'n',
        'd',
        'l',
        'e',
        'X',
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    };
    auto const result = deserialize_bundle(buf);
    EXPECT_FALSE(result.has_value());
}

//
// Test Runner
//
TEST_MAIN(statusbar_osc, osc_test)