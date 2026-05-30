// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Audio Module - macOS CoreAudio Implementation
// Based on low-latency CoreAudio patterns

#if defined(__APPLE__)

#    include "statusbar/audio/audio.hpp"
#    include "statusbar/itc/itc_published.hpp"
#    include "statusbar/itc/itc_rt_callback_slot.hpp"
#    include "statusbar/status/throw_or_abort.hpp"

#    include <pthread.h>

#    include <algorithm>
#    include <atomic>
#    include <cstring>
#    include <ctime>
#    include <expected>
#    include <memory>
#    include <optional>
#    include <string>
#    include <thread>
#    include <vector>

#    include <AudioUnit/AudioUnit.h>
#    include <CoreAudio/CoreAudio.h>
#    include <CoreFoundation/CoreFoundation.h>

namespace statusbar::audio {

// RT-safe callback ABI: plain function pointer + user context
using AudioCallbackFn =
    int (*)(void* user, float const* const* input, float* const* output, uint32_t frames, double stream_time) noexcept;

using AudioCallbackRT = statusbar::itc::RtCallbackSlot<AudioCallbackFn>;

// Adapter to wrap std::function for RT callback
struct CallbackHolder
{
    AudioCallbackFloat f;  // inplace_function with V2 signature

    // Scratch storage for building AudioCallbackParams (pre-allocated at stream start)
    std::vector<InputAudioBufferFloat> input_buffers;
    std::vector<OutputAudioBufferFloat> output_buffers;
    static auto thunk(void* user, float const* const* input, float* const* output, uint32_t frames, double t) noexcept -> int
    {
        auto* self = static_cast<CallbackHolder*>(user);
#    if __cpp_exceptions
        try {
#    endif
            // Build AudioCallbackParams from raw pointers
            AudioCallbackParams params{};
            params.stream_time = t;

            // Setup input buffers if present
            if (input != nullptr && !self->input_buffers.empty()) {
                for (size_t i = 0; i < self->input_buffers.size(); ++i) {
                    self->input_buffers[i].sample = std::span<float const>(input[i], frames);
                }
                params.input_buffers = self->input_buffers;
            }

            // Setup output buffers if present
            if (output != nullptr && !self->output_buffers.empty()) {
                for (size_t i = 0; i < self->output_buffers.size(); ++i) {
                    self->output_buffers[i].sample = std::span<float>(output[i], frames);
                }
                params.output_buffers = self->output_buffers;
            }

            // Call user callback and convert Status to int
            auto result = self->f(params);
            return result.has_value() ? 0 : -1;
#    if __cpp_exceptions
        } catch (...) {
            return -1;  // Signal error on exception
        }
#    endif
    }
};

// RAII guard to track callback execution (prevents dispose during callback)
struct CallbackGuard
{
    std::atomic<uint32_t>* counter;
    explicit CallbackGuard(std::atomic<uint32_t>* c)
        : counter(c)
    {
        counter->fetch_add(1, std::memory_order_acq_rel);
    }
    ~CallbackGuard() { counter->fetch_sub(1, std::memory_order_acq_rel); }

    // Non-copyable and non-movable (RAII guard)
    CallbackGuard(CallbackGuard const&) = delete;
    auto operator=(CallbackGuard const&) -> CallbackGuard& = delete;
    CallbackGuard(CallbackGuard&&) = delete;
    auto operator=(CallbackGuard&&) -> CallbackGuard& = delete;
};

// Utility Functions

namespace {

/// Zero all audio buffers in the buffer list
auto silence_audio_buffers(AudioBufferList* io_data) noexcept -> void
{
    for (UInt32 i = 0; i < io_data->mNumberBuffers; ++i) {
        if (io_data->mBuffers[i].mData != nullptr) {
            std::memset(io_data->mBuffers[i].mData, 0, io_data->mBuffers[i].mDataByteSize);
        }
    }
}

auto cfstring_to_std(CFStringRef s) -> std::string
{
    if (s == nullptr) {
        return {};
    }
    char buf[512];
    if (CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8) != 0) {
        return buf;
    }
    return {};
}

auto get_device_name(AudioDeviceID dev) -> std::string
{
    CFStringRef cfName = nullptr;
    UInt32 sz = sizeof(CFStringRef);
    AudioObjectPropertyAddress const pa{
        .mSelector = kAudioObjectPropertyName,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain};
    if (AudioObjectGetPropertyData(dev, &pa, 0, nullptr, &sz, static_cast<void*>(&cfName)) != noErr) {
        return {};
    }
    // AudioObjectGetPropertyData returns +1 retained CF object - must CFRelease
    std::string out = cfstring_to_std(cfName);
    if (cfName != nullptr) {
        CFRelease(cfName);
    }
    return out;
}

auto get_device_uid(AudioDeviceID dev) -> std::string
{
    CFStringRef cfUID = nullptr;
    UInt32 sz = sizeof(CFStringRef);
    AudioObjectPropertyAddress const pa{
        .mSelector = kAudioDevicePropertyDeviceUID,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain};
    if (AudioObjectGetPropertyData(dev, &pa, 0, nullptr, &sz, static_cast<void*>(&cfUID)) != noErr) {
        return {};
    }
    // AudioObjectGetPropertyData returns +1 retained CF object - must CFRelease
    std::string out = cfstring_to_std(cfUID);
    if (cfUID != nullptr) {
        CFRelease(cfUID);
    }
    return out;
}

auto get_device_channels(AudioDeviceID dev, bool isInput) -> UInt32
{
    AudioObjectPropertyAddress const pa{
        .mSelector = kAudioDevicePropertyStreamConfiguration,
        .mScope = isInput ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput,
        .mElement = kAudioObjectPropertyElementMain};
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(dev, &pa, 0, nullptr, &sz) != noErr) {
        return 0;
    }
    std::vector<char> buf(sz);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto* abl = reinterpret_cast<AudioBufferList*>(buf.data());
    if (AudioObjectGetPropertyData(dev, &pa, 0, nullptr, &sz, abl) != noErr) {
        return 0;
    }
    UInt32 ch = 0;
    for (UInt32 i = 0; i < abl->mNumberBuffers; ++i) {
        ch += abl->mBuffers[i].mNumberChannels;
    }
    return ch;
}

