// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/audio/audio_mock.hpp"

namespace statusbar::audio {

// MockDeviceProvider

auto MockDeviceProvider::default_input_device() -> std::optional<DeviceInfo>
{
    for (auto const& device : devices_) {
        if (device.uid == default_input_uid_ && device.max_input_channels > 0) {
            return device;
        }
    }
    // Fall back to first device with input channels
    for (auto const& device : devices_) {
        if (device.max_input_channels > 0) {
            return device;
        }
    }
    return std::nullopt;
}

auto MockDeviceProvider::default_output_device() -> std::optional<DeviceInfo>
{
    for (auto const& device : devices_) {
        if (device.uid == default_output_uid_ && device.max_output_channels > 0) {
            return device;
        }
    }
    // Fall back to first device with output channels
    for (auto const& device : devices_) {
        if (device.max_output_channels > 0) {
            return device;
        }
    }
    return std::nullopt;
}

auto MockDeviceProvider::find_device(std::string_view const identifier, bool const is_input) -> std::optional<DeviceInfo>
{
    if (identifier == "default") {
        return is_input ? default_input_device() : default_output_device();
    }

    for (auto const& device : devices_) {
        if (device.uid == identifier || device.name == identifier) {
            // Check if device has appropriate channels
            if (is_input && device.max_input_channels > 0) {
                return device;
            }
            if (!is_input && device.max_output_channels > 0) {
                return device;
            }
        }
    }
    return std::nullopt;
}

// MockOutputStream

MockOutputStream::MockOutputStream(DeviceInfo device, AudioConfig config, MockConfig mock_config)
    : device_(std::move(device))
    , config_(config)
    , mock_config_(mock_config)
    , running_(false)
    , callback_count_(0)
    , total_frames_(0)
{
    // Pre-allocate channel buffers
    output_buffers_.resize(config_.channels);
    channel_ptrs_.resize(config_.channels);
}

MockOutputStream::~MockOutputStream()
{
    running_ = false;
    if (simulation_thread_.joinable()) {
        simulation_thread_.join();
    }
}

auto MockOutputStream::start(AudioCallbackFloat&& callback) -> Status
{
    if (running_) {
        return failure(AudioError::AlreadyRunning);
    }

    if (mock_config_.error_on_start != AudioError::None) {
        return failure(mock_config_.error_on_start);
    }

    callback_ = std::move(callback);
    running_ = true;
    callback_count_ = 0;
    total_frames_ = 0;
    stream_time_.publish(0.0);

    // Clear capture buffers
    for (auto& buf : output_buffers_) {
        buf.clear();
    }

    // Start simulation thread
    simulation_thread_ = std::thread([this]() -> void {
#if __cpp_exceptions
        try {
#endif
            simulation_loop();
#if __cpp_exceptions
        } catch (std::exception const& e) {
            // Can't really log from here safely, but we need to catch to avoid terminate
            (void)e;
            running_ = false;
        } catch (...) {
            running_ = false;
        }
#endif
    });

    return success();
}

void MockOutputStream::stop()
{
    if (!running_) {
        return;  // Already stopped - idempotent
    }

    running_ = false;

    if (simulation_thread_.joinable()) {
        simulation_thread_.join();
    }

    callback_ = nullptr;
}

auto MockOutputStream::get_peak_level(uint32_t const channel) const -> float
{
    if (channel >= output_buffers_.size()) {
        return 0.0F;
    }
    float peak = 0.0F;
    for (float const sample : output_buffers_[channel]) {
        peak = std::max(peak, std::fabs(sample));
    }
    return peak;
}

auto MockOutputStream::has_output() const -> bool
{
    for (auto const& buf : output_buffers_) {
        for (float const sample : buf) {
            if (sample != 0.0F) {
                return true;
            }
        }
    }
    return false;
}

auto MockOutputStream::simulation_loop() -> void
{
    uint32_t const frames_per_callback = mock_config_.callback_frames > 0 ? mock_config_.callback_frames : config_.buffer_frames;

    // Allocate scratch buffers for callback
    std::vector<float> scratch(static_cast<size_t>(frames_per_callback) * config_.channels);
    std::vector<float*> out_ptrs(config_.channels);

    // Pre-allocate callback output buffer structs
    callback_output_buffers_.resize(config_.channels);

    // Calculate callback interval for latency simulation
    double const sample_rate = static_cast<double>(static_cast<uint32_t>(config_.sample_rate));
    auto callback_interval = std::chrono::microseconds(static_cast<long long>(frames_per_callback / sample_rate * 1000000.0));

    while (running_) {
        if (mock_config_.max_callbacks > 0 && callback_count_ >= mock_config_.max_callbacks) {
            running_ = false;
            break;
        }

        prepare_output_buffers(out_ptrs, scratch, frames_per_callback);

        if (!invoke_callback_and_check(out_ptrs, frames_per_callback)) {
            running_ = false;
            break;
        }

        capture_output(out_ptrs, frames_per_callback);

        ++callback_count_;
        total_frames_ += frames_per_callback;
        stream_time_.publish(static_cast<double>(total_frames_) / sample_rate);

        if (mock_config_.simulate_latency) {
            std::this_thread::sleep_for(callback_interval);
        }
    }
}

void MockOutputStream::prepare_output_buffers(std::vector<float*>& out_ptrs, std::vector<float>& scratch, uint32_t frames)
{
    for (uint32_t ch = 0; ch < config_.channels; ++ch) {
        out_ptrs[ch] = scratch.data() + (static_cast<size_t>(ch) * frames);
        std::fill_n(out_ptrs[ch], frames, 0.0F);
    }
}

auto MockOutputStream::invoke_callback_and_check(std::vector<float*>& out_ptrs, uint32_t frames) -> bool
{
    bool callback_failed = false;
    if (callback_) {
        for (uint32_t ch = 0; ch < config_.channels; ++ch) {
            callback_output_buffers_[ch].sample = std::span<float>(out_ptrs[ch], frames);
        }
        AudioCallbackParams params{};
        params.output_buffers = callback_output_buffers_;
        params.stream_time = stream_time_.load();
        auto result = callback_(params);
        callback_failed = !result.has_value();
    }
    if (mock_config_.callback_return_value != 0) {
        callback_failed = true;
    }
    return !callback_failed;
}

void MockOutputStream::capture_output(std::vector<float*> const& out_ptrs, uint32_t frames)
{
    if (!mock_config_.capture_output) {
        return;
    }
    for (uint32_t ch = 0; ch < config_.channels; ++ch) {
        size_t const current_size = output_buffers_[ch].size();
        size_t const max_size =
            mock_config_.max_capture_frames > 0 ? mock_config_.max_capture_frames : std::numeric_limits<size_t>::max();
        if (current_size < max_size) {
            size_t const to_copy = std::min(static_cast<size_t>(frames), max_size - current_size);
            output_buffers_[ch].insert(output_buffers_[ch].end(), out_ptrs[ch], out_ptrs[ch] + to_copy);
        }
    }
}

// MockInputStream

MockInputStream::MockInputStream(DeviceInfo device, AudioConfig config, MockConfig mock_config)
    : device_(std::move(device))
    , config_(config)
    , mock_config_(mock_config)
    , running_(false)
    , callback_count_(0)
    , total_frames_(0)
    , input_position_(0)
{
    input_buffers_.resize(config_.channels);
}

MockInputStream::~MockInputStream()
{
    running_ = false;
    if (simulation_thread_.joinable()) {
        simulation_thread_.join();
    }
}

auto MockInputStream::generate_sine_wave(float const frequency, float const amplitude, size_t const frames) -> void
{
    double const sample_rate = static_cast<double>(static_cast<uint32_t>(config_.sample_rate));
    for (uint32_t ch = 0; ch < config_.channels; ++ch) {
        input_buffers_[ch].resize(frames);
        for (size_t i = 0; i < frames; ++i) {
            double const t = static_cast<double>(i) / sample_rate;
            input_buffers_[ch][i] = amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * frequency * t));
        }
    }
}

