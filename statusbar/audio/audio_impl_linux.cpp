// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Audio Module - Linux ALSA Implementation
// Based on low-latency ALSA patterns

#if defined(__linux__)

#    include "statusbar/audio/audio.hpp"
#    include "statusbar/itc/itc_published.hpp"
#    include "statusbar/itc/itc_rt_callback_slot.hpp"
#    include "statusbar/status/catch_or_status.hpp"
#    include "statusbar/status/status.hpp"

#    include <pthread.h>

#    include <algorithm>
#    include <atomic>
#    include <chrono>
#    include <cstring>
#    include <expected>
#    include <functional>
#    include <limits>
#    include <memory>
#    include <optional>
#    include <span>
#    include <string>
#    include <thread>
#    include <vector>

#    include <alsa/asoundlib.h>

namespace statusbar::audio {

// RT-safe callback ABI: plain function pointer + user context
using AudioCallbackFn =
    int (*)(void* user, float const* const* input, float* const* output, uint32_t frames, double stream_time) noexcept;

using AudioCallbackRT = statusbar::itc::RtCallbackSlot<AudioCallbackFn>;

// Adapter to wrap std::function for RT callback
// Encapsulates the callback function and provides RT-safe access via thunk
// RAII: automatically clears the AudioCallbackRT on destruction
class CallbackHolder
{
  public:
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved) - moved in initializer list
    explicit CallbackHolder(
        AudioCallbackFloat&& callback, AudioCallbackRT& rt, uint32_t num_input_channels, uint32_t num_output_channels)
        : f_(std::move(callback))
        , rt_(&rt)
        , input_buffers_{}
        , output_buffers_{}
    {
        // Pre-allocate buffer vectors to avoid allocation in RT callback
        if (num_input_channels > 0) {
            input_buffers_.resize(num_input_channels);
        }
        if (num_output_channels > 0) {
            output_buffers_.resize(num_output_channels);
        }

        rt_->publish(&CallbackHolder::thunk, this);
    }

    ~CallbackHolder()
    {
        if (rt_ != nullptr) {
            rt_->clear();
        }
    }

    // Non-copyable, non-movable (prevent dangling rt_ pointer)
    CallbackHolder(CallbackHolder const&) = delete;
    auto operator=(CallbackHolder const&) -> CallbackHolder& = delete;
    CallbackHolder(CallbackHolder&&) = delete;
    auto operator=(CallbackHolder&&) -> CallbackHolder& = delete;

  private:
    AudioCallbackFloat f_;  // inplace_function with V2 signature
    AudioCallbackRT* rt_;   // Pointer to RT callback struct (cleared on destruction)

    // Scratch storage for building AudioCallbackParams (pre-allocated at stream start)
    std::vector<InputAudioBufferFloat> input_buffers_;
    std::vector<OutputAudioBufferFloat> output_buffers_;

    static auto thunk(void* user, float const* const* input, float* const* output, uint32_t frames, double t) noexcept -> int
    {
        auto* self = static_cast<CallbackHolder*>(user);
#    if __cpp_exceptions
        try {
#    endif
            // Build AudioCallbackParams from raw pointers
            AudioCallbackParamsFloat params{};
            params.stream_time = t;

            // Setup input buffers if present
            if (input != nullptr && !self->input_buffers_.empty()) {
                for (size_t i = 0; i < self->input_buffers_.size(); ++i) {
                    self->input_buffers_[i].sample = std::span<float const>(input[i], frames);
                }
                params.input_buffers = self->input_buffers_;
            }

            // Setup output buffers if present
            if (output != nullptr && !self->output_buffers_.empty()) {
                for (size_t i = 0; i < self->output_buffers_.size(); ++i) {
                    self->output_buffers_[i].sample = std::span<float>(output[i], frames);
                }
                params.output_buffers = self->output_buffers_;
            }

            // Call user callback and convert Status to int
            auto result = self->f_(params);
            return result.has_value() ? 0 : -1;
#    if __cpp_exceptions
        } catch (...) {
            return -1;  // Signal error on exception
        }
#    endif
    }
};

// RAII wrapper for snd_pcm_t* during initialization
// Automatically closes the PCM handle on destruction unless released
struct PcmHandleGuard
{
    snd_pcm_t* handle{nullptr};

    PcmHandleGuard() = default;

    explicit PcmHandleGuard(snd_pcm_t* h)
        : handle(h)
    {}
    ~PcmHandleGuard()
    {
        if (handle != nullptr) {
            snd_pcm_close(handle);
        }
    }

    // Non-copyable
    PcmHandleGuard(PcmHandleGuard const&) = delete;
    auto operator=(PcmHandleGuard const&) -> PcmHandleGuard& = delete;

    // Movable
    PcmHandleGuard(PcmHandleGuard&& other) noexcept
        : handle(other.handle)
    {
        other.handle = nullptr;
    }
    auto operator=(PcmHandleGuard&& other) noexcept -> PcmHandleGuard&
    {
        if (this != &other) {
            if (handle != nullptr) {
                snd_pcm_close(handle);
            }
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }

    // Release ownership - returns the handle and clears internal pointer
    auto release() noexcept -> snd_pcm_t*
    {
        snd_pcm_t* h = handle;
        handle = nullptr;
        return h;
    }

    // Drop pending audio (unblocks waiting read/write calls)
    void drop() const noexcept
    {
        if (handle != nullptr) {
            snd_pcm_drop(handle);
        }
    }

    // Close the handle and clear internal pointer
    void close() noexcept
    {
        if (handle != nullptr) {
            snd_pcm_close(handle);
            handle = nullptr;
        }
    }

    // Check if handle is valid
    explicit operator bool() const noexcept { return handle != nullptr; }
};

// Open ALSA PCM device and return RAII-wrapped handle
// @param pcm_name Device name (e.g., "hw:0,0")
// @param stream Direction: SND_PCM_STREAM_PLAYBACK or SND_PCM_STREAM_CAPTURE
auto open_pcm_device(std::string_view pcm_name, snd_pcm_stream_t stream) -> statusbar::StatusValue<PcmHandleGuard>
{
    snd_pcm_t* pcm = nullptr;
    std::string const pcm_name_str{pcm_name};
    int const err = snd_pcm_open(&pcm, pcm_name_str.c_str(), stream, 0);
    if (err < 0) {
        return failure(AudioError::DeviceNotFound);
    }
    return PcmHandleGuard(pcm);
}

// Conversion buffers for format translation between float and ALSA native formats
// Pre-allocated outside the audio loop for RT-safety (no malloc in audio callback)
struct ConversionBuffers
{
    std::vector<float> interleaved_float{};
    std::vector<int32_t> s32_buffer{};
    std::vector<int16_t> s16_buffer{};

    // Initialize buffers based on ALSA format and total sample count
    // Call once before entering the audio loop
    void initialize(snd_pcm_format_t format, size_t total_samples)
    {
        interleaved_float.resize(total_samples);

        // NOLINTBEGIN(bugprone-branch-clone) - different buffers, same operation
        if (format == SND_PCM_FORMAT_S32_LE) {
            s32_buffer.resize(total_samples);
        } else if (format == SND_PCM_FORMAT_S16_LE) {
            s16_buffer.resize(total_samples);
        }
        // NOLINTEND(bugprone-branch-clone)
    }

