[← back to module index](README.md)

# osc

Open Sound Control 1.0 wire-format codec: typed `OscArgument` values
carried by `OscMessage` (address pattern + comma-led type-tag string)
or grouped by `OscBundle` (NTP timetag + nested elements), all
encoded big-endian and 4-byte aligned over a caller-owned
`std::span<uint8_t>`. Free `osc_serialize` / `osc_deserialize_*`
functions move between the in-memory object model and the wire form.

## Overview

The `osc` module implements OSC 1.0 — the messaging format used to drive
synthesizers, lighting, and other multimedia devices over UDP or any
other byte transport. The module is concerned only with the *packet*
layer: building, framing, and parsing OSC packets. Transport, dispatch,
and pattern matching are left to the caller.

There are two packet kinds. An **`OscMessage`** is an address pattern
(a UTF-8 path starting with `/`) followed by a `,`-led type-tag string
and one wire-encoded argument per type-tag character. An **`OscBundle`**
is the literal header `#bundle\0`, an 8-byte NTP timetag, and a sequence
of `int32`-prefixed elements where each element is itself a message or a
nested bundle. Both are size-prefixed when carried inside a bundle and
unframed when standalone.

Arguments are a closed set of OSC 1.0 type tags backed by a
`std::variant`: `'i'` (int32), `'f'` (float32), `'s'` (string), `'b'`
(blob), `'h'` (int64), `'t'` (NTP timetag), `'d'` (float64), `'T'`/`'F'`
(true / false, no data), `'N'` (nil), `'I'` (impulse / bang), plus the
project-specific `'a'` (AVB-style timetag — same 8-byte payload as `'t'`
but a distinct tag). The OSC 1.1 array delimiters `[` `]` and the
`'S'`/`'c'`/`'r'`/`'m'` types are **not** implemented; the deserializer
returns `OscError::unsupported_type` if it encounters one.

The serializer/deserializer split mirrors `core::buffer`: free functions
take a `std::span<uint8_t>` plus an `OscMessage` / `OscBundle` /
`OscArgument` (serialize) or a `std::span<uint8_t const>` and produce
the corresponding object (deserialize). Both report progress via the
project's `StatusValue<size_t>`. Low-level `store_*` / `load_*` helpers
for individual wire fields are also public so callers building higher-
level codecs can reuse the 4-byte-aligned, big-endian primitives
directly.

`OscMessage` and `OscBundle` use `std::pmr::vector` for their
argument / element storage so callers that care about allocation
behaviour can supply a `std::pmr::memory_resource*` at construction
(default is `std::pmr::get_default_resource()`). An
allocation-light deserialize path is available via the
`osc_deserialize_message(buffer, OscMessage&, bytes_consumed)`
overload: it clears and reuses an existing message's allocated argument
storage rather than constructing a new one.

## Key types

- `OscMessage` — address pattern + arguments. Constructed from a `std::string_view` address (must start with `/`); `append_int32`/`append_float`/`append_string`/`append_blob`/`append_timetag`/`append_true`/`append_false`/`append_nil`/`append_impulse` etc. add arguments; `type_tags()` produces the comma-led string; `wire_size()` reports the serialized length.
- `OscBundle` — NTP timetag plus a `std::pmr::vector<OscBundleElement>` where each element is either an `OscMessage` or a nested `OscBundle`. `append(OscMessage)` / `append(OscBundle)` add elements; defaults to the "immediate" timetag.
- `OscBundleElement` — `std::variant<OscMessage, OscBundle>` alias for bundle entries.
- `OscArgument` — type-tagged value (`std::variant` over `int32_t`, `float`, `double`, `int64_t`, `std::string`, `std::vector<uint8_t>`, `NtpTimetag`, `std::monostate`). Constructed via `make_int32` / `make_float` / `make_string` / `make_blob` / `make_timetag` / `make_true` / `make_false` / `make_nil` / `make_impulse` / `make_avb_timetag`; inspected via `as_int32()` … `as_blob()` (all return `StatusValue<T>`) or `visit(callable)`.
- `NtpTimetag` — 64-bit NTP-format timetag (`seconds` + `fraction` as `ieee::quadlet_t`s, big-endian on the wire). `NtpTimetag::immediate()` is the OSC "execute now" sentinel `0x0000000000000001`; `is_immediate()` / `is_zero()` test it.
- `OscError` / `osc_error_category()` / `make_error_code(OscError)` — `enum class` registered as `std::is_error_code_enum`; category name `"statusbar.osc"`. Codes cover address, type-tag, alignment, framing, and buffer-bounds failures.
- `type_tag::*` — `constexpr char` constants (`int32`, `float32`, `string`, `blob`, `int64`, `timetag`, `float64`, `true_val`, `false_val`, `nil`, `impulse`, `avb_time`) for the implemented tag characters.

The address pattern itself is plain `std::string` / `std::string_view`;
the module does not provide a dedicated address type or matcher.

## Quick example

```cpp
#include "statusbar/osc/osc.hpp"

#include <array>
#include <cstdint>

using namespace statusbar::osc;

int main()
{
    // Build a message into a fixed buffer.
    OscMessage msg{"/synth/frequency"};
    msg.append_float(440.0f);
    msg.append_string("sine");

    std::array<uint8_t, 256> buffer{};
    auto written = osc_serialize(buffer, msg);
    if (!written) {
        return 1;
    }

    // Parse it back from the same bytes.
    size_t consumed{};
    auto parsed = osc_deserialize_message(std::span{buffer.data(), *written}, consumed);
    if (!parsed) {
        return 1;
    }

    auto const& m = *parsed;
    auto const freq = m.argument(0)->as_float();   // StatusValue<float>{440.0f}
    auto const wave = m.argument(1)->as_string();  // StatusValue<string_view>{"sine"}
    return (freq && wave) ? 0 : 1;
}
```

