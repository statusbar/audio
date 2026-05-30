#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

//
// Engine Module — tiered real-time data-flow framework.
//
// The engine hosts a self-similar hierarchy of Elements. Each tier has the
// same structural shape — high-rate data flowing through, lower-rate control
// flowing in from above, telemetry flowing back up — only the rates change.
//
//   Tier 0 (audio rate, hard real-time)
//     8/16/32 samples per cycle, ~166.667 µs cycle period
//     reads coefficient Segments from a itc::LatestPipe each leg boundary
//
//   Tier 1 (leg rate, soft real-time, ~187.5 Hz)
//     produces coefficient Segments for Tier 0
//     consumes higher-level commands or sidechain inputs
//
//   Tier 2+ (planner rate, soft real-time, application-defined)
//     fader taper evaluation, automation, MIDI, network controllers
//     produce streams of high-level commands for Tier 1
//
// Module structure:
//   Tier-0 primitives:
//     engine_constants.hpp        — compile-time cycle/leg/recip constants
//     engine_segment.hpp          — Segment<Coeffs> and SegmentableCoeffs concept
//     engine_element.hpp          — IsElement concept and RealtimePolicy
//     engine_smoothed_element.hpp — SmoothedElement<...> generic Tier 0 wrapper
//     engine_gain_matrix.hpp      — GainMatrix<NIn, NOut, SR> SoA crosspoint mixer
//     engine_scheduler.hpp        — drive_cycles() single-Element driver
//
//   Biquad Tier 0 + Tier 1:
//     engine_biquad_coeffs.hpp    — BiquadComplexCoeffs SegmentableCoeffs type
//     engine_biquad_element.hpp   — BiquadApply + BiquadElement<SR> (Tier 0)
//     engine_biquad_chain.hpp     — BiquadChain<N, SR, T> (Tier 0; N cascaded biquads)
//     engine_biquad_designer.hpp  — BiquadDesigner with always-3-leg parameter morph (Tier 1)
//
//   Gain Tier 0 + Tier 1:
//     engine_gain_coeffs.hpp      — GainAmplitudeCoeffs SegmentableCoeffs type
//     engine_gain_element.hpp     — GainApply + GainElement<SR> (Tier 0)
//     engine_gain_designer.hpp    — GainDesigner 1-leg ramp producer (Tier 1)
//
//   Metering / telemetry:
//     engine_meter.hpp            — Meter<SR, T> passthrough Tier 0 element
//                                   emitting per-leg peak + RMS upward
//

#include "statusbar/engine/engine_biquad_chain.hpp"
#include "statusbar/engine/engine_biquad_coeffs.hpp"
#include "statusbar/engine/engine_biquad_designer.hpp"
#include "statusbar/engine/engine_biquad_element.hpp"
#include "statusbar/engine/engine_constants.hpp"
#include "statusbar/engine/engine_element.hpp"
#include "statusbar/engine/engine_gain_coeffs.hpp"
#include "statusbar/engine/engine_gain_designer.hpp"
#include "statusbar/engine/engine_gain_element.hpp"
#include "statusbar/engine/engine_gain_matrix.hpp"
#include "statusbar/engine/engine_meter.hpp"
#include "statusbar/engine/engine_scheduler.hpp"
#include "statusbar/engine/engine_segment.hpp"
#include "statusbar/engine/engine_smoothed_element.hpp"
#include "statusbar/itc/itc_message_pipe.hpp"

namespace statusbar::engine {}