auto get_device_sample_rates(AudioDeviceID dev) -> std::vector<uint32_t>
{
    // Try to get available nominal sample rates
    AudioObjectPropertyAddress const pa{
        .mSelector = kAudioDevicePropertyAvailableNominalSampleRates,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain};
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(dev, &pa, 0, nullptr, &sz) == noErr && sz > 0) {
        size_t const count = sz / sizeof(AudioValueRange);
        std::vector<AudioValueRange> ranges(count);
        if (AudioObjectGetPropertyData(dev, &pa, 0, nullptr, &sz, ranges.data()) == noErr) {
            std::vector<uint32_t> rates;
            for (size_t i = 0; i < count; ++i) {
                // For discrete rates, mMinimum == mMaximum
                if (ranges[i].mMinimum == ranges[i].mMaximum) {
                    rates.push_back(static_cast<uint32_t>(ranges[i].mMinimum));
                } else {
                    // Range - add common rates within range
                    for (double rate : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0}) {
                        if (rate >= ranges[i].mMinimum && rate <= ranges[i].mMaximum) {
                            rates.push_back(static_cast<uint32_t>(rate));
                        }
                    }
                }
            }
            if (!rates.empty()) {
                // Dedupe and sort
                std::sort(rates.begin(), rates.end());
                rates.erase(std::unique(rates.begin(), rates.end()), rates.end());
                return rates;
            }
        }
    }

    // Fallback: just return current nominal sample rate
    AudioObjectPropertyAddress const sr_pa{
        .mSelector = kAudioDevicePropertyNominalSampleRate,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain};
    Float64 current_rate = 0;
    UInt32 rate_sz = sizeof(current_rate);
    if (AudioObjectGetPropertyData(dev, &sr_pa, 0, nullptr, &rate_sz, &current_rate) == noErr && current_rate > 0) {
        return {static_cast<uint32_t>(current_rate)};
    }

    // Last resort: assume 48kHz
    return {48000};
}

auto find_device_by_uid(std::string_view uid) -> AudioDeviceID
{
    AudioObjectPropertyAddress const pa{
        .mSelector = kAudioHardwarePropertyDevices,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain};
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &pa, 0, nullptr, &sz) != noErr) {
        return kAudioObjectUnknown;
    }
    UInt32 const n = sz / sizeof(AudioDeviceID);
    std::vector<AudioDeviceID> devs(n);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &pa, 0, nullptr, &sz, devs.data()) != noErr) {
        return kAudioObjectUnknown;
    }
    for (auto d : devs) {
        if (get_device_uid(d) == uid) {
            return d;
        }
    }
    return kAudioObjectUnknown;
}

// Shared AudioUnit Helper Functions
// These functions are used by both OutputStreamDarwin and InputStreamDarwin

/// Create a HAL AudioUnit component
/// @return noErr on success, error code on failure
auto create_hal_audio_unit(AudioUnit* audio_unit) -> OSStatus
{
    AudioComponentDescription const desc{
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_HALOutput,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
        .componentFlags = 0,
        .componentFlagsMask = 0};
    AudioComponent const comp = AudioComponentFindNext(nullptr, &desc);
    if (comp == nullptr) {
        return kAudioUnitErr_InvalidElement;
    }
    return AudioComponentInstanceNew(comp, audio_unit);
}

/// Set the current device for an AudioUnit
auto set_audio_unit_device(AudioUnit audio_unit, AudioDeviceID device_id) -> OSStatus
{
    return AudioUnitSetProperty(
        audio_unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &device_id, sizeof(device_id));
}

/// Try to set device buffer size (best-effort, may fail)
auto configure_device_buffer_size(AudioDeviceID device_id, uint32_t requested_frames) -> void
{
    AudioObjectPropertyAddress const pa{
        .mSelector = kAudioDevicePropertyBufferFrameSize,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain};

    Boolean settable = 0;
    if (AudioObjectIsPropertySettable(device_id, &pa, &settable) != noErr || settable == 0) {
        return;
    }

    AudioValueRange range{.mMinimum = 0, .mMaximum = 0};
    AudioObjectPropertyAddress const range_pa{
        .mSelector = kAudioDevicePropertyBufferFrameSizeRange,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain};
    UInt32 range_size = sizeof(range);
    if (AudioObjectGetPropertyData(device_id, &range_pa, 0, nullptr, &range_size, &range) != noErr) {
        return;
    }

    UInt32 buffer_frames = requested_frames;
    if (buffer_frames < range.mMinimum) {
        buffer_frames = static_cast<UInt32>(range.mMinimum);
    }
    if (buffer_frames > range.mMaximum) {
        buffer_frames = static_cast<UInt32>(range.mMaximum);
    }

    UInt32 const sz = sizeof(buffer_frames);
    AudioObjectSetPropertyData(device_id, &pa, 0, nullptr, sz, &buffer_frames);
}

/// Set maximum frames per slice hint (best-effort)
auto set_max_frames_per_slice(AudioUnit audio_unit, UInt32* max_frames_per_slice, uint32_t requested_frames) -> void
{
    *max_frames_per_slice = requested_frames;
    AudioUnitSetProperty(
        audio_unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, max_frames_per_slice, sizeof(UInt32));
}

/// Read back actual buffer parameters after initialization
auto read_actual_buffer_parameters(
    AudioUnit audio_unit,
    AudioDeviceID device_id,
    UInt32* max_frames_per_slice,
    UInt32* device_buffer_frames,
    uint32_t fallback_frames) -> void
{
    // Read actual maximum frames per slice
    UInt32 max_fps_size = sizeof(*max_frames_per_slice);
    OSStatus const err = AudioUnitGetProperty(
        audio_unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, max_frames_per_slice, &max_fps_size);
    if (err != noErr || *max_frames_per_slice == 0) {
        *max_frames_per_slice = fallback_frames * 2;
    }

    // Read actual device buffer size
    UInt32 device_buffer_size = 0;
    UInt32 buffer_size_sz = sizeof(device_buffer_size);
    AudioObjectPropertyAddress const buffer_pa{
        .mSelector = kAudioDevicePropertyBufferFrameSize,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain};
    if (AudioObjectGetPropertyData(device_id, &buffer_pa, 0, nullptr, &buffer_size_sz, &device_buffer_size) == noErr &&
        device_buffer_size > 0) {
        *device_buffer_frames = device_buffer_size;
    } else {
        *device_buffer_frames = *max_frames_per_slice * 3;
    }
}