    // Get pointer to buffer for ALSA read operations (capture)
    // Returns buffer matching the ALSA format for direct reading
    auto read_buffer(snd_pcm_format_t format) -> void*
    {
        // NOLINTBEGIN(bugprone-branch-clone) - different buffers, same operation
        if (format == SND_PCM_FORMAT_FLOAT_LE) {
            return interleaved_float.data();
        }
        if (format == SND_PCM_FORMAT_S32_LE) {
            return s32_buffer.data();
        }
        if (format == SND_PCM_FORMAT_S16_LE) {
            return s16_buffer.data();
        }
        // NOLINTEND(bugprone-branch-clone)
        return nullptr;
    }

    // Get pointer to buffer for ALSA write operations (playback)
    // Converts from interleaved_float to ALSA format, returns buffer for writing
    auto write_buffer(snd_pcm_format_t format, size_t total_samples) -> void*
    {
        if (format == SND_PCM_FORMAT_FLOAT_LE) {
            return interleaved_float.data();
        }
        if (format == SND_PCM_FORMAT_S32_LE) {
            convert_float_to_s32(interleaved_float.data(), s32_buffer.data(), total_samples);
            return s32_buffer.data();
        }
        if (format == SND_PCM_FORMAT_S16_LE) {
            convert_float_to_s16(interleaved_float.data(), s16_buffer.data(), total_samples);
            return s16_buffer.data();
        }
        return nullptr;
    }

    // Convert from ALSA format to interleaved float (for capture)
    // Call after reading into the appropriate buffer
    void convert_to_float(snd_pcm_format_t format, size_t total_samples)
    {
        if (format == SND_PCM_FORMAT_S32_LE) {
            convert_s32_to_float(s32_buffer.data(), interleaved_float.data(), total_samples);
        } else if (format == SND_PCM_FORMAT_S16_LE) {
            convert_s16_to_float(s16_buffer.data(), interleaved_float.data(), total_samples);
        }
        // FLOAT_LE: already in interleaved_float, nothing to do
    }
};

// Utility Functions

namespace {

// Audio loop parameters computed once at thread start
// Validates and derives all size/format parameters needed for the audio loop
struct AudioLoopParams
{
    uint32_t period_frames;  // Period size in frames (validated to fit uint32_t)
    uint32_t channels;       // Channel count
    size_t total_samples;    // period_frames * channels
    size_t bytes_per_frame;  // Physical bytes per interleaved frame
    double sample_rate;      // Sample rate as double for time calculations
};

// Compute audio loop parameters from negotiated ALSA settings
// Returns nullopt if any parameter is invalid
auto make_audio_loop_params(snd_pcm_uframes_t period_frames, uint32_t channels, uint32_t sample_rate, snd_pcm_format_t format)
    -> std::optional<AudioLoopParams>
{
    // Validate period_size fits in uint32_t (callback API uses uint32_t frames parameter)
    if (period_frames > std::numeric_limits<uint32_t>::max()) {
        return std::nullopt;
    }

    // Calculate bytes per frame from ALSA format physical width
    int const bits_per_sample = snd_pcm_format_physical_width(format);
    if (bits_per_sample <= 0) {
        return std::nullopt;
    }
    size_t const bpf = channels * (static_cast<size_t>(bits_per_sample) / 8);
    if (bpf == 0) {
        return std::nullopt;
    }

    return AudioLoopParams{
        .period_frames = static_cast<uint32_t>(period_frames),
        .channels = channels,
        .total_samples = static_cast<size_t>(period_frames) * channels,
        .bytes_per_frame = bpf,
        .sample_rate = static_cast<double>(sample_rate),
    };
}

auto get_device_description(std::string_view pcm_name) -> std::string
{
    snd_ctl_t* ctl = nullptr;
    snd_ctl_card_info_t* info = nullptr;

    // Extract card number from PCM name (e.g., "hw:0,0" -> "hw:0")
    std::string card_name(pcm_name);
    auto comma_pos = card_name.find(',');
    if (comma_pos != std::string::npos) {
        card_name = card_name.substr(0, comma_pos);
    }

    if (snd_ctl_open(&ctl, card_name.c_str(), 0) < 0) {
        return std::string(pcm_name);
    }

    snd_ctl_card_info_alloca(&info);
    if (snd_ctl_card_info(ctl, info) >= 0) {
        std::string desc = snd_ctl_card_info_get_name(info);
        snd_ctl_close(ctl);
        return desc;
    }

    snd_ctl_close(ctl);
    return std::string(pcm_name);
}

auto probe_sample_rates(std::string_view pcm_name, snd_pcm_stream_t stream) -> std::vector<uint32_t>
{
    std::vector<uint32_t> rates;
    snd_pcm_t* pcm = nullptr;
    std::string const pcm_name_str{pcm_name};

    if (snd_pcm_open(&pcm, pcm_name_str.c_str(), stream, SND_PCM_NONBLOCK) < 0) {
        return {44100, 48000, 96000};  // Default fallback
    }

    snd_pcm_hw_params_t* hw_params = nullptr;
    snd_pcm_hw_params_alloca(&hw_params);

    if (snd_pcm_hw_params_any(pcm, hw_params) >= 0) {
        // Test common sample rates (including 88.2kHz family for completeness)
        std::vector<uint32_t> const test_rates = {44100, 48000, 88200, 96000, 176400, 192000};
        for (auto rate : test_rates) {
            unsigned int const test_rate = rate;
            if (snd_pcm_hw_params_test_rate(pcm, hw_params, test_rate, 0) >= 0) {
                rates.push_back(rate);
            }
        }
    }

    snd_pcm_close(pcm);

    if (rates.empty()) {
        return {44100, 48000, 96000};  // Default fallback
    }

    // Sort and deduplicate results (ensures consistent ordering for UX)
    std::sort(rates.begin(), rates.end());
    rates.erase(std::unique(rates.begin(), rates.end()), rates.end());

    return rates;
}

auto get_max_channels(std::string_view pcm_name, snd_pcm_stream_t stream) -> uint32_t
{
    snd_pcm_t* pcm = nullptr;
    std::string const pcm_name_str{pcm_name};

    if (snd_pcm_open(&pcm, pcm_name_str.c_str(), stream, SND_PCM_NONBLOCK) < 0) {
        return 0;
    }

    snd_pcm_hw_params_t* hw_params = nullptr;
    snd_pcm_hw_params_alloca(&hw_params);

    unsigned int max_channels = 0;
    if (snd_pcm_hw_params_any(pcm, hw_params) >= 0) {
        snd_pcm_hw_params_get_channels_max(hw_params, &max_channels);
    }

    snd_pcm_close(pcm);
    return max_channels;
}

// Try multiple ALSA formats in priority order (FLOAT -> S32 -> S16)
// Returns the format that was successfully set, or error
auto try_set_format(snd_pcm_t* pcm, snd_pcm_hw_params_t* hw_params) -> std::optional<snd_pcm_format_t>
{
    // Try formats in order of preference
    std::vector<snd_pcm_format_t> const formats = {
        SND_PCM_FORMAT_FLOAT_LE,  // Preferred: no conversion needed
        SND_PCM_FORMAT_S32_LE,    // Fallback 1: high quality
        SND_PCM_FORMAT_S16_LE     // Fallback 2: universal support
    };

    // CRITICAL: Each format attempt needs a clean hw_params (failed set_format can dirty it)
    // This is especially important for capture devices where format constraints are stricter
    snd_pcm_hw_params_t* tmp_params;
    snd_pcm_hw_params_alloca(&tmp_params);

    for (auto fmt : formats) {
        // Copy base hw_params to tmp for this attempt (ensures clean slate per format)
        snd_pcm_hw_params_copy(tmp_params, hw_params);

        if (snd_pcm_hw_params_set_format(pcm, tmp_params, fmt) >= 0) {
            // Success - copy tmp back to hw_params so caller gets the working params
            snd_pcm_hw_params_copy(hw_params, tmp_params);
            return fmt;
        }
        // Failure - tmp is discarded, hw_params remains clean for next attempt
    }

    return std::nullopt;  // All formats failed
}

// Note: Sample format conversion functions are now provided by the :convert module partition
// (clamp_sample, convert_float_to_s16, convert_float_to_s32, convert_s16_to_float, convert_s32_to_float)

// Set thread priority based on configuration
// Returns true on success, false on failure
auto set_thread_priority(ThreadPriority priority) -> bool
{
    if (priority == ThreadPriority::Normal) {
        // Normal priority - nothing to do
        return true;
    }

    // Try to set realtime priority (SCHED_FIFO with priority 80)
    // Priority range is typically 1-99, with 80-90 being common for audio
    struct sched_param param{};
    param.sched_priority = 80;

    int const result = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    if (result == 0) {
        // Successfully set realtime priority
        return true;
    }

    // Failed to set realtime priority
    if (priority == ThreadPriority::Realtime) {
        // Realtime required - fail
        return false;
    }

    // Elevated mode - fallback to normal is acceptable
    return true;
}

// Handle ALSA suspend error (ESTRPIPE)
// Returns true if recovered successfully, false if should stop
auto handle_pcm_suspend(snd_pcm_t* pcm_handle, std::atomic<bool>& running) -> bool
{
    // Short-circuit if stop was requested (avoid recovery work after stop())
    if (!running.load(std::memory_order_acquire)) {
        return false;
    }

    int err = 0;
    // System suspended - resume the stream
    while ((err = snd_pcm_resume(pcm_handle)) == -EAGAIN) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // Check if stop was requested during resume loop
        if (!running.load(std::memory_order_acquire)) {
            return false;
        }
    }
    // If resume failed, prepare as fallback
    if (err < 0) {
        err = snd_pcm_prepare(pcm_handle);
        if (err < 0) {
            // Prepare failed - stop audio
            running.store(false, std::memory_order_release);
            return false;
        }
    }
    return true;  // Recovered successfully
}

// Handle ALSA XRUN error (EPIPE - underrun/overrun)
// Returns true if recovered successfully, false if should stop
auto handle_pcm_xrun(snd_pcm_t* pcm_handle, std::atomic<bool>& running) -> bool
{
    // Short-circuit if stop was requested (avoid recovery work after stop())
    if (!running.load(std::memory_order_acquire)) {
        return false;
    }

    // XRUN occurred - prepare the stream
    int const err = snd_pcm_prepare(pcm_handle);
    if (err < 0) {
        // Prepare failed - stop audio
        running.store(false, std::memory_order_release);
        return false;
    }
    return true;  // Recovered successfully
}

// Handle generic ALSA error with snd_pcm_recover
// Returns true if recovered successfully, false if should stop
auto handle_pcm_error(snd_pcm_t* pcm_handle, int err, std::atomic<bool>& running) -> bool
{
    // Short-circuit if stop was requested (avoid recovery work after stop())
    if (!running.load(std::memory_order_acquire)) {
        return false;
    }

    // For other errors, use generic snd_pcm_recover
    // Note: snd_pcm_recover returns 0 on success, not frames
    if (snd_pcm_recover(pcm_handle, err, 1) < 0) {
        // Recovery failed - stop audio
        running.store(false, std::memory_order_release);
        return false;
    }
    return true;  // Recovered successfully
}

}  // namespace

