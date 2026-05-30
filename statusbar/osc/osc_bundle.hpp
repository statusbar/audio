#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// OSC Bundle - Open Sound Control Protocol Bundle Type
/// Based on OSC 1.0 specification

#include "statusbar/osc/osc_base.hpp"
#include "statusbar/osc/osc_message.hpp"

#include <cstddef>
#include <memory_resource>
#include <variant>
#include <vector>

namespace statusbar::osc {

// Forward declaration for recursive bundle type
class OscBundle;

/// Element in an OSC bundle - either a message or a nested bundle
using OscBundleElement = std::variant<OscMessage, OscBundle>;

//
// OSC Bundle Class
//
/// An OSC bundle consisting of a timetag and zero or more elements (messages or bundles)
class OscBundle
{
  public:
    /// Bundle header size: "#bundle\0" (8 bytes) + timetag (8 bytes)
    static constexpr size_t HEADER_SIZE = 16;

    /// Default constructor - creates empty bundle with immediate timetag.
    /// Uses std::pmr::get_default_resource() for element storage.
    OscBundle()
        : timetag_{NtpTimetag::immediate()}
    {}

    /// Construct with timetag.
    /// @param timetag The bundle timetag
    /// @param memory_resource Memory resource for the elements vector.
    ///        nullptr is treated as std::pmr::get_default_resource().
    explicit OscBundle(NtpTimetag timetag, std::pmr::memory_resource* memory_resource = nullptr)
        : timetag_{timetag}
        , elements_{memory_resource != nullptr ? memory_resource : std::pmr::get_default_resource()}
    {}

    //===
    // Timetag
    //===

    /// Get the bundle timetag
    [[nodiscard]] auto timetag() const noexcept -> NtpTimetag { return timetag_; }

    /// Set the bundle timetag
    /// @param t The NTP timetag to set
    void set_timetag(NtpTimetag t) noexcept { timetag_ = t; }

    /// Check if this bundle should be processed immediately
    [[nodiscard]] auto is_immediate() const noexcept -> bool { return timetag_.is_immediate(); }

    //===
    // Elements
    //===

    /// Get the number of elements
    [[nodiscard]] auto element_count() const noexcept -> size_t { return elements_.size(); }

    /// Check if the bundle has any elements
    [[nodiscard]] auto has_elements() const noexcept -> bool { return !elements_.empty(); }

    /// Get element at index (returns nullptr if out of range)
    /// @param index Zero-based element index
    [[nodiscard]] auto element(size_t index) const noexcept -> OscBundleElement const*
    {
        if (index < elements_.size()) {
            return &elements_[index];
        }
        return nullptr;
    }

    /// Get all elements
    [[nodiscard]] auto elements() const noexcept -> std::pmr::vector<OscBundleElement> const& { return elements_; }

    /// Append a message to the bundle
    /// @param msg The OSC message to append
    void append(OscMessage msg) { elements_.push_back(std::move(msg)); }

    /// Append a nested bundle
    /// @param bundle The nested OSC bundle to append
    void append(OscBundle bundle) { elements_.push_back(std::move(bundle)); }

    /// Clear all elements
    void clear_elements() noexcept { elements_.clear(); }

    /// Reserve capacity for elements
    /// @param capacity Number of elements to pre-allocate space for
    void reserve_elements(size_t capacity) { elements_.reserve(capacity); }

    //===
    // Wire Size Calculation
    //===

    /// Calculate the total wire size for this bundle
    [[nodiscard]] auto wire_size() const -> size_t;

  private:
    /// Calculate wire size for a bundle element
    [[nodiscard]] static auto element_wire_size(OscBundleElement const& elem) -> size_t
    {
        return std::visit([](auto const& e) -> size_t { return e.wire_size(); }, elem);
    }

    NtpTimetag timetag_;                           ///< Bundle timetag
    std::pmr::vector<OscBundleElement> elements_;  ///< Bundle elements (messages or nested bundles)
};

}  // namespace statusbar::osc