/// Setup callback holder and publish to RT thread
/// @param num_input_channels Number of input channels (0 for output-only streams)
/// @param num_output_channels Number of output channels (0 for input-only streams)
auto setup_audio_callback(
    std::unique_ptr<CallbackHolder>* holder,
    AudioCallbackRT* cb_rt,
    AudioCallbackFloat&& callback,
    uint32_t num_input_channels,
    uint32_t num_output_channels) -> void
{
    *holder = std::make_unique<CallbackHolder>();
    (*holder)->f = std::move(callback);

    // Pre-allocate buffer vectors to avoid allocation in RT callback
    if (num_input_channels > 0) {
        (*holder)->input_buffers.resize(num_input_channels);
    }
    if (num_output_channels > 0) {
        (*holder)->output_buffers.resize(num_output_channels);
    }

    cb_rt->publish(&CallbackHolder::thunk, holder->get());
}

/// Start the AudioUnit and update running state
/// @return noErr on success, error code on failure
auto start_audio_unit(AudioUnit audio_unit, std::atomic<bool>* running, bool* started) -> OSStatus
{
    running->store(true, std::memory_order_release);
    OSStatus const err = AudioOutputUnitStart(audio_unit);
    if (err != noErr) {
        running->store(false, std::memory_order_release);
        return err;
    }
    *started = true;
    return noErr;
}

/// Wait for in-flight callbacks to complete
/// Uses two-phase wait: quick yields first, then sleep with backoff
auto wait_for_callbacks_to_complete(std::atomic<uint32_t>* in_callback) -> void
{
    // Phase 1: Try a handful of yields (callback should exit immediately)
    for (int i = 0; i < 20 && in_callback->load(std::memory_order_acquire) != 0; ++i) {
        pthread_yield_np();
    }

    // Phase 2: wait-until-zero with backoff (correctness-first)
    timespec ts{.tv_sec = 0, .tv_nsec = 100000};
    int i = 0;
    while (in_callback->load(std::memory_order_acquire) != 0) {
        nanosleep(&ts, nullptr);
        ++i;
        if (i == 50) {
            ts.tv_nsec = 500000;
        }
        if (i == 200) {
            ts.tv_nsec = 1000000;
        }
    }
}

/// Centralized AudioUnit cleanup helper for consistent teardown
/// @param audio_unit Pointer to AudioUnit (will be set to nullptr)
/// @param running Atomic running flag
/// @param in_callback Atomic in-callback counter
/// @param destroying Atomic destroying flag (for idempotency)
/// @param initialized Pointer to initialized flag
/// @param started Pointer to started flag
/// @param holder Pointer to callback holder
/// @param cb_rt Pointer to RT callback struct
auto destroy_audio_unit(
    AudioUnit* audio_unit,
    std::atomic<bool>* running,
    std::atomic<uint32_t>* in_callback,
    std::atomic<bool>* destroying,
    bool* initialized,
    bool* started,
    std::unique_ptr<CallbackHolder>* holder,
    AudioCallbackRT* cb_rt) noexcept -> void
{
    if (*audio_unit == nullptr) {
        return;
    }

    // Prevent concurrent destroy_unit calls (make idempotent)
    bool expected = false;
    if (!destroying->compare_exchange_strong(expected, true, std::memory_order_acquire)) {
        return;  // Already being destroyed by another thread
    }

    // Signal callbacks to exit before stopping (reduces work during teardown)
    running->store(false, std::memory_order_release);

    // Only stop if actually started (avoids rare HAL device oddities)
    if (*started) {
        AudioOutputUnitStop(*audio_unit);
        AudioUnitReset(*audio_unit, kAudioUnitScope_Global, 0);  // Flush zombie callbacks
        *started = false;
    }

    // Wait for any in-flight callbacks to complete before disposing
    wait_for_callbacks_to_complete(in_callback);

    // Now safe to clear callback - no RT thread is accessing it
    cb_rt->clear();
    holder->reset();

    // Only uninitialize if actually initialized
    if (*initialized) {
        AudioUnitUninitialize(*audio_unit);
        *initialized = false;
    }

    AudioComponentInstanceDispose(*audio_unit);
    *audio_unit = nullptr;

    destroying->store(false, std::memory_order_release);
}

}  // namespace

// DeviceManagerDarwin Implementation

auto DeviceManagerDarwin::enumerate_devices_impl() -> std::vector<DeviceInfo>
{
    std::vector<DeviceInfo> result;

    AudioObjectPropertyAddress const pa{
        .mSelector = kAudioHardwarePropertyDevices,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain};
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &pa, 0, nullptr, &sz) != noErr) {
        return result;
    }

    UInt32 const n = sz / sizeof(AudioDeviceID);
    std::vector<AudioDeviceID> devs(n);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &pa, 0, nullptr, &sz, devs.data()) != noErr) {
        return result;
    }

    // Get default devices
    AudioObjectPropertyAddress const pa_def_out{
        .mSelector = kAudioHardwarePropertyDefaultOutputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain};
    AudioObjectPropertyAddress const pa_def_in{
        .mSelector = kAudioHardwarePropertyDefaultInputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain};
    AudioDeviceID def_out = kAudioObjectUnknown, def_in = kAudioObjectUnknown;
    UInt32 def_sz = sizeof(AudioDeviceID);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &pa_def_out, 0, nullptr, &def_sz, &def_out);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &pa_def_in, 0, nullptr, &def_sz, &def_in);

    for (auto d : devs) {
        DeviceInfo info;
        info.name = get_device_name(d);
        info.uid = get_device_uid(d);
        info.max_input_channels = get_device_channels(d, true);
        info.max_output_channels = get_device_channels(d, false);
        info.is_default_input = (d == def_in);
        info.is_default_output = (d == def_out);

        // Query actual supported sample rates from device
        info.supported_sample_rates = get_device_sample_rates(d);

        result.push_back(std::move(info));
    }

    return result;
}

auto DeviceManagerDarwin::default_output_device_impl() -> std::optional<DeviceInfo>
{
    AudioObjectPropertyAddress const pa{
        .mSelector = kAudioHardwarePropertyDefaultOutputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain};
    AudioDeviceID dev = kAudioObjectUnknown;
    UInt32 sz = sizeof(dev);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &pa, 0, nullptr, &sz, &dev) != noErr) {
        return std::nullopt;
    }
    if (dev == kAudioObjectUnknown) {
        return std::nullopt;
    }

    DeviceInfo info;
    info.name = get_device_name(dev);
    info.uid = get_device_uid(dev);
    info.max_input_channels = get_device_channels(dev, true);
    info.max_output_channels = get_device_channels(dev, false);
    info.is_default_output = true;
    // Query actual supported sample rates from device
    info.supported_sample_rates = get_device_sample_rates(dev);

    return info;
}