// DeviceManagerLinux Implementation

/// Build a DeviceInfo from an ALSA device hint
/// @return DeviceInfo if the hint is valid and has channels, nullopt otherwise
auto build_device_info_from_hint(void* hint) -> std::optional<DeviceInfo>
{
    char* name = snd_device_name_get_hint(hint, "NAME");
    char* desc = snd_device_name_get_hint(hint, "DESC");
    char* ioid = snd_device_name_get_hint(hint, "IOID");

    auto cleanup = [&]() {
        free(name);
        free(desc);
        free(ioid);
    };

    if (name == nullptr || std::string_view(name) == "null") {
        cleanup();
        return std::nullopt;
    }

    DeviceInfo info;
    info.uid = name;
    std::string_view const name_view(name);

    if (desc != nullptr) {
        std::string desc_str(desc);
        std::replace(desc_str.begin(), desc_str.end(), '\n', ' ');
        info.name = desc_str;
    } else {
        info.name = name;
    }

    bool const is_input = (ioid == nullptr) || std::string_view(ioid) == "Input";
    bool const is_output = (ioid == nullptr) || std::string_view(ioid) == "Output";

    info.max_input_channels = is_input ? get_max_channels(name, SND_PCM_STREAM_CAPTURE) : 0;
    info.max_output_channels = is_output ? get_max_channels(name, SND_PCM_STREAM_PLAYBACK) : 0;

    if (is_input) {
        info.supported_sample_rates = probe_sample_rates(name, SND_PCM_STREAM_CAPTURE);
    } else if (is_output) {
        info.supported_sample_rates = probe_sample_rates(name, SND_PCM_STREAM_PLAYBACK);
    }

    info.is_default_input = (name_view == "default");
    info.is_default_output = (name_view == "default");

    cleanup();

    if (info.max_input_channels == 0 && info.max_output_channels == 0) {
        return std::nullopt;
    }

    return info;
}

auto DeviceManagerLinux::enumerate_devices_impl() -> std::vector<DeviceInfo>
{
    std::vector<DeviceInfo> result;
    void** hints = nullptr;

    // Use ALSA device hints API for comprehensive enumeration
    if (snd_device_name_hint(-1, "pcm", &hints) < 0) {
        return result;  // Enumeration failed
    }

    for (void** hint = hints; *hint != nullptr; ++hint) {
        if (auto info = build_device_info_from_hint(*hint)) {
            result.push_back(std::move(*info));
        }
    }

    snd_device_name_free_hint(hints);

    // Ensure "default" appears first if present
    auto default_it = std::find_if(result.begin(), result.end(), [](DeviceInfo const& d) { return d.uid == "default"; });

    if (default_it != result.end() && default_it != result.begin()) {
        std::rotate(result.begin(), default_it, default_it + 1);
    }

    return result;
}

auto DeviceManagerLinux::default_output_device_impl() -> std::optional<DeviceInfo>
{
    // First, enumerate all devices
    auto all_devices = enumerate_devices_impl();

    // Try to find "default" device with output channels
    for (auto const& dev : all_devices) {
        if (dev.uid == "default" && dev.max_output_channels > 0) {
            return dev;
        }
    }

    // Fallback: return first device with output channels
    // Mark it as default since it's being used as the system default
    for (auto& dev : all_devices) {
        if (dev.max_output_channels > 0) {
            dev.is_default_output = true;
            return dev;
        }
    }

    return std::nullopt;
}

auto DeviceManagerLinux::default_input_device_impl() -> std::optional<DeviceInfo>
{
    // First, enumerate all devices
    auto all_devices = enumerate_devices_impl();

    // Try to find "default" device with input channels
    for (auto const& dev : all_devices) {
        if (dev.uid == "default" && dev.max_input_channels > 0) {
            return dev;
        }
    }

    // Fallback: return first device with input channels
    // Mark it as default since it's being used as the system default
    for (auto& dev : all_devices) {
        if (dev.max_input_channels > 0) {
            dev.is_default_input = true;
            return dev;
        }
    }

    return std::nullopt;
}

