#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Self-contained fatal helper for the dsp module. The SIMD headers are
/// deliberately dependency-light (pure math, no core/status include), so they
/// use this instead of statusbar::throw_or_abort. With exceptions enabled it
/// throws std::out_of_range (preserving .at() semantics — callers may catch);
/// under -fno-exceptions it logs and calls std::terminate(). This is the only
/// __cpp_exceptions switch in the dsp module.

#include <cstdio>
#include <exception>
#include <stdexcept>

namespace statusbar::dsp::detail {

[[noreturn]] inline void out_of_range([[maybe_unused]] char const* message)
{
#if __cpp_exceptions
    throw std::out_of_range(message);
#else
    std::fprintf(stderr, "fatal: %s\n", message);
    std::terminate();
#endif
}

}  // namespace statusbar::dsp::detail