auto DeviceManagerDarwin::default_input_device_impl() -> std::optional<DeviceInfo>
{
    AudioObjectPropertyAddress const pa{
        .mSelector = kAudioHardwarePropertyDefaultInputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain};
    AudioDeviceID dev = kAudioObjectUnknown;
    UInt32 sz = sizeof(dev);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &pa, 0, nullptr, &sz, &dev) != noErr) {
        return std::nullopt;
    }
    if (dev == kAudioObjectUnknown) {
        return std::nullopt;
    }

    DeviceInfo info;
    info.name = get_device_name(dev);
    info.uid = get_device_uid(dev);
    info.max_input_channels = get_device_channels(dev, true);
    info.max_output_channels = get_device_channels(dev, false);
    info.is_default_input = true;
    // Query actual supported sample rates from device
    info.supported_sample_rates = get_device_sample_rates(dev);

    return info;
}

auto DeviceManagerDarwin::find_device_impl(std::string_view identifier, bool is_input) -> std::optional<DeviceInfo>
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

    return std::nullopt;
}

// DeviceManager Forwarding Implementation

auto DeviceManager::enumerate_devices() -> std::vector<DeviceInfo>
{
    return DeviceManagerDarwin::enumerate_devices_impl();
}

auto DeviceManager::default_input_device() -> std::optional<DeviceInfo>
{
    return DeviceManagerDarwin::default_input_device_impl();
}

auto DeviceManager::default_output_device() -> std::optional<DeviceInfo>
{
    return DeviceManagerDarwin::default_output_device_impl();
}

auto DeviceManager::find_device(std::string_view identifier, bool is_input) -> std::optional<DeviceInfo>
{
    return DeviceManagerDarwin::find_device_impl(identifier, is_input);
}

// OutputStreamDarwin Implementation

struct OutputStreamDarwin::Impl
{
    AudioUnit audio_unit{nullptr};
    AudioDeviceID device_id{kAudioObjectUnknown};
    AudioCallbackRT cb_rt;                   // RT-safe callback slot (coherent fn/ctx pair via triple buffer)
    std::unique_ptr<CallbackHolder> holder;  // Manages std::function lifetime
    AudioConfig config;
    DeviceInfo device_info;
    std::atomic<bool> running{false};
    std::atomic<uint32_t> in_callback{0};  // Guard: callback is executing
    std::atomic<bool> destroying{false};   // Guard: prevent concurrent destroy_unit calls
    bool initialized{false};               // AudioUnit has been initialized
    bool started{false};                   // AudioOutputUnitStart succeeded
    statusbar::itc::Published<double> stream_time{0.0};
    std::atomic<uint64_t> total_frames{0};
    Float64 effective_sample_rate{48000.0};  // Initialize to safe default, overwrite with actual
    UInt32 max_frames_per_slice{0};          // Maximum frames CoreAudio may request (period size)
    UInt32 device_buffer_frames{0};          // Actual device I/O buffer size (total buffer)

    // Channel pointer array for non-interleaved format (buffers provided by CoreAudio)
    std::vector<float*> channel_ptrs;

    // Non-copyable and non-movable (owns AudioUnit)
    Impl() = default;
    Impl(Impl const&) = delete;
    auto operator=(Impl const&) -> Impl& = delete;
    Impl(Impl&&) = delete;
    auto operator=(Impl&&) -> Impl& = delete;

    // Helper Functions forstart() =====

    /// Configure IO: enable output, disable input
    auto configure_output_io() const -> void
    {
        UInt32 on = 1, off = 0;
        AudioUnitSetProperty(audio_unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &on, sizeof(on));
        AudioUnitSetProperty(audio_unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &off, sizeof(off));
    }

    /// Configure stream format for output (non-interleaved float32)
    /// Returns noErr on success and updates effective_sample_rate
    auto configure_output_format() -> OSStatus
    {
        AudioStreamBasicDescription fmt{};
        fmt.mSampleRate = static_cast<Float64>(config.sample_rate);
        fmt.mFormatID = kAudioFormatLinearPCM;
        fmt.mFormatFlags = static_cast<AudioFormatFlags>(
            static_cast<unsigned>(kAudioFormatFlagsNativeFloatPacked) | static_cast<unsigned>(kAudioFormatFlagIsNonInterleaved));
        fmt.mBitsPerChannel = 32;
        fmt.mChannelsPerFrame = config.channels;
        fmt.mBytesPerFrame = 4;
        fmt.mFramesPerPacket = 1;
        fmt.mBytesPerPacket = 4;
        fmt.mReserved = 0;

        // For AUHAL output: set format on Input scope element 0
        OSStatus err =
            AudioUnitSetProperty(audio_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof(fmt));
        if (err != noErr) {
            return err;
        }

        // Query actual negotiated sample rate
        AudioStreamBasicDescription actual_fmt{};
        UInt32 fmt_size = sizeof(actual_fmt);
        err = AudioUnitGetProperty(audio_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &actual_fmt, &fmt_size);
        effective_sample_rate = (err == noErr) ? actual_fmt.mSampleRate : static_cast<Float64>(config.sample_rate);

        return noErr;
    }

