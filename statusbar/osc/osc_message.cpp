// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/osc/osc_message.hpp"

namespace statusbar::osc {

auto OscMessage::type_tags() const -> std::string
{
    std::string result;
    result.reserve(arguments_.size() + 1);
    result.push_back(',');
    for (auto const& arg : arguments_) {
        result.push_back(arg.get_type_tag());
    }
    return result;
}

auto OscMessage::wire_size() const noexcept -> size_t
{
    // Address pattern (padded)
    size_t size = osc_padded_string_size(address_.size());

    // Type tag string (padded) - includes leading ','
    size += osc_padded_string_size(arguments_.size() + 1);

    // Argument data
    for (auto const& arg : arguments_) {
        size += arg.wire_size();
    }

    return size;
}

}  // namespace statusbar::osc
