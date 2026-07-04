// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/osc/osc_error.hpp"

#include <string>
#include <system_error>

namespace statusbar::osc {

auto OscErrorCategory::message(int ev) const -> std::string
{
    switch (static_cast<OscError>(ev)) {
        case OscError::invalid_address:
            return "OSC address must start with '/'";
        case OscError::invalid_type_tag:
            return "OSC type tag must start with ','";
        case OscError::unsupported_type:
            return "Unsupported OSC type tag";
        case OscError::buffer_overflow:
            return "Buffer too small for OSC data";
        case OscError::buffer_underflow:
            return "Unexpected end of OSC data";
        case OscError::invalid_bundle:
            return "OSC bundle must start with '#bundle'";
        case OscError::invalid_alignment:
            return "OSC data not 4-byte aligned";
        case OscError::string_not_terminated:
            return "OSC string missing null terminator";
        case OscError::type_mismatch:
            return "OSC argument type mismatch";
        case OscError::invalid_message:
            return "Invalid OSC message format";
        case OscError::bundle_too_deep:
            return "OSC bundle nesting too deep";
        default:
            return "Unknown OSC error";
    }
}

auto osc_error_category() noexcept -> OscErrorCategory const&
{
    static OscErrorCategory const instance;
    return instance;
}

auto make_error_code(OscError e) noexcept -> std::error_code
{
    return {static_cast<int>(e), osc_error_category()};
}

}  // namespace statusbar::osc