auto DeviceManagerLinux::find_device_impl(std::string_view identifier, bool is_input) -> std::optional<DeviceInfo>
{
    // Handle "default" keyword
    if (identifier == "default") {
        return is_input ? default_input_device_impl() : default_output_device_impl();
    }

    // Search by UID or name
    auto devices = enumerate_devices_impl();
    for (auto const& dev : devices) {
        if (dev.uid == identifier || dev.name == identifier) {
            // Verify device has appropriate channels
            if (is_input && dev.max_input_channels == 0) {
                continue;
            }
            if (!is_input && dev.max_output_channels == 0) {
                continue;
            }
            return dev;
        }
    }

    // Not found in enumerated devices — treat identifier as a raw ALSA device name
    // This supports plughw:, hw:, and other ALSA device strings not in the hints list
    if (identifier.find(':') != std::string_view::npos || identifier.find("hw") != std::string_view::npos) {
        std::string const name{identifier};
        DeviceInfo info;
        info.uid = name;
        info.name = name;
        info.max_input_channels = is_input ? get_max_channels(name, SND_PCM_STREAM_CAPTURE) : 0;
        info.max_output_channels = is_input ? 0 : get_max_channels(name, SND_PCM_STREAM_PLAYBACK);
        // If probing failed, use a reasonable default
        if (is_input && info.max_input_channels == 0) {
            info.max_input_channels = 2;
        }
        if (!is_input && info.max_output_channels == 0) {
            info.max_output_channels = 2;
        }
        info.is_default_input = false;
        info.is_default_output = false;
        return info;
    }

    return std::nullopt;
}

// DeviceManager Forwarding Implementation

auto DeviceManager::enumerate_devices() -> std::vector<DeviceInfo>
{
    return DeviceManagerLinux::enumerate_devices_impl();
}

auto DeviceManager::default_input_device() -> std::optional<DeviceInfo>
{
    return DeviceManagerLinux::default_input_device_impl();
}

auto DeviceManager::default_output_device() -> std::optional<DeviceInfo>
{
    return DeviceManagerLinux::default_output_device_impl();
}

auto DeviceManager::find_device(std::string_view identifier, bool is_input) -> std::optional<DeviceInfo>
{
    return DeviceManagerLinux::find_device_impl(identifier, is_input);
}

// Shared state and hardware-configuration helpers for the two Linux stream
// directions. Output and Input differ only in the audio loop (generate vs
// process) and the software-params (playback vs capture); the members below and
// the hw-params negotiation are byte-for-byte identical, so they live here once.
struct StreamImplLinuxBase
{
    PcmHandleGuard pcm_handle;               // RAII-managed PCM handle
    AudioCallbackRT cb_rt;                   // RT-safe callback slot (coherent fn/ctx pair via triple buffer)
    std::unique_ptr<CallbackHolder> holder;  // Manages std::function lifetime
    AudioConfig config;                      // Requested configuration (immutable after construction)
    DeviceInfo device_info;
    std::atomic<bool> running{false};
    statusbar::itc::Published<double> stream_time{0.0};
    std::atomic<uint64_t> total_frames{0};
    std::thread audio_thread;

    // ALSA-negotiated parameters (set during start(), may differ from requested config)
    snd_pcm_format_t alsa_format{SND_PCM_FORMAT_FLOAT_LE};  // Actual ALSA format
    uint32_t negotiated_sample_rate{48000};                 // Actual sample rate (may not be in enum)
    snd_pcm_uframes_t negotiated_period_frames{512};        // Actual period size (callback buffer size)
    snd_pcm_uframes_t negotiated_buffer_frames{1536};       // Actual buffer size (total HW buffer)

    // Channel buffers for non-interleaved format
    std::vector<std::vector<float>> channel_buffers;
    std::vector<float*> channel_ptrs;

    // Observability: xrun tally and the reason the stream last stopped/errored.
    // Written by the audio thread, polled by the control thread.
    std::atomic<uint64_t> xrun_count{0};
    std::atomic<AudioError> last_error{AudioError::None};

    // Signal audio loop to stop (used from within audio_loop and helpers)
    void audio_loop_stop() { running.store(false, std::memory_order_release); }

    // Record why the stream stopped/errored (audio thread → control thread).
    void set_last_error(AudioError e) { last_error.store(e, std::memory_order_release); }

    // Check if audio loop should continue running
    bool audio_loop_is_running() const { return running.load(std::memory_order_acquire); }

    // Get current total frames count (relaxed ordering for audio loop use)
    uint64_t audio_loop_get_total_frames() const { return total_frames.load(std::memory_order_relaxed); }

    // Update stream time from total_frames counter
    void update_stream_time(double sample_rate)
    {
        stream_time.publish(static_cast<double>(audio_loop_get_total_frames()) / sample_rate);
    }

    ~StreamImplLinuxBase()
    {
        // Signal audio thread to exit
        audio_loop_stop();

        // Drop PCM first to unblock thread if it's waiting in snd_pcm_writei()/readi()
        pcm_handle.drop();

        if (audio_thread.joinable()) {
            // Wait for audio thread to complete
            audio_thread.join();
        }

        // holder.reset() clears cb_rt via CallbackHolder destructor
        // pcm_handle automatically closed by PcmHandleGuard destructor
    }