    /// Set the render callback for output
    auto set_output_callback() -> OSStatus
    {
        AURenderCallbackStruct cb{.inputProc = Impl::render_callback, .inputProcRefCon = this};
        return AudioUnitSetProperty(audio_unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb));
    }

    /// Initialize AudioUnit and verify format
    /// Returns noErr on success
    auto initialize_and_verify_format() -> OSStatus
    {
        OSStatus err = AudioUnitInitialize(audio_unit);
        if (err != noErr) {
            return err;
        }
        initialized = true;

        // Re-verify format after initialize
        AudioStreamBasicDescription final_fmt{};
        UInt32 final_fmt_size = sizeof(final_fmt);
        err = AudioUnitGetProperty(
            audio_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &final_fmt, &final_fmt_size);
        if (err == noErr) {
            effective_sample_rate = final_fmt.mSampleRate;
            if ((final_fmt.mFormatFlags & kAudioFormatFlagIsNonInterleaved) == 0 ||
                final_fmt.mChannelsPerFrame != config.channels) {
                return kAudioUnitErr_FormatNotSupported;
            }
        }

        return noErr;
    }

    // Cleanup

    /// Centralized cleanup helper for consistent teardown
    auto destroy_unit() noexcept -> void
    {
        destroy_audio_unit(&audio_unit, &running, &in_callback, &destroying, &initialized, &started, &holder, &cb_rt);
    }

    ~Impl()
    {
        // Ensure audio is stopped and callback has finished before cleanup
        running.store(false, std::memory_order_release);
        destroy_unit();
    }

    static auto render_callback(
        void* refcon, AudioUnitRenderActionFlags*, AudioTimeStamp const* timestamp, UInt32, UInt32 frames, AudioBufferList* io_data)
        -> OSStatus
    {
        auto* self = static_cast<Impl*>(refcon);

        // Mark that we're in callback (prevent dispose while executing)
        CallbackGuard const guard{&self->in_callback};

        // Defensive null check
        if (io_data == nullptr || io_data->mNumberBuffers == 0) {
            return noErr;
        }

        // Load callback atomically from RT-safe storage
        auto const cb = self->cb_rt.load();

        if (cb.fn == nullptr || !self->running.load(std::memory_order_acquire)) {
            // Silence output
            silence_audio_buffers(io_data);
            return noErr;
        }

        // Validate buffer layout matches our configuration (non-interleaved)
        if (io_data->mNumberBuffers != self->config.channels) {
            // Unexpected buffer configuration - silence output and stop stream
            silence_audio_buffers(io_data);
            self->running.store(false, std::memory_order_release);
            return noErr;
        }

        // Validate each buffer: 1 channel per buffer, valid data, sufficient size
        auto const is_invalid_audio_buffer = [frames](AudioBuffer const& buf) {
            return buf.mNumberChannels != 1 || buf.mData == nullptr || buf.mDataByteSize < frames * sizeof(float);
        };
        for (UInt32 i = 0; i < io_data->mNumberBuffers; ++i) {
            if (is_invalid_audio_buffer(io_data->mBuffers[i])) {
                // Invalid buffer layout - silence all outputs and stop stream
                silence_audio_buffers(io_data);
                self->running.store(false, std::memory_order_release);
                return noErr;
            }
        }

        // Note: Don't check frames > max_frames_per_slice for output - that property is best-effort
        // and devices can legitimately request larger slices. CoreAudio provides the buffers,
        // so we trust mDataByteSize (already validated above) as the real capacity.

        // Validate channel_ptrs array size matches configuration
        if (self->channel_ptrs.size() != self->config.channels) {
            // Unexpected channel_ptrs size - silence output and stop stream
            silence_audio_buffers(io_data);
            self->running.store(false, std::memory_order_release);
            return noErr;
        }

        // Prepare channel pointers using buffers from CoreAudio (no allocation in callback)
        for (UInt32 i = 0; i < io_data->mNumberBuffers; ++i) {
            self->channel_ptrs[i] = static_cast<float*>(io_data->mBuffers[i].mData);
        }

        // Compute stream time - prefer AudioTimeStamp if valid
        double stream_time = 0.0;
        if (timestamp != nullptr && (timestamp->mFlags & kAudioTimeStampSampleTimeValid) != 0) {
            stream_time = timestamp->mSampleTime / self->effective_sample_rate;
        } else {
            stream_time = static_cast<double>(self->total_frames.load(std::memory_order_relaxed)) / self->effective_sample_rate;
        }

        // Call user callback via RT-safe function pointer (noexcept, no std::function on RT thread)
        int const result = cb.fn(cb.ctx, nullptr, self->channel_ptrs.data(), frames, stream_time);

        // CallbackHolder::thunk catches exceptions and returns -1
        if (result < 0) {
            // Error from callback - silence output and stop stream
            silence_audio_buffers(io_data);
            self->running.store(false, std::memory_order_release);
            return noErr;
        }

        // Update stream time to end-of-block time (current time after processing this block)
        auto total_frames_after = self->total_frames.fetch_add(frames, std::memory_order_relaxed) + frames;
        self->stream_time.publish(static_cast<double>(total_frames_after) / self->effective_sample_rate);

        return noErr;
    }
};

OutputStreamDarwin::OutputStreamDarwin(DeviceInfo device, AudioConfig config)
    : impl_(std::make_unique<Impl>())
{
    impl_->device_info = std::move(device);
    impl_->config = config;

    // Find device ID
    impl_->device_id = find_device_by_uid(impl_->device_info.uid);
    if (impl_->device_id == kAudioObjectUnknown) {
        statusbar::throw_or_abort(std::errc::no_such_device, "Device not found: " + impl_->device_info.uid);
    }
}

OutputStreamDarwin::~OutputStreamDarwin() noexcept = default;

auto OutputStreamDarwin::start(AudioCallbackFloat&& callback) -> statusbar::Status
{
    if (impl_->running.load(std::memory_order_acquire)) {
        return failure(AudioError::AlreadyRunning);
    }

    // Clean up any existing AudioUnit from previous start/stop cycle
    impl_->destroy_unit();

    // Reset stream state
    impl_->total_frames.store(0, std::memory_order_relaxed);
    impl_->stream_time.publish(0.0);
    impl_->channel_ptrs.resize(impl_->config.channels);

    // Stage 1: Create AudioUnit
    if (create_hal_audio_unit(&impl_->audio_unit) != noErr) {
        return failure(AudioError::InitializationFailed);
    }

    // Stage 2: Configure IO (enable output, disable input)
    impl_->configure_output_io();

    // Stage 3: Set device
    if (set_audio_unit_device(impl_->audio_unit, impl_->device_id) != noErr) {
        impl_->destroy_unit();
        return failure(AudioError::DeviceNotFound);
    }

    // Stage 4: Configure stream format
    if (impl_->configure_output_format() != noErr) {
        impl_->destroy_unit();
        return failure(AudioError::UnsupportedFormat);
    }

    // Stage 5: Set render callback
    if (impl_->set_output_callback() != noErr) {
        impl_->destroy_unit();
        return failure(AudioError::InitializationFailed);
    }

    // Stage 6: Configure buffer sizes (best-effort)
    configure_device_buffer_size(impl_->device_id, impl_->config.buffer_frames);
    set_max_frames_per_slice(impl_->audio_unit, &impl_->max_frames_per_slice, impl_->config.buffer_frames);

    // Stage 7: Initialize and verify format
    if (impl_->initialize_and_verify_format() != noErr) {
        impl_->destroy_unit();
        return failure(AudioError::UnsupportedFormat);
    }

    // Stage 8: Read actual buffer parameters
    read_actual_buffer_parameters(
        impl_->audio_unit,
        impl_->device_id,
        &impl_->max_frames_per_slice,
        &impl_->device_buffer_frames,
        impl_->config.buffer_frames);

    // Stage 9: Setup callback and start
    // Output stream: 0 input channels, config.channels output channels
    setup_audio_callback(&impl_->holder, &impl_->cb_rt, std::move(callback), 0, impl_->config.channels);

    if (start_audio_unit(impl_->audio_unit, &impl_->running, &impl_->started) != noErr) {
        impl_->destroy_unit();
        return failure(AudioError::HardwareError);
    }

    return success();
}

