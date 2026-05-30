#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Audio Module - Common Types
// Cross-platform audio types and configuration structures

#include "statusbar/sg14/inplace_function.h"
#include "statusbar/status/status.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace statusbar::audio {

/// Sample formats supported by the audio system
enum class SampleFormat : uint8_t
{
    Float32,  // 32-bit float (native format, recommended)
    Int16,    // 16-bit signed integer
    Int32     // 32-bit signed integer
};

/// Standard sample rates
enum class SampleRate : uint32_t
{
    Rate_44100 = 44100,
    Rate_48000 = 48000,
    Rate_96000 = 96000,
    Rate_192000 = 192000
};

/// Thread priority for audio processing
enum class ThreadPriority : uint8_t
{
    Normal,    ///< Normal priority (SCHED_OTHER) - works without privileges
    Elevated,  ///< Try realtime, fallback to normal if insufficient privileges
    Realtime   ///< Require realtime scheduling (SCHED_FIFO) - fails if no privileges
};

/// Audio stream configuration
struct AudioConfig
{
    SampleRate sample_rate{SampleRate::Rate_48000};            ///< Sample rate in Hz
    uint32_t channels{2};                                      ///< Number of channels (1=mono, 2=stereo)
    SampleFormat format{SampleFormat::Float32};                ///< Sample format
    uint32_t buffer_frames{512};                               ///< Frames per callback (latency control)
    bool non_interleaved{true};                                ///< Use planar (non-interleaved) format
    ThreadPriority thread_priority{ThreadPriority::Elevated};  ///< Audio thread priority
};

/// Audio device information
struct DeviceInfo
{
    std::string name;                              ///< Human-readable device name
    std::string uid;                               ///< Unique device identifier
    uint32_t max_input_channels{0};                ///< Maximum input channels
    uint32_t max_output_channels{0};               ///< Maximum output channels
    std::vector<uint32_t> supported_sample_rates;  ///< Supported sample rates
    bool is_default_input{false};                  ///< Is default input device
    bool is_default_output{false};                 ///< Is default output device
};

template <typename T = float>
struct InputAudioBuffer
{
    std::span<T const> sample = {};  ///< Span to input audio frames
};

using InputAudioBufferFloat = InputAudioBuffer<float>;

template <typename T = float>
struct OutputAudioBuffer
{
    std::span<T> sample = {};  ///< Span to space for output audio frames
};

using OutputAudioBufferFloat = OutputAudioBuffer<float>;

template <typename T = float>
struct AudioCallbackParams
{
    std::span<InputAudioBuffer<T> const> input_buffers = {};    ///< Input channel buffers (nullptr if no input)
    std::span<OutputAudioBuffer<T> const> output_buffers = {};  ///< Output channel buffers (must write to these)
    double stream_time = {};                                    ///< Stream time in seconds since start
};

using AudioCallbackParamsFloat = AudioCallbackParams<float>;

/// Audio callback function type (structured parameters)
/// @param params Structured callback parameters with input/output buffers
/// @return success() to continue, failure() to stop stream
template <typename T = float>
using AudioCallback = statusbar::sg14::inplace_function<statusbar::Status(AudioCallbackParams<T> const&), 64>;

using AudioCallbackFloat = AudioCallback<float>;

}  // namespace statusbar::audio