    // Configure and apply ALSA hardware parameters (format/rate/channels/period).
    statusbar::Status configure_hw_params(snd_pcm_t* pcm, snd_pcm_hw_params_t* hw_params, unsigned int& periods)
    {
        int err = snd_pcm_hw_params_any(pcm, hw_params);
        if (err < 0) {
            return failure(AudioError::InitializationFailed);
        }

        // Set access type (interleaved)
        err = snd_pcm_hw_params_set_access(pcm, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
        if (err < 0) {
            return failure(AudioError::UnsupportedFormat);
        }

        // Try format fallback: FLOAT -> S32 -> S16
        auto format_result = try_set_format(pcm, hw_params);
        if (!format_result) {
            return failure(AudioError::UnsupportedFormat);
        }
        alsa_format = *format_result;

        // Set channels
        err = snd_pcm_hw_params_set_channels(pcm, hw_params, config.channels);
        if (err < 0) {
            return failure(AudioError::UnsupportedFormat);
        }

        // Set sample rate
        unsigned int rate = static_cast<unsigned int>(config.sample_rate);
        err = snd_pcm_hw_params_set_rate_near(pcm, hw_params, &rate, nullptr);
        if (err < 0) {
            return failure(AudioError::UnsupportedFormat);
        }

        // Set period size (callback size)
        snd_pcm_uframes_t period_size = config.buffer_frames;
        err = snd_pcm_hw_params_set_period_size_near(pcm, hw_params, &period_size, nullptr);
        if (err < 0) {
            return failure(AudioError::InitializationFailed);
        }

        // Set number of periods in hardware buffer (2-4 periods recommended)
        periods = 3;  // 3 periods for balanced latency/robustness
        err = snd_pcm_hw_params_set_periods_near(pcm, hw_params, &periods, nullptr);
        if (err < 0) {
            // Not critical - ALSA will choose a reasonable default
            periods = 3;
        }

        // Set buffer size to control total latency
        snd_pcm_uframes_t buffer_size = period_size * periods;
        err = snd_pcm_hw_params_set_buffer_size_near(pcm, hw_params, &buffer_size);
        if (err < 0) {
            // Not critical - ALSA will choose a reasonable default
        }

        // Apply hardware parameters
        err = snd_pcm_hw_params(pcm, hw_params);
        if (err < 0) {
            return failure(AudioError::InitializationFailed);
        }

        return success();
    }

    // Query back actual negotiated parameters from ALSA
    statusbar::Status query_negotiated_params(snd_pcm_t* pcm, unsigned int& actual_periods)
    {
        snd_pcm_hw_params_t* cur_params = nullptr;
        snd_pcm_hw_params_alloca(&cur_params);

        int const err = snd_pcm_hw_params_current(pcm, cur_params);
        if (err < 0) {
            return failure(AudioError::InitializationFailed);
        }

        unsigned int rate = 0;
        snd_pcm_hw_params_get_rate(cur_params, &rate, nullptr);
        negotiated_sample_rate = rate;

        snd_pcm_uframes_t actual_period_size = 0;
        snd_pcm_hw_params_get_period_size(cur_params, &actual_period_size, nullptr);
        negotiated_period_frames = actual_period_size;

        snd_pcm_hw_params_get_periods(cur_params, &actual_periods, nullptr);

        snd_pcm_uframes_t actual_buffer_size = 0;
        snd_pcm_hw_params_get_buffer_size(cur_params, &actual_buffer_size);
        negotiated_buffer_frames = actual_buffer_size;

        return success();
    }

    // Allocate channel buffers using negotiated period size
    void allocate_channel_buffers()
    {
        channel_buffers.resize(config.channels);
        channel_ptrs.resize(config.channels);
        for (uint32_t i = 0; i < config.channels; ++i) {
            channel_buffers[i].resize(negotiated_period_frames);
            channel_ptrs[i] = channel_buffers[i].data();
        }
    }

    // Prepare PCM for playback/capture
    statusbar::Status prepare_pcm(snd_pcm_t* pcm)
    {
        int const err = snd_pcm_prepare(pcm);
        if (err < 0) {
            return failure(AudioError::InitializationFailed);
        }
        return success();
    }
};

// OutputStreamLinux Implementation

struct OutputStreamLinux::Impl : StreamImplLinuxBase
{
    // Invoke user callback and interleave into output buffer
    // Returns true to continue, false to stop
    bool audio_loop_generate(uint32_t period_size, uint32_t channels, double sample_rate, ConversionBuffers& buffers)
    {
        // Load callback atomically from RT-safe storage
        auto const cb = cb_rt.load();

        if (cb.fn == nullptr) {
            // No callback set - render silence instead of stopping
            std::fill(buffers.interleaved_float.begin(), buffers.interleaved_float.end(), 0.0F);
        } else {
            // Call user callback with stream time at start of this period
            double const stream_time_param = static_cast<double>(audio_loop_get_total_frames()) / sample_rate;

            // Call user callback via RT-safe function pointer (noexcept, no std::function on RT thread)
            int const result = cb.fn(cb.ctx, nullptr, channel_ptrs.data(), period_size, stream_time_param);

            // CallbackHolder::thunk catches exceptions and returns -1
            if (result < 0) {
                // Error from callback - stop audio
                set_last_error(AudioError::CallbackError);
                audio_loop_stop();
                return false;
            }

            // Convert non-interleaved to interleaved (always into interleaved_float first)
            for (uint32_t frame = 0; frame < period_size; ++frame) {
                for (uint32_t ch = 0; ch < channels; ++ch) {
                    buffers.interleaved_float[(frame * channels) + ch] = channel_buffers[ch][frame];
                }
            }
        }

        return true;
    }

    // Handle ALSA write errors (suspend, underrun, other)
    // Returns true if recovered and should retry, false if should stop
    bool handle_write_error(int err)
    {
        if (err == -ESTRPIPE) {
            return handle_pcm_suspend(pcm_handle.handle, running);
        }
        if (err == -EPIPE) {
            xrun_count.fetch_add(1, std::memory_order_relaxed);
            set_last_error(AudioError::BufferUnderrun);
            return handle_pcm_xrun(pcm_handle.handle, running);
        }
        return handle_pcm_error(pcm_handle.handle, err, running);
    }

    // Write one full period to ALSA, handling errors and partial writes
    // Returns true on success, false if audio should stop
    bool audio_loop_write(snd_pcm_uframes_t period_size, void* write_buffer, size_t bytes_per_frame)
    {
        snd_pcm_sframes_t frames_written_total = 0;
        uint8_t* const buffer_base = static_cast<uint8_t*>(write_buffer);

        while (frames_written_total < static_cast<snd_pcm_sframes_t>(period_size) && audio_loop_is_running()) {
            // Compute frame offset from base (makes frame vs byte semantics clear)
            snd_pcm_uframes_t const frames_offset = static_cast<snd_pcm_uframes_t>(frames_written_total);
            void* const write_ptr = buffer_base + (frames_offset * bytes_per_frame);
            snd_pcm_uframes_t const remaining = period_size - frames_offset;

            snd_pcm_sframes_t const frames_written = snd_pcm_writei(pcm_handle.handle, write_ptr, remaining);

            if (frames_written < 0) {
                if (!handle_write_error(static_cast<int>(frames_written))) {
                    return false;  // Recovery failed - stop audio
                }
                continue;  // Retry after recovery
            }

            // Advance counters for partial writes
            frames_written_total += frames_written;
            total_frames.fetch_add(frames_written, std::memory_order_relaxed);
        }

        // If we were interrupted (running became false), signal stop
        return frames_written_total == static_cast<snd_pcm_sframes_t>(period_size);
    }

    // Stream Initialization Helper Functions
    // These functions take snd_pcm_t* as a parameter so ownership can remain
    // with the caller's RAII guard until initialization is complete.

    // Configure and apply ALSA hardware parameters
    // Configure and apply ALSA software parameters for playback
    statusbar::Status configure_sw_params_playback(snd_pcm_t* pcm, unsigned int actual_periods) const
    {
        snd_pcm_sw_params_t* sw_params = nullptr;
        snd_pcm_sw_params_alloca(&sw_params);

        int err = snd_pcm_sw_params_current(pcm, sw_params);
        if (err < 0) {
            return failure(AudioError::InitializationFailed);
        }

        // Start playback threshold depends on thread priority
        snd_pcm_uframes_t start_threshold = negotiated_period_frames;
        if (config.thread_priority == ThreadPriority::Normal && actual_periods >= 2) {
            start_threshold = negotiated_period_frames * 2;
        }
        err = snd_pcm_sw_params_set_start_threshold(pcm, sw_params, start_threshold);
        if (err < 0) {
            return failure(AudioError::InitializationFailed);
        }

        // Wake up callback when at least one period is available
        snd_pcm_sw_params_set_avail_min(pcm, sw_params, negotiated_period_frames);

        // Stop threshold: stop PCM when buffer drains completely
        snd_pcm_sw_params_set_stop_threshold(pcm, sw_params, negotiated_buffer_frames);

        // Silence threshold: fill buffer tail with silence on underrun
        snd_pcm_sw_params_set_silence_threshold(pcm, sw_params, 0);

        // Silence size: fill entire buffer with silence when threshold is reached
        snd_pcm_sw_params_set_silence_size(pcm, sw_params, negotiated_buffer_frames);

        err = snd_pcm_sw_params(pcm, sw_params);
        if (err < 0) {
            return failure(AudioError::InitializationFailed);
        }

        return success();
    }