auto MockInputStream::start(AudioCallbackFloat&& callback) -> Status
{
    if (running_) {
        return failure(AudioError::AlreadyRunning);
    }

    if (mock_config_.error_on_start != AudioError::None) {
        return failure(mock_config_.error_on_start);
    }

    callback_ = std::move(callback);
    running_ = true;
    callback_count_ = 0;
    total_frames_ = 0;
    stream_time_.publish(0.0);
    input_position_ = 0;

    simulation_thread_ = std::thread([this]() -> void {
#if __cpp_exceptions
        try {
#endif
            simulation_loop();
#if __cpp_exceptions
        } catch (...) {
            running_ = false;
        }
#endif
    });

    return success();
}

void MockInputStream::stop()
{
    if (!running_) {
        return;  // Already stopped - idempotent
    }

    running_ = false;

    if (simulation_thread_.joinable()) {
        simulation_thread_.join();
    }

    callback_ = nullptr;
}

auto MockInputStream::simulation_loop() -> void
{
    uint32_t const frames_per_callback = mock_config_.callback_frames > 0 ? mock_config_.callback_frames : config_.buffer_frames;

    // Allocate scratch buffers
    std::vector<float> scratch(static_cast<size_t>(frames_per_callback) * config_.channels);
    std::vector<float*> in_ptrs(config_.channels);

    // Pre-allocate callback input buffer structs
    callback_input_buffers_.resize(config_.channels);

    double const sample_rate = static_cast<double>(static_cast<uint32_t>(config_.sample_rate));
    auto callback_interval = std::chrono::microseconds(static_cast<long long>(frames_per_callback / sample_rate * 1000000.0));

    while (running_) {
        if (mock_config_.max_callbacks > 0 && callback_count_ >= mock_config_.max_callbacks) {
            running_ = false;
            break;
        }

        fill_input_buffers(in_ptrs, scratch, frames_per_callback);

        if (!invoke_input_callback(in_ptrs, frames_per_callback)) {
            running_ = false;
            break;
        }

        if (!input_buffers_.empty() && !input_buffers_[0].empty()) {
            input_position_ = (input_position_ + frames_per_callback) % input_buffers_[0].size();
        }

        ++callback_count_;
        total_frames_ += frames_per_callback;
        stream_time_.publish(static_cast<double>(total_frames_) / sample_rate);

        if (mock_config_.simulate_latency) {
            std::this_thread::sleep_for(callback_interval);
        }
    }
}

