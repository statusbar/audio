// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for the OSC wire-format decoder. OSC is a UDP wire-format
/// parser fed from the network; osc_parse dispatches to the message and
/// (recursive) bundle decoders. They must not crash, hang, over-recurse, or
/// read out of bounds on any input. What we look for is undefined behavior
/// caught by ASan / UBSan.

#include "statusbar/osc/osc_deserializer.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

using namespace statusbar;

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    auto const buffer = std::span<uint8_t const>(data, size);

    // Top-level dispatch (message-or-bundle), which exercises the recursive
    // bundle path and the message path together.
    (void)osc::osc_parse(buffer);

    // Also drive the two decoders directly, so a packet that osc_parse routes
    // one way still exercises the other decoder's bounds checks.
    {
        size_t consumed{};
        (void)osc::osc_deserialize_message(buffer, consumed);
    }
    {
        size_t consumed{};
        (void)osc::osc_deserialize_bundle(buffer, consumed);
    }
    return 0;
}