void OutputStreamDarwin::stop()
{
    if (!impl_->running.exchange(false, std::memory_order_acq_rel)) {
        return;  // Already stopped
    }

    // If called from within callback thread, defer destroy to avoid deadlock
    // (AudioOutputUnitStop deadlocks when called from render thread)
    if (impl_->in_callback.load(std::memory_order_acquire) > 0) {
        // Just set running=false (already done above), callback will exit naturally
        // Actual cleanup happens in destructor
        return;
    }

    // Destroy AudioUnit immediately to release device resources
    // (will be recreated on next start() call)
    impl_->destroy_unit();
}

auto OutputStreamDarwin::is_running() const noexcept -> bool
{
    return impl_->running.load(std::memory_order_acquire);
}

auto OutputStreamDarwin::config() const noexcept -> AudioConfig const&
{
    return impl_->config;
}

auto OutputStreamDarwin::stream_time() const noexcept -> double
{
    return impl_->stream_time.load();
}

auto OutputStreamDarwin::effective_sample_rate() const noexcept -> double
{
    return impl_->effective_sample_rate;
}

auto OutputStreamDarwin::effective_period_frames() const noexcept -> uint32_t
{
    return impl_->max_frames_per_slice;
}

auto OutputStreamDarwin::effective_buffer_frames() const noexcept -> uint32_t
{
    return impl_->device_buffer_frames;
}

auto OutputStreamDarwin::device() const noexcept -> DeviceInfo const&
{
    return impl_->device_info;
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
        return std::make_unique<OutputStreamDarwin>(*device_info, config);
#    if __cpp_exceptions
    } catch (...) {
        return failure(AudioError::InitializationFailed);
    }
#    endif
}

// InputStreamDarwin Implementation

struct InputStreamDarwin::Impl
{
    AudioUnit audio_unit{nullptr};
    AudioDeviceID device_id{kAudioObjectUnknown};
    AudioCallbackRT cb_rt;                   // RT-safe callback slot (coherent fn/ctx pair via triple buffer)
    std::unique_ptr<CallbackHolder> holder;  // Manages std::function lifetime
    AudioConfig config;
    DeviceInfo device_info;
    std::atomic<bool> running{false};
    std::atomic<uint32_t> in_callback{0};  // Guard: callback is executing
    std::atomic<bool> destroying{false};   // Guard: prevent concurrent destroy_unit calls
    bool initialized{false};               // AudioUnit has been initialized
    bool started{false};                   // AudioOutputUnitStart succeeded
    statusbar::itc::Published<double> stream_time{0.0};
    std::atomic<uint64_t> total_frames{0};
    Float64 effective_sample_rate{0};                // Actual hardware sample rate
    UInt32 max_frames_per_slice{0};                  // Maximum frames CoreAudio may request (period size)
    UInt32 device_buffer_frames{0};                  // Actual device I/O buffer size (total buffer)
    std::atomic<uint32_t> oversized_slice_count{0};  // Count slices larger than max (device quirk detection)

    // Channel buffers for non-interleaved format - use FLAT buffer
    std::vector<float> input_scratch;  // Flat buffer: channels * maxFrames
    std::vector<float*> channel_ptrs;

    // Pre-allocated AudioBufferList storage to avoid allocation in callback
    std::vector<uint8_t> buffer_list_storage;

    // Non-copyable and non-movable (owns AudioUnit)
    Impl() = default;
    Impl(Impl const&) = delete;
    auto operator=(Impl const&) -> Impl& = delete;
    Impl(Impl&&) = delete;
    auto operator=(Impl&&) -> Impl& = delete;

    // Helper Functions forstart() =====

    /// Configure IO: enable input, disable output
    auto configure_input_io() const -> OSStatus
    {
        UInt32 on = 1, off = 0;
        OSStatus const err =
            AudioUnitSetProperty(audio_unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &on, sizeof(on));
        if (err != noErr) {
            return err;
        }
        return AudioUnitSetProperty(audio_unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &off, sizeof(off));
    }

    /// Query hardware sample rate
    auto query_hardware_sample_rate() -> OSStatus
    {
        Float64 hardware_sr = 0;
        UInt32 sr_size = sizeof(hardware_sr);
        AudioObjectPropertyAddress const sr_addr{
            .mSelector = kAudioDevicePropertyNominalSampleRate,
            .mScope = kAudioObjectPropertyScopeGlobal,
            .mElement = kAudioObjectPropertyElementMain};
        OSStatus const err = AudioObjectGetPropertyData(device_id, &sr_addr, 0, nullptr, &sr_size, &hardware_sr);
        if (err != noErr) {
            return err;
        }
        effective_sample_rate = hardware_sr;
        return noErr;
    }

    /// Configure stream format for input (non-interleaved float32)
    auto configure_input_format() const -> OSStatus
    {
        AudioStreamBasicDescription fmt{};
        fmt.mSampleRate = effective_sample_rate;  // Must match hardware
        fmt.mFormatID = kAudioFormatLinearPCM;
        fmt.mFormatFlags = static_cast<AudioFormatFlags>(
            static_cast<unsigned>(kAudioFormatFlagsNativeFloatPacked) | static_cast<unsigned>(kAudioFormatFlagIsNonInterleaved));
        fmt.mBitsPerChannel = 32;
        fmt.mChannelsPerFrame = config.channels;
        fmt.mBytesPerFrame = 4;
        fmt.mFramesPerPacket = 1;
        fmt.mBytesPerPacket = 4;
        fmt.mReserved = 0;

        // For HAL input: Set format on OUTPUT scope of element 1
        return AudioUnitSetProperty(audio_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &fmt, sizeof(fmt));
    }