    // Set up callback and start audio thread
    // Output stream: 0 input channels, config.channels output channels
    void start_audio_thread(AudioCallbackFloat&& callback)
    {
        // Join a thread left over from a prior self-stop (callback failure or
        // denied RT priority): assigning over a joinable std::thread terminates.
        if (audio_thread.joinable()) {
            audio_thread.join();
        }

        // Fresh run: clear observability state carried from a previous run.
        xrun_count.store(0, std::memory_order_relaxed);
        last_error.store(AudioError::None, std::memory_order_release);

        holder = std::make_unique<CallbackHolder>(std::move(callback), cb_rt, 0, config.channels);

        running.store(true, std::memory_order_release);
        audio_thread = std::thread([this]() {
            // Exception barrier (run_guarded): an exception escaping a thread
            // entry calls std::terminate and aborts the process. Always mark the
            // stream stopped on exit so a thread that died on a throw doesn't
            // look alive.
            run_guarded("audio thread", [this]() { audio_loop(); });
            running.store(false, std::memory_order_release);
        });
    }

    void audio_loop()
    {
        // Set thread priority based on configuration
        if (!set_thread_priority(config.thread_priority)) {
            // Failed to set required realtime priority - stop audio
            set_last_error(AudioError::RealtimePriorityDenied);
            audio_loop_stop();
            return;
        }

        // Compute and validate audio loop parameters
        auto const params = make_audio_loop_params(negotiated_period_frames, config.channels, negotiated_sample_rate, alsa_format);
        if (!params) {
            audio_loop_stop();
            return;
        }

        // Preallocate conversion buffers (RT-safety: no malloc in audio loop)
        ConversionBuffers buffers;
        buffers.initialize(alsa_format, params->total_samples);

        while (audio_loop_is_running()) {
            // Invoke callback and interleave into output buffer
            if (!audio_loop_generate(params->period_frames, params->channels, params->sample_rate, buffers)) {
                break;
            }

            // Apply format conversion from float to ALSA format and get buffer pointer
            void* write_ptr = buffers.write_buffer(alsa_format, params->total_samples);
            if (write_ptr == nullptr) {
                // Invalid format - stop audio
                audio_loop_stop();
                break;
            }

            // Write one full period to ALSA
            if (!audio_loop_write(params->period_frames, write_ptr, params->bytes_per_frame)) {
                break;
            }

            // Update stream time based on actual frames written
            update_stream_time(params->sample_rate);
        }
    }
};

OutputStreamLinux::OutputStreamLinux(DeviceInfo device, AudioConfig config)
    : impl_(std::make_unique<Impl>())
{
    impl_->device_info = std::move(device);
    impl_->config = config;
}

OutputStreamLinux::~OutputStreamLinux() noexcept = default;

auto OutputStreamLinux::start(AudioCallbackFloat&& callback) -> statusbar::Status
{
    if (impl_->running.load(std::memory_order_acquire)) {
        return failure(AudioError::AlreadyRunning);
    }

    impl_->total_frames.store(0, std::memory_order_relaxed);
    impl_->stream_time.publish(0.0);

    // Open ALSA PCM device (RAII guard ensures handle is closed on any failure)
    auto guard_result = open_pcm_device(impl_->device_info.uid, SND_PCM_STREAM_PLAYBACK);
    if (!guard_result) {
        return failure(guard_result.error());
    }
    auto guard = std::move(*guard_result);
    snd_pcm_t* pcm = guard.handle;

    // Configure and apply hardware parameters
    snd_pcm_hw_params_t* hw_params = nullptr;
    snd_pcm_hw_params_alloca(&hw_params);
    unsigned int periods = 0;

    if (auto status = impl_->configure_hw_params(pcm, hw_params, periods); !status) {
        return status;
    }

    // Query back actual negotiated parameters
    unsigned int actual_periods = 0;
    if (auto status = impl_->query_negotiated_params(pcm, actual_periods); !status) {
        return status;
    }

    // Allocate channel buffers using negotiated period size
    impl_->allocate_channel_buffers();

    // Configure and apply software parameters for playback
    if (auto status = impl_->configure_sw_params_playback(pcm, actual_periods); !status) {
        return status;
    }

    // Prepare PCM
    if (auto status = impl_->prepare_pcm(pcm); !status) {
        return status;
    }

    // Success - transfer ownership from guard to impl
    impl_->pcm_handle = std::move(guard);

    // Set up callback and start audio thread
    impl_->start_audio_thread(std::move(callback));

    return success();
}

void OutputStreamLinux::stop()
{
    // Drop pending audio only if we are the one stopping a running stream — this
    // unblocks a thread waiting in snd_pcm_writei. A self-stopped stream (the
    // callback returned failure, or RT priority was denied) has already exited
    // the loop, so running is false here.
    bool const was_running = impl_->running.exchange(false, std::memory_order_acq_rel);
    if (was_running) {
        impl_->pcm_handle.drop();
    }

    // Always join a lingering thread, even on a self-stop where running was
    // already false: leaving it joinable would terminate the process when the
    // next start() assigns over impl_->audio_thread. Idempotent across repeat
    // stop() calls (not joinable after the first).
    if (impl_->audio_thread.joinable()) {
        impl_->audio_thread.join();
    }

    // Clear callback after thread has stopped (CallbackHolder destructor clears cb_rt)
    impl_->holder.reset();

    // Close and cleanup PCM handle for clean restart
    impl_->pcm_handle.close();
}

auto OutputStreamLinux::is_running() const noexcept -> bool
{
    return impl_->running.load(std::memory_order_acquire);
}

auto OutputStreamLinux::config() const noexcept -> AudioConfig const&
{
    return impl_->config;
}

auto OutputStreamLinux::stream_time() const noexcept -> double
{
    return impl_->stream_time.load();
}

auto OutputStreamLinux::device() const noexcept -> DeviceInfo const&
{
    return impl_->device_info;
}

auto OutputStreamLinux::effective_sample_rate() const noexcept -> double
{
    return static_cast<double>(impl_->negotiated_sample_rate);
}

auto OutputStreamLinux::effective_period_frames() const noexcept -> uint32_t
{
    // Validate fits in uint32_t (same check as audio_loop thread)
    if (impl_->negotiated_period_frames > std::numeric_limits<uint32_t>::max()) {
        return std::numeric_limits<uint32_t>::max();
    }
    return static_cast<uint32_t>(impl_->negotiated_period_frames);
}

auto OutputStreamLinux::effective_buffer_frames() const noexcept -> uint32_t
{
    // Validate fits in uint32_t
    if (impl_->negotiated_buffer_frames > std::numeric_limits<uint32_t>::max()) {
        return std::numeric_limits<uint32_t>::max();
    }
    return static_cast<uint32_t>(impl_->negotiated_buffer_frames);
}

auto OutputStreamLinux::xrun_count() const noexcept -> uint64_t
{
    return impl_->xrun_count.load(std::memory_order_relaxed);
}

auto OutputStreamLinux::last_error() const noexcept -> AudioError
{
    return impl_->last_error.load(std::memory_order_acquire);
}

// OutputStream Factory

auto OutputStream::create(std::string_view device_uid, AudioConfig const& config)
    -> statusbar::StatusValue<std::unique_ptr<OutputStream>>
{
    auto device_info = DeviceManager::find_device(device_uid, false);
    if (!device_info) {
        return failure(AudioError::DeviceNotFound);
    }

#    if __cpp_exceptions
    try {
#    endif
        return std::make_unique<OutputStreamLinux>(*device_info, config);
#    if __cpp_exceptions
    } catch (...) {
        return failure(AudioError::InitializationFailed);
    }
#    endif
}

// InputStreamLinux Implementation

struct InputStreamLinux::Impl : StreamImplLinuxBase
{
    // Handle ALSA read errors (suspend, overrun, other)
    // Returns true if recovered and should retry, false if should stop
    bool handle_read_error(int err)
    {
        if (err == -ESTRPIPE) {
            return handle_pcm_suspend(pcm_handle.handle, running);
        }
        if (err == -EPIPE) {
            xrun_count.fetch_add(1, std::memory_order_relaxed);
            set_last_error(AudioError::BufferOverrun);
            return handle_pcm_xrun(pcm_handle.handle, running);
        }
        return handle_pcm_error(pcm_handle.handle, err, running);
    }

