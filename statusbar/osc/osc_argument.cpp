// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/osc/osc_argument.hpp"

namespace statusbar::osc {

auto OscArgument::as_bool() const noexcept -> StatusValue<bool>
{
    if (type_tag_ == type_tag::true_val) {
        return success(true);
    }
    if (type_tag_ == type_tag::false_val) {
        return success(false);
    }
    // Also support integer conversion to bool
    if (auto const* ptr = std::get_if<int32_t>(&value_)) {
        return success(*ptr != 0);
    }
    return failure(OscError::type_mismatch);
}

auto OscArgument::wire_size() const noexcept -> size_t
{
    switch (type_tag_) {
        case type_tag::int32:
        case type_tag::float32:
            return 4;

        case type_tag::int64:
        case type_tag::float64:
        case type_tag::timetag:
        case type_tag::avb_time:
            return 8;

        case type_tag::string:
            if (auto const* ptr = std::get_if<std::string>(&value_)) {
                return osc_padded_string_size(ptr->size());
            }
            return 0;

        case type_tag::blob:
            if (auto const* ptr = std::get_if<std::vector<uint8_t>>(&value_)) {
                return osc_padded_blob_size(ptr->size());
            }
            return 0;

        case type_tag::true_val:
        case type_tag::false_val:
        case type_tag::nil:
        case type_tag::impulse:
        default:
            return 0;
    }
}

}  // namespace statusbar::osc
