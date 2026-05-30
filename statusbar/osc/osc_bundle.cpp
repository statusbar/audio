// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/osc/osc_bundle.hpp"

namespace statusbar::osc {

auto OscBundle::wire_size() const -> size_t
{
    // Header: "#bundle\0" (8 bytes) + timetag (8 bytes)
    size_t size = HEADER_SIZE;

    // Elements: each has a 4-byte size prefix + element data
    for (auto const& elem : elements_) {
        size += 4;  // Size prefix
        size += element_wire_size(elem);
    }

    return size;
}

}  // namespace statusbar::osc