void MockInputStream::fill_input_buffers(std::vector<float*>& in_ptrs, std::vector<float>& scratch, uint32_t frames)
{
    for (uint32_t ch = 0; ch < config_.channels; ++ch) {
        float* buf = scratch.data() + (static_cast<size_t>(ch) * frames);
        in_ptrs[ch] = buf;

        if (ch < input_buffers_.size() && !input_buffers_[ch].empty()) {
            size_t const input_size = input_buffers_[ch].size();
            for (uint32_t i = 0; i < frames; ++i) {
                buf[i] = input_buffers_[ch][(input_position_ + i) % input_size];
            }
        } else {
            std::fill_n(buf, frames, 0.0F);
        }
    }
}

auto MockInputStream::invoke_input_callback(std::vector<float*>& in_ptrs, uint32_t frames) -> bool
{
    if (!callback_) {
        return true;
    }
    for (uint32_t ch = 0; ch < config_.channels; ++ch) {
        callback_input_buffers_[ch].sample = std::span<float const>(in_ptrs[ch], frames);
    }
    AudioCallbackParams params{};
    params.input_buffers = callback_input_buffers_;
    params.stream_time = stream_time_.load();
    return callback_(params).has_value();
}

// MockStreamFactory

auto MockStreamFactory::create_output_stream(DeviceInfo const& device, AudioConfig const& config)
    -> StatusValue<std::unique_ptr<OutputStream>>
{
    if (output_create_error_ != AudioError::None) {
        return failure(output_create_error_);
    }

    auto stream = std::make_unique<MockOutputStream>(device, config, output_config_);
    last_output_stream_ = stream.get();
    return stream;
}

auto MockStreamFactory::create_input_stream(DeviceInfo const& device, AudioConfig const& config)
    -> StatusValue<std::unique_ptr<InputStream>>
{
    if (input_create_error_ != AudioError::None) {
        return failure(input_create_error_);
    }

    auto stream = std::make_unique<MockInputStream>(device, config, input_config_);
    last_input_stream_ = stream.get();
    return stream;
}

// Helper Functions

auto make_mock_device(std::string name, std::string uid, uint32_t const in_channels, uint32_t const out_channels) -> DeviceInfo
{
    return DeviceInfo{
        .name = std::move(name),
        .uid = std::move(uid),
        .max_input_channels = in_channels,
        .max_output_channels = out_channels,
        .supported_sample_rates = {44100, 48000, 96000},
        .is_default_input = false,
        .is_default_output = false};
}

auto make_default_mock_provider() -> std::unique_ptr<MockDeviceProvider>
{
    auto provider = std::make_unique<MockDeviceProvider>();
    auto device = make_mock_device("Mock Audio Device", "mock-default", 2, 2);
    device.is_default_input = true;
    device.is_default_output = true;
    provider->add_device(device);
    provider->set_default_input("mock-default");
    provider->set_default_output("mock-default");
    return provider;
}

}  // namespace statusbar::audio