    /// Set the input callback
    auto set_input_callback() -> OSStatus
    {
        AURenderCallbackStruct cb{.inputProc = Impl::input_callback, .inputProcRefCon = this};
        return AudioUnitSetProperty(
            audio_unit, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 1, &cb, sizeof(cb));
    }

    /// Initialize AudioUnit and verify format
    auto initialize_and_verify_format() -> OSStatus
    {
        OSStatus err = AudioUnitInitialize(audio_unit);
        if (err != noErr) {
            return err;
        }
        initialized = true;

        // Re-verify format after initialize
        AudioStreamBasicDescription final_fmt{};
        UInt32 final_fmt_size = sizeof(final_fmt);
        err = AudioUnitGetProperty(
            audio_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &final_fmt, &final_fmt_size);
        if (err == noErr) {
            if ((final_fmt.mFormatFlags & kAudioFormatFlagIsNonInterleaved) == 0 ||
                final_fmt.mChannelsPerFrame != config.channels) {
                return kAudioUnitErr_FormatNotSupported;
            }
            effective_sample_rate = final_fmt.mSampleRate;
        }

        return noErr;
    }

    /// Allocate input buffers for the callback
    auto allocate_input_buffers() -> void
    {
        // Allocate flat scratch buffer
        input_scratch.resize(static_cast<size_t>(config.channels) * max_frames_per_slice);
        channel_ptrs.resize(config.channels);
        for (uint32_t i = 0; i < config.channels; ++i) {
            channel_ptrs[i] = &input_scratch[static_cast<size_t>(i) * max_frames_per_slice];
        }

        // Setup AudioBufferList storage
        size_t const buffer_list_size = sizeof(AudioBufferList) + ((config.channels - 1) * sizeof(AudioBuffer));
        buffer_list_storage.resize(buffer_list_size);

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        AudioBufferList* buffer_list = reinterpret_cast<AudioBufferList*>(buffer_list_storage.data());
        buffer_list->mNumberBuffers = config.channels;
        for (UInt32 i = 0; i < config.channels; ++i) {
            buffer_list->mBuffers[i].mNumberChannels = 1;
            buffer_list->mBuffers[i].mDataByteSize = max_frames_per_slice * sizeof(float);
            buffer_list->mBuffers[i].mData = &input_scratch[static_cast<size_t>(i) * max_frames_per_slice];
        }
    }

    // Cleanup

    /// Centralized cleanup helper for consistent teardown
    auto destroy_unit() noexcept -> void
    {
        destroy_audio_unit(&audio_unit, &running, &in_callback, &destroying, &initialized, &started, &holder, &cb_rt);
    }

    ~Impl()
    {
        // Ensure audio is stopped and callback has finished before cleanup
        running.store(false, std::memory_order_release);
        destroy_unit();
    }

    static auto input_callback(
        void* refcon, AudioUnitRenderActionFlags* flags, AudioTimeStamp const* timestamp, UInt32, UInt32 frames, AudioBufferList*)
        -> OSStatus
    {
        auto* self = static_cast<Impl*>(refcon);
        // Always use bus 1 for AUHAL input (matches element 1 configuration)

        // Mark that we're in callback (prevent dispose while executing)
        CallbackGuard const guard{&self->in_callback};

        // Load callback atomically from RT-safe storage
        auto const cb = self->cb_rt.load();

        if (cb.fn == nullptr || !self->running.load(std::memory_order_acquire)) {
            return noErr;
        }

        // Defensive check: ensure effective_sample_rate is valid
        if (self->effective_sample_rate <= 0.0) {
            self->running.store(false, std::memory_order_release);
            return noErr;
        }

        // Bounds check: kAudioUnitProperty_MaximumFramesPerSlice is not a hard guarantee
        // (devices can send larger slices during transitions or with certain configurations).
        // Drop oversized slices and keep running - count them for diagnostics.
        if (self->max_frames_per_slice > 0 && frames > self->max_frames_per_slice) {
            self->oversized_slice_count.fetch_add(1, std::memory_order_relaxed);
            return noErr;  // Drop this buffer, keep stream running
        }

        // Validate input_scratch buffer is properly sized
        if (self->max_frames_per_slice == 0 ||
            self->input_scratch.size() < static_cast<size_t>(self->config.channels) * self->max_frames_per_slice) {
            self->running.store(false, std::memory_order_release);
            return noErr;
        }

        // Validate buffer_list_storage size before dereferencing
        size_t const needed = sizeof(AudioBufferList) + ((self->config.channels - 1) * sizeof(AudioBuffer));
        if (self->buffer_list_storage.size() < needed) {
            self->running.store(false, std::memory_order_release);
            return noErr;
        }

        // Use pre-allocated buffer list and initialize it fully each callback
        // (Don't trust mNumberBuffers - just set it every time to avoid initialization race)
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        AudioBufferList* buffer_list = reinterpret_cast<AudioBufferList*>(self->buffer_list_storage.data());
        buffer_list->mNumberBuffers = self->config.channels;

        // Initialize all buffer fields for this callback
        for (UInt32 i = 0; i < buffer_list->mNumberBuffers; ++i) {
            buffer_list->mBuffers[i].mNumberChannels = 1;
            buffer_list->mBuffers[i].mDataByteSize = frames * sizeof(float);
            buffer_list->mBuffers[i].mData = &self->input_scratch[static_cast<size_t>(i) * self->max_frames_per_slice];
        }

        // Pull audio data from input device (bus 1 matches our element 1 configuration)
        OSStatus const err = AudioUnitRender(self->audio_unit, flags, timestamp, 1, frames, buffer_list);
        if (err != noErr) {
            self->running.store(false, std::memory_order_release);
            return err;
        }

        // Validate channel_ptrs array size matches configuration
        if (self->channel_ptrs.size() != self->config.channels) {
            // Unexpected channel_ptrs size - stop stream
            self->running.store(false, std::memory_order_release);
            return noErr;
        }

        // Update channel_ptrs from buffer_list after AudioUnitRender
        // (AudioUnitRender may have modified mData pointers, so copy them to channel_ptrs)
        for (UInt32 i = 0; i < buffer_list->mNumberBuffers; ++i) {
            self->channel_ptrs[i] = static_cast<float*>(buffer_list->mBuffers[i].mData);
        }

        // Compute stream time - prefer AudioTimeStamp if valid
        double stream_time = 0.0;
        if (timestamp != nullptr && (timestamp->mFlags & kAudioTimeStampSampleTimeValid) != 0) {
            stream_time = timestamp->mSampleTime / self->effective_sample_rate;
        } else {
            stream_time = static_cast<double>(self->total_frames.load(std::memory_order_relaxed)) / self->effective_sample_rate;
        }

        // Call user callback via RT-safe function pointer (noexcept, no std::function on RT thread)
        int const result = cb.fn(cb.ctx, self->channel_ptrs.data(), nullptr, frames, stream_time);

        // CallbackHolder::thunk catches exceptions and returns -1
        if (result < 0) {
            // Error from callback - stop stream
            self->running.store(false, std::memory_order_release);
            return noErr;
        }

        // Update stream time to end-of-block time (current time after processing this block)
        auto total_frames_after = self->total_frames.fetch_add(frames, std::memory_order_relaxed) + frames;
        self->stream_time.publish(static_cast<double>(total_frames_after) / self->effective_sample_rate);

        return noErr;
    }
};