    // Handle zero frames read (wait for data to become available)
    // Returns true if should retry, false if should stop
    bool handle_zero_frames_read(int& timeout_count)
    {
        // Short-circuit if stop was requested (avoid recovery work after stop())
        if (!audio_loop_is_running()) {
            return false;
        }

        // No data available - wait for PCM to become ready
        // Use shorter timeout (100ms) for better stop() responsiveness
        // snd_pcm_wait() returns: >0 (ready), 0 (timeout), <0 (error)
        int const r = snd_pcm_wait(pcm_handle.handle, 100);

        if (r < 0) {
            // Error occurred during wait - attempt recovery (check running again to avoid work after stop)
            if (!audio_loop_is_running()) {
                return false;
            }
            if (snd_pcm_recover(pcm_handle.handle, r, 1) < 0) {
                // Recovery failed - stop audio
                audio_loop_stop();
                return false;
            }
            timeout_count = 0;  // Reset on error (recovery might have fixed it)
        } else if (r == 0) {
            // Timeout - increment counter and prepare PCM after too many consecutive timeouts
            timeout_count++;
            if (timeout_count >= 10) {
                // Too many timeouts - device may be stuck. Check PCM state before prepare()
                // Only call prepare() if the device is in a bad state (XRUN, SUSPENDED, DISCONNECTED)
                // to avoid unnecessary glitches on quiet/gated input devices
                auto const is_pcm_error_state = [](snd_pcm_state_t s) {
                    return s == SND_PCM_STATE_XRUN || s == SND_PCM_STATE_SUSPENDED || s == SND_PCM_STATE_DISCONNECTED;
                };
                snd_pcm_state_t const state = snd_pcm_state(pcm_handle.handle);
                if (is_pcm_error_state(state)) {
                    int const err = snd_pcm_prepare(pcm_handle.handle);
                    if (err < 0) {
                        // Prepare failed - stop audio
                        audio_loop_stop();
                        return false;
                    }
                }
                timeout_count = 0;  // Reset counter regardless (we've diagnosed the issue)
            }
        } else {
            // Ready (r > 0) - reset timeout counter
            timeout_count = 0;
        }
        // Retry read
        return true;
    }

    // Read one full period from ALSA, handling errors and partial reads
    // Returns true on success, false if audio should stop
    bool audio_loop_read(uint32_t period_size, void* read_buffer, size_t bytes_per_frame, int& timeout_count)
    {
        snd_pcm_sframes_t frames_read_total = 0;
        uint8_t* const buffer_base = static_cast<uint8_t*>(read_buffer);

        while (frames_read_total < static_cast<snd_pcm_sframes_t>(period_size) && audio_loop_is_running()) {
            // Compute frame offset from base (makes frame vs byte semantics clear)
            snd_pcm_uframes_t const frames_offset = static_cast<snd_pcm_uframes_t>(frames_read_total);
            void* const read_ptr = buffer_base + (frames_offset * bytes_per_frame);
            snd_pcm_uframes_t const remaining = period_size - frames_offset;

            snd_pcm_sframes_t const frames_read = snd_pcm_readi(pcm_handle.handle, read_ptr, remaining);

            if (frames_read < 0) {
                if (!handle_read_error(static_cast<int>(frames_read))) {
                    return false;  // Recovery failed - stop audio
                }
                continue;  // Retry after recovery
            }

            if (frames_read == 0) {
                if (!handle_zero_frames_read(timeout_count)) {
                    return false;  // Should stop
                }
                continue;  // Retry read
            }

            // Successful read - reset timeout counter
            timeout_count = 0;

            // Advance counters for partial reads
            frames_read_total += frames_read;

            // Update frame count as frames are successfully read (mirrors output behavior)
            total_frames.fetch_add(frames_read, std::memory_order_relaxed);
        }

        // If we were interrupted (running became false), signal stop
        return frames_read_total == static_cast<snd_pcm_sframes_t>(period_size);
    }

    // Process captured audio: convert format and deinterleave into channel buffers
    void audio_loop_process(uint32_t period_size, uint32_t channels, size_t total_samples, ConversionBuffers& buffers)
    {
        // Convert from ALSA format to interleaved float (if needed)
        buffers.convert_to_float(alsa_format, total_samples);

        // Deinterleave into channel buffers
        for (uint32_t frame = 0; frame < period_size; ++frame) {
            for (uint32_t ch = 0; ch < channels; ++ch) {
                channel_buffers[ch][frame] = buffers.interleaved_float[(frame * channels) + ch];
            }
        }
    }

    // Invoke user callback with captured audio
    // Returns true to continue, false to stop
    // @param frames_before Frame count at start of this period (for stream_time calculation)
    bool audio_loop_publish(double sample_rate, uint64_t frames_before, uint32_t period_size)
    {
        // Load callback atomically from RT-safe storage
        auto const cb = cb_rt.load();

        if (cb.fn == nullptr) {
            // No callback set - discard captured data and continue (mirrors output silence behavior)
            // This makes input robust: stream keeps running even if callback isn't set yet
            return true;
        }

        // Stream time at START of this buffer (mirrors output behavior)
        double const stream_time_param = static_cast<double>(frames_before) / sample_rate;

        // Call user callback via RT-safe function pointer (noexcept, no std::function on RT thread)
        int const result = cb.fn(cb.ctx, channel_ptrs.data(), nullptr, period_size, stream_time_param);

        // CallbackHolder::thunk catches exceptions and returns -1
        if (result < 0) {
            // Error from callback - stop audio
            audio_loop_stop();
            return false;
        }

        // Update stream time based on frames after this period
        update_stream_time(sample_rate);

        return true;
    }

    // Stream Initialization Helper Functions
    // Note: Helper functions take snd_pcm_t* pcm as parameter rather than using pcm_handle member.
    // This enables clean RAII: start() creates a PcmHandleGuard that owns the handle during
    // initialization; if any step fails, the guard's destructor closes the handle. The member
    // pcm_handle is only assigned on success via guard.release().

    // Configure and apply ALSA hardware parameters
    // Configure and apply ALSA software parameters for capture
    statusbar::Status configure_sw_params_capture(snd_pcm_t* pcm) const
    {
        snd_pcm_sw_params_t* sw_params = nullptr;
        snd_pcm_sw_params_alloca(&sw_params);

        int err = snd_pcm_sw_params_current(pcm, sw_params);
        if (err < 0) {
            return failure(AudioError::InitializationFailed);
        }

        // For capture, set avail_min to wake up when at least one period is available
        snd_pcm_sw_params_set_avail_min(pcm, sw_params, negotiated_period_frames);

        err = snd_pcm_sw_params(pcm, sw_params);
        if (err < 0) {
            return failure(AudioError::InitializationFailed);
        }

        return success();
    }