## Headers

- `statusbar/osc/osc.hpp` — module header; consumers `#include` this.
- `statusbar/osc/osc_base.hpp` — `NtpTimetag`, the `type_tag::*` character constants, `OSC_BUNDLE_ID`, length limits, and the `osc_round_up` / `osc_padded_string_size` / `osc_padded_blob_size` alignment helpers.
- `statusbar/osc/osc_error.hpp` — `OscError` enum, `OscErrorCategory`, `osc_error_category()`, `make_error_code()`.
- `statusbar/osc/osc_argument.hpp` — `OscArgument` plus the `OscArgumentValue` variant alias.
- `statusbar/osc/osc_message.hpp` — `OscMessage`.
- `statusbar/osc/osc_bundle.hpp` — `OscBundle`, `OscBundleElement`.
- `statusbar/osc/osc_serializer.hpp` — `osc_serialize(...)` overloads for arguments, messages, and bundles, plus the low-level `store_int32` / `store_float32` / `store_int64` / `store_float64` / `store_timetag` / `store_string` / `store_blob` field helpers.
- `statusbar/osc/osc_deserializer.hpp` — `osc_deserialize_message` / `osc_deserialize_bundle` / `osc_deserialize_argument`, the `osc_parse` message-or-bundle dispatcher, `osc_is_bundle` / `osc_is_message` predicates, and the matching `load_*` field helpers.

## Dependencies

- **Statusbar modules:** `core::buffer` (`span_copy`, `span_load`, `span_store`, `span_zero`, `make_const_span`, `as_string_view`), `core::ieee` (`quadlet_t` storage for `NtpTimetag::seconds` / `fraction`), `core::status` (every checked operation returns `Status` or `StatusValue<size_t>`).
- **System / external:** `<array>`, `<bit>` (`std::bit_cast`), `<cstdint>`, `<cstring>` (`std::memchr`), `<expected>`, `<memory_resource>` (`std::pmr::vector` / `memory_resource`), `<span>`, `<string>`, `<string_view>`, `<system_error>`, `<variant>` from the C++20/23 standard library.

## Notes & caveats

- Targets **OSC 1.0**. OSC 1.1 arrays (`[` … `]`) and the 1.1 / extended tags `S` (symbol), `c` (char), `r` (RGBA), `m` (MIDI) are not implemented; encountering them on the wire yields `OscError::unsupported_type`. The `a` tag (AVB-style timetag, 8-byte NTP payload) is a project-specific extension.
- All multi-byte fields are **big-endian** and every field — address, type-tag string, string argument, blob — is padded out to a multiple of 4 bytes. The `osc_round_up` / `osc_padded_string_size` / `osc_padded_blob_size` helpers are the canonical way to size these.
- Messages must have an address starting with `/`; the serializer rejects others with `OscError::invalid_address`. The deserializer additionally rejects type-tag strings that do not start with `,` (`OscError::invalid_type_tag`) and bundles that do not begin with `#bundle\0` (`OscError::invalid_bundle`).
- Buffer-bounds failures surface as `OscError::buffer_overflow` (serializer ran out of destination space) or `OscError::buffer_underflow` (deserializer ran out of source bytes); string fields missing a `\0` terminator yield `OscError::string_not_terminated`.
- Address-pattern matching (the OSC wildcard grammar — `?`, `*`, `[...]`, `{a,b}`) is **not** provided. Consumers dispatch on `OscMessage::address()` themselves.
- `OscMessage` and `OscBundle` allocate via `std::pmr::vector`; pass a `std::pmr::memory_resource*` to the constructor to redirect allocation (e.g. into a `std::pmr::monotonic_buffer_resource` for hot-path use). The `osc_deserialize_message(buffer, OscMessage&, bytes_consumed)` overload reuses an existing message's capacity for allocation-light reparsing — call `reserve_arguments(n)` at init time.
- Bundles recurse: the deserializer walks nested bundles directly with no explicit depth cap. Use trusted input or wrap parsing in a stack-guard if exposing it to untrusted network data.
- `NtpTimetag::immediate()` is the OSC "execute now" sentinel (`seconds == 0`, `fraction == 1`); a zero timetag is distinct and is reported by `is_zero()`. Seconds are NTP epoch (1900-01-01 UTC), not Unix epoch.
- `osc_parse(buffer)` returns a `std::variant<OscMessage, OscBundle>` based on the leading bytes (`#bundle…` → bundle; `/…` → message). Other leading bytes yield `OscError::invalid_message`.
- `OscArgument::as_bool()` accepts `T`, `F`, **and** an `int32` argument (non-zero → true) — convenient when peer encoders use integers for booleans.

## Further reading

- [`midi`](MIDI_MODULE.md) — sibling protocol module in the same package.
- `core::buffer` — the byte-buffer primitives this module's `span_copy`/`span_load`/`span_store`/`span_zero` calls come from.
- `core::status` — the `Status` / `StatusValue<T>` return types used throughout.
