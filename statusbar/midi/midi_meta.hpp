#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// MIDI Meta Event — Non-owning view into meta event data
/// The data span is valid only during callback invocation.

#include "statusbar/buffer/span_utils.hpp"
#include "statusbar/midi/midi_error.hpp"
#include "statusbar/midi/midi_types.hpp"
#include "statusbar/status/status.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace statusbar::midi {

struct MetaEvent
{
    uint8_t type{};
    std::span<uint8_t const> data{};

    [[nodiscard]] auto tempo_us_per_beat() const -> StatusValue<uint32_t>
    {
        if (data.size() != 3) {
            return failure(MidiError::invalid_meta_event);
        }
        return static_cast<uint32_t>(data[0]) << 16 | static_cast<uint32_t>(data[1]) << 8 | static_cast<uint32_t>(data[2]);
    }

    [[nodiscard]] auto tempo_bpm() const -> StatusValue<float>
    {
        auto const us = tempo_us_per_beat();
        if (!us.has_value()) {
            return forward_failure(us);
        }
        return 60'000'000.0f / static_cast<float>(us.value());
    }

    [[nodiscard]] auto text() const -> std::string_view { return ::statusbar::as_string_view(data); }

    [[nodiscard]] auto time_signature() const -> StatusValue<std::array<uint8_t, 4>>
    {
        if (data.size() != 4) {
            return failure(MidiError::invalid_meta_event);
        }
        return std::array<uint8_t, 4>{data[0], data[1], data[2], data[3]};
    }

    [[nodiscard]] auto key_signature() const -> StatusValue<std::array<int8_t, 2>>
    {
        if (data.size() != 2) {
            return failure(MidiError::invalid_meta_event);
        }
        return std::array<int8_t, 2>{static_cast<int8_t>(data[0]), static_cast<int8_t>(data[1])};
    }
};

}  // namespace statusbar::midi