    // Set up callback and start audio thread
    // Input stream: config.channels input channels, 0 output channels
    void start_audio_thread(AudioCallbackFloat&& callback)
    {
        // Join a thread left over from a prior self-stop (callback failure or
        // denied RT priority): assigning over a joinable std::thread terminates.
        if (audio_thread.joinable()) {
            audio_thread.join();
        }

        // Fresh run: clear observability state carried from a previous run.
        xrun_count.store(0, std::memory_order_relaxed);
        last_error.store(AudioError::None, std::memory_order_release);

        holder = std::make_unique<CallbackHolder>(std::move(callback), cb_rt, config.channels, 0);

        running.store(true, std::memory_order_release);
        audio_thread = std::thread([this]() {
            // Exception barrier (run_guarded): an exception escaping a thread
            // entry calls std::terminate and aborts the process. Always mark the
            // stream stopped on exit so a thread that died on a throw doesn't
            // look alive.
            run_guarded("audio thread", [this]() { audio_loop(); });
            running.store(false, std::memory_order_release);
        });
    }

    void audio_loop()
    {
        // Set thread priority based on configuration
        if (!set_thread_priority(config.thread_priority)) {
            // Failed to set required realtime priority - stop audio
            set_last_error(AudioError::RealtimePriorityDenied);
            audio_loop_stop();
            return;
        }

        // Compute and validate audio loop parameters
        auto const params = make_audio_loop_params(negotiated_period_frames, config.channels, negotiated_sample_rate, alsa_format);
        if (!params) {
            audio_loop_stop();
            return;
        }

        // Preallocate conversion buffers (RT-safety: no malloc in audio loop)
        ConversionBuffers buffers;
        buffers.initialize(alsa_format, params->total_samples);

        // Timeout counter for snd_pcm_wait() - detects stuck devices
        int timeout_count = 0;

        while (audio_loop_is_running()) {
            // Capture frame count BEFORE reading this period (for stream_time calculation)
            uint64_t const frames_before = audio_loop_get_total_frames();

            // Get read buffer pointer matching ALSA format
            void* read_ptr = buffers.read_buffer(alsa_format);
            if (read_ptr == nullptr) {
                // Invalid format - stop audio
                audio_loop_stop();
                break;
            }

            // Read one full period from ALSA (increments total_frames internally)
            if (!audio_loop_read(params->period_frames, read_ptr, params->bytes_per_frame, timeout_count)) {
                break;
            }

            // Process: convert format and deinterleave
            audio_loop_process(params->period_frames, params->channels, params->total_samples, buffers);

            // Invoke user callback with captured audio (pass frames_before for timing)
            if (!audio_loop_publish(params->sample_rate, frames_before, params->period_frames)) {
                break;
            }
        }
    }
};

InputStreamLinux::InputStreamLinux(DeviceInfo device, AudioConfig config)
    : impl_(std::make_unique<Impl>())
{
    impl_->device_info = std::move(device);
    impl_->config = config;
}

InputStreamLinux::~InputStreamLinux() noexcept = default;

auto InputStreamLinux::start(AudioCallbackFloat&& callback) -> statusbar::Status
{
    if (impl_->running.load(std::memory_order_acquire)) {
        return failure(AudioError::AlreadyRunning);
    }

    impl_->total_frames.store(0, std::memory_order_relaxed);
    impl_->stream_time.publish(0.0);

    // Open ALSA PCM device for capture (RAII guard ensures handle is closed on any failure)
    auto guard_result = open_pcm_device(impl_->device_info.uid, SND_PCM_STREAM_CAPTURE);
    if (!guard_result) {
        return failure(guard_result.error());
    }
    auto guard = std::move(*guard_result);
    snd_pcm_t* pcm = guard.handle;

    // Configure and apply hardware parameters
    snd_pcm_hw_params_t* hw_params = nullptr;
    snd_pcm_hw_params_alloca(&hw_params);
    unsigned int periods = 0;

    if (auto status = impl_->configure_hw_params(pcm, hw_params, periods); !status) {
        return status;  // Guard cleans up automatically
    }

    // Query back actual negotiated parameters
    unsigned int actual_periods = 0;
    if (auto status = impl_->query_negotiated_params(pcm, actual_periods); !status) {
        return status;  // Guard cleans up automatically
    }

    // Allocate channel buffers using negotiated period size
    impl_->allocate_channel_buffers();

    // Configure and apply software parameters for capture
    if (auto status = impl_->configure_sw_params_capture(pcm); !status) {
        return status;  // Guard cleans up automatically
    }

    // Prepare PCM
    if (auto status = impl_->prepare_pcm(pcm); !status) {
        return status;  // Guard cleans up automatically
    }

    // Success - transfer ownership from guard to impl
    impl_->pcm_handle = std::move(guard);

    // Set up callback and start audio thread
    impl_->start_audio_thread(std::move(callback));

    return success();
}

void InputStreamLinux::stop()
{
    // Drop pending audio only when stopping a still-running stream (unblocks a
    // thread waiting in snd_pcm_readi). A self-stopped stream has already left
    // the loop, so running is false here.
    bool const was_running = impl_->running.exchange(false, std::memory_order_acq_rel);
    if (was_running) {
        impl_->pcm_handle.drop();
    }

    // Always join a lingering thread, even on a self-stop where running was
    // already false: leaving it joinable would terminate the process when the
    // next start() assigns over impl_->audio_thread. Idempotent across repeat
    // stop() calls (not joinable after the first).
    if (impl_->audio_thread.joinable()) {
        impl_->audio_thread.join();
    }

    // Clear callback after thread has stopped (CallbackHolder destructor clears cb_rt)
    impl_->holder.reset();

    // Close and cleanup PCM handle for clean restart
    impl_->pcm_handle.close();
}

auto InputStreamLinux::is_running() const noexcept -> bool
{
    return impl_->running.load(std::memory_order_acquire);
}

auto InputStreamLinux::config() const noexcept -> AudioConfig const&
{
    return impl_->config;
}

auto InputStreamLinux::stream_time() const noexcept -> double
{
    return impl_->stream_time.load();
}

auto InputStreamLinux::device() const noexcept -> DeviceInfo const&
{
    return impl_->device_info;
}

auto InputStreamLinux::effective_sample_rate() const noexcept -> double
{
    return static_cast<double>(impl_->negotiated_sample_rate);
}

auto InputStreamLinux::effective_period_frames() const noexcept -> uint32_t
{
    // Validate fits in uint32_t (same check as audio_loop thread)
    if (impl_->negotiated_period_frames > std::numeric_limits<uint32_t>::max()) {
        return std::numeric_limits<uint32_t>::max();
    }
    return static_cast<uint32_t>(impl_->negotiated_period_frames);
}

auto InputStreamLinux::effective_buffer_frames() const noexcept -> uint32_t
{
    // Validate fits in uint32_t
    if (impl_->negotiated_buffer_frames > std::numeric_limits<uint32_t>::max()) {
        return std::numeric_limits<uint32_t>::max();
    }
    return static_cast<uint32_t>(impl_->negotiated_buffer_frames);
}

auto InputStreamLinux::xrun_count() const noexcept -> uint64_t
{
    return impl_->xrun_count.load(std::memory_order_relaxed);
}

auto InputStreamLinux::last_error() const noexcept -> AudioError
{
    return impl_->last_error.load(std::memory_order_acquire);
}

// InputStream Factory

auto InputStream::create(std::string_view device_uid, AudioConfig const& config)
    -> statusbar::StatusValue<std::unique_ptr<InputStream>>
{
    auto device_info = DeviceManager::find_device(device_uid, true);
    if (!device_info) {
        return failure(AudioError::DeviceNotFound);
    }

#    if __cpp_exceptions
    try {
#    endif
        return std::make_unique<InputStreamLinux>(*device_info, config);
#    if __cpp_exceptions
    } catch (...) {
        return failure(AudioError::InitializationFailed);
    }
#    endif
}

}  // namespace statusbar::audio

#endif  // defined(__linux__)