InputStreamDarwin::InputStreamDarwin(DeviceInfo device, AudioConfig config)
    : impl_(std::make_unique<Impl>())
{
    impl_->device_info = std::move(device);
    impl_->config = config;

    // Find device ID
    impl_->device_id = find_device_by_uid(impl_->device_info.uid);
    if (impl_->device_id == kAudioObjectUnknown) {
        statusbar::throw_or_abort(std::errc::no_such_device, "Device not found: " + impl_->device_info.uid);
    }
}

InputStreamDarwin::~InputStreamDarwin() noexcept = default;

auto InputStreamDarwin::start(AudioCallbackFloat&& callback) -> statusbar::Status
{
    if (impl_->running.load(std::memory_order_acquire)) {
        return failure(AudioError::AlreadyRunning);
    }

    // Clean up any existing AudioUnit from previous start/stop cycle
    impl_->destroy_unit();

    // Reset stream state
    impl_->total_frames.store(0, std::memory_order_relaxed);
    impl_->stream_time.publish(0.0);

    // Stage 1: Create AudioUnit
    if (create_hal_audio_unit(&impl_->audio_unit) != noErr) {
        return failure(AudioError::InitializationFailed);
    }

    // Stage 2: Configure IO (enable input, disable output)
    if (impl_->configure_input_io() != noErr) {
        impl_->destroy_unit();
        return failure(AudioError::InitializationFailed);
    }

    // Stage 3: Set device
    if (set_audio_unit_device(impl_->audio_unit, impl_->device_id) != noErr) {
        impl_->destroy_unit();
        return failure(AudioError::DeviceNotFound);
    }

    // Stage 4: Query hardware sample rate
    if (impl_->query_hardware_sample_rate() != noErr) {
        impl_->destroy_unit();
        return failure(AudioError::HardwareError);
    }

    // Stage 5: Configure buffer sizes (best-effort)
    configure_device_buffer_size(impl_->device_id, impl_->config.buffer_frames);

    // Stage 6: Configure stream format
    if (impl_->configure_input_format() != noErr) {
        impl_->destroy_unit();
        return failure(AudioError::UnsupportedFormat);
    }

    // Stage 7: Set input callback
    if (impl_->set_input_callback() != noErr) {
        impl_->destroy_unit();
        return failure(AudioError::InitializationFailed);
    }

    // Stage 8: Set max frames per slice hint
    set_max_frames_per_slice(impl_->audio_unit, &impl_->max_frames_per_slice, impl_->config.buffer_frames);

    // Stage 9: Initialize and verify format
    if (impl_->initialize_and_verify_format() != noErr) {
        impl_->destroy_unit();
        return failure(AudioError::UnsupportedFormat);
    }

    // Stage 10: Read actual buffer parameters
    read_actual_buffer_parameters(
        impl_->audio_unit,
        impl_->device_id,
        &impl_->max_frames_per_slice,
        &impl_->device_buffer_frames,
        impl_->config.buffer_frames);

    // Stage 11: Allocate input buffers
    impl_->allocate_input_buffers();

    // Stage 12: Setup callback and start
    // Input stream: config.channels input channels, 0 output channels
    setup_audio_callback(&impl_->holder, &impl_->cb_rt, std::move(callback), impl_->config.channels, 0);

    if (start_audio_unit(impl_->audio_unit, &impl_->running, &impl_->started) != noErr) {
        impl_->destroy_unit();
        return failure(AudioError::HardwareError);
    }

    return success();
}

void InputStreamDarwin::stop()
{
    if (!impl_->running.exchange(false, std::memory_order_acq_rel)) {
        return;  // Already stopped
    }

    // If called from within callback thread, defer destroy to avoid deadlock
    // (AudioOutputUnitStop deadlocks when called from render thread)
    if (impl_->in_callback.load(std::memory_order_acquire) > 0) {
        // Just set running=false (already done above), callback will exit naturally
        // Actual cleanup happens in destructor
        return;
    }

    // Destroy AudioUnit immediately to release device resources
    // (will be recreated on next start() call)
    impl_->destroy_unit();
}

auto InputStreamDarwin::is_running() const noexcept -> bool
{
    return impl_->running.load(std::memory_order_acquire);
}

auto InputStreamDarwin::config() const noexcept -> AudioConfig const&
{
    return impl_->config;
}

auto InputStreamDarwin::stream_time() const noexcept -> double
{
    return impl_->stream_time.load();
}

auto InputStreamDarwin::effective_sample_rate() const noexcept -> double
{
    return impl_->effective_sample_rate;
}

auto InputStreamDarwin::effective_period_frames() const noexcept -> uint32_t
{
    return impl_->max_frames_per_slice;
}

auto InputStreamDarwin::effective_buffer_frames() const noexcept -> uint32_t
{
    return impl_->device_buffer_frames;
}

auto InputStreamDarwin::device() const noexcept -> DeviceInfo const&
{
    return impl_->device_info;
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
        return std::make_unique<InputStreamDarwin>(*device_info, config);
#    if __cpp_exceptions
    } catch (...) {
        return failure(AudioError::InitializationFailed);
    }
#    endif
}

}  // namespace statusbar::audio

#endif  // defined(__APPLE__)
