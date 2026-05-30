// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/benchmark/benchmark.hpp"
#include "statusbar/dsp/dsp.hpp"

#include <array>
#include <cmath>
#include <print>
#include <span>
#include <vector>

using namespace statusbar::benchmark;
using namespace statusbar::dsp;

static constexpr size_t BLOCK_256 = 256;
static constexpr float SAMPLE_RATE = 48000.0F;
static constexpr float FREQUENCY = 1000.0F;

static auto make_sine_f32(size_t len) -> std::vector<float>
{
    std::vector<float> buf(len);
    generate_sine(std::span{buf}, FREQUENCY, SAMPLE_RATE);
    return buf;
}

static auto make_sine_f32x4(size_t len) -> std::vector<simd_float32x4>
{
    auto scalar = make_sine_f32(len);
    std::vector<simd_float32x4> buf(len);
    for (size_t i = 0; i < len; ++i) {
        buf[i] = simd_float32x4::splat(scalar[i]);
    }
    return buf;
}

static void bench_biquad()
{
    std::println("=== Biquad Filter ===\n");

    BenchmarkConfig const cfg{.warmup_iterations = 200, .measurement_iterations = 2000, .batch_size = 10};

    auto const params = FilterParams<>{
        .sample_rate_recip = 1.0 / static_cast<double>(SAMPLE_RATE),
        .frequency = static_cast<double>(FREQUENCY),
        .q = 0.707,
    };

    // Per-sample scalar f32
    {
        BiQuad<float> filter;
        filter.coeffs.calculate_lowpass(params);
        auto signal = make_sine_f32(BLOCK_256);
        auto stats = run(
            "biquad/process_f32_per_sample",
            [&]() {
                float sum = 0.0F;
                for (auto s : signal) {
                    sum += filter(s);
                }
                do_not_optimize(sum);
            },
            cfg);
        report("biquad/process_f32_per_sample", stats);
    }

    // Block processing at various sizes
    for (size_t sz : {64UL, 256UL, 1024UL, 4096UL}) {
        BiQuad<float> filter;
        filter.coeffs.calculate_lowpass(params);
        auto signal = make_sine_f32(sz);
        auto name = std::format("biquad/block_f32/{}", sz);
        auto stats = run(
            name,
            [&]() {
                process_in_place(filter, std::span{signal});
                do_not_optimize(signal.data());
            },
            cfg);
        report(name, stats);
    }

    // F32x4 (4-channel SIMD)
    {
        BiQuad<simd_float32x4> filter;
        filter.coeffs.calculate_lowpass(params);
        auto signal = make_sine_f32x4(BLOCK_256);
        auto stats = run(
            "biquad/process_f32x4_per_sample",
            [&]() {
                simd_float32x4 sum = simd_float32x4::zero();
                for (auto s : signal) {
                    sum = sum + filter(s);
                }
                do_not_optimize(sum);
            },
            cfg);
        report("biquad/process_f32x4_per_sample", stats);
    }

    {
        BiQuad<simd_float32x4> filter;
        filter.coeffs.calculate_lowpass(params);
        auto signal = make_sine_f32x4(BLOCK_256);
        auto stats = run(
            "biquad/block_f32x4_256",
            [&]() {
                process_in_place(filter, std::span{signal});
                do_not_optimize(signal.data());
            },
            cfg);
        report("biquad/block_f32x4_256", stats);
    }

    // All filter types at 256 samples
    auto const params_with_gain = FilterParams<>{
        .sample_rate_recip = 1.0 / static_cast<double>(SAMPLE_RATE),
        .frequency = static_cast<double>(FREQUENCY),
        .q = 0.707,
        .gain_db = 6.0,
    };

    struct FilterSetup
    {
        char const* name;
        void (*setup)(BiQuad<float>::Coeffs&, FilterParams<> const&);
    };
    std::array const filter_types = {
        FilterSetup{"lowpass", [](BiQuad<float>::Coeffs& c, FilterParams<> const& p) { c.calculate_lowpass(p); }},
        FilterSetup{"highpass", [](BiQuad<float>::Coeffs& c, FilterParams<> const& p) { c.calculate_highpass(p); }},
        FilterSetup{"bandpass", [](BiQuad<float>::Coeffs& c, FilterParams<> const& p) { c.calculate_bandpass(p); }},
        FilterSetup{"notch", [](BiQuad<float>::Coeffs& c, FilterParams<> const& p) { c.calculate_notch(p); }},
        FilterSetup{"peak", [](BiQuad<float>::Coeffs& c, FilterParams<> const& p) { c.calculate_peak(p); }},
        FilterSetup{"lowshelf", [](BiQuad<float>::Coeffs& c, FilterParams<> const& p) { c.calculate_lowshelf(p); }},
        FilterSetup{"highshelf", [](BiQuad<float>::Coeffs& c, FilterParams<> const& p) { c.calculate_highshelf(p); }},
    };
    for (auto const& [ft_name, setup_fn] : filter_types) {
        BiQuad<float> filter;
        setup_fn(filter.coeffs, params_with_gain);
        auto signal = make_sine_f32(BLOCK_256);
        auto name = std::format("biquad/type_{}_256", ft_name);
        auto stats = run(
            name,
            [&]() {
                process_in_place(filter, std::span{signal});
                do_not_optimize(signal.data());
            },
            cfg);
        report(name, stats);
    }

    // Coefficient design cost
    {
        auto stats = run(
            "biquad/design_lowpass",
            [&]() {
                BiQuad<float>::Coeffs coeffs;
                coeffs.calculate_lowpass(params);
                do_not_optimize(coeffs);
            },
            BenchmarkConfig{.warmup_iterations = 500, .measurement_iterations = 5000, .batch_size = 100});
        report("biquad/design_lowpass", stats);
    }

    std::println("");
}

static void bench_complex_biquad()
{
    std::println("=== Complex Biquad Filter ===\n");

    BenchmarkConfig const cfg{.warmup_iterations = 200, .measurement_iterations = 2000, .batch_size = 10};
    auto const params = FilterParams<>{
        .sample_rate_recip = 1.0 / static_cast<double>(SAMPLE_RATE), .frequency = static_cast<double>(FREQUENCY), .q = 0.707};

    {
        ComplexBiQuad<float> filter;
        filter.coeffs.calculate_lowpass(params);
        auto signal = make_sine_f32(BLOCK_256);
        auto stats = run(
            "complex_biquad/process_f32_per_sample",
            [&]() {
                float sum = 0.0F;
                for (auto s : signal) {
                    sum += filter(s);
                }
                do_not_optimize(sum);
            },
            cfg);
        report("complex_biquad/process_f32_per_sample", stats);
    }

    {
        ComplexBiQuad<float> filter;
        filter.coeffs.calculate_lowpass(params);
        auto signal = make_sine_f32(BLOCK_256);
        auto stats = run(
            "complex_biquad/block_f32_256",
            [&]() {
                process_in_place(filter, std::span{signal});
                do_not_optimize(signal.data());
            },
            cfg);
        report("complex_biquad/block_f32_256", stats);
    }

    {
        ComplexBiQuad<simd_float32x4> filter;
        filter.coeffs.calculate_lowpass(params);
        auto signal = make_sine_f32x4(BLOCK_256);
        auto stats = run(
            "complex_biquad/process_f32x4_per_sample",
            [&]() {
                simd_float32x4 sum = simd_float32x4::zero();
                for (auto s : signal) {
                    sum = sum + filter(s);
                }
                do_not_optimize(sum);
            },
            cfg);
        report("complex_biquad/process_f32x4_per_sample", stats);
    }

    std::println("");
}

static void bench_gain()
{
    std::println("=== Gain ===\n");

    BenchmarkConfig const cfg{.warmup_iterations = 200, .measurement_iterations = 2000, .batch_size = 10};

    {
        Gain<float> gain;
        gain.coeffs.set_amplitude(0.5F);
        gain.coeffs.set_time_constant(static_cast<double>(SAMPLE_RATE), 0.01);
        auto signal = make_sine_f32(BLOCK_256);
        auto stats = run(
            "gain/process_f32_256",
            [&]() {
                process_in_place(gain, std::span{signal});
                do_not_optimize(signal.data());
            },
            cfg);
        report("gain/process_f32_256", stats);
    }

    {
        Gain<simd_float32x4> gain;
        gain.coeffs.set_amplitude(0.5F);
        gain.coeffs.set_time_constant(static_cast<double>(SAMPLE_RATE), 0.01);
        auto signal = make_sine_f32x4(BLOCK_256);
        auto stats = run(
            "gain/process_f32x4_256",
            [&]() {
                process_in_place(gain, std::span{signal});
                do_not_optimize(signal.data());
            },
            cfg);
        report("gain/process_f32x4_256", stats);
    }

    std::println("");
}

static void bench_oscillator()
{
    std::println("=== Oscillator ===\n");

    BenchmarkConfig const cfg{.warmup_iterations = 200, .measurement_iterations = 2000, .batch_size = 10};

    {
        Oscillator<float> osc;
        osc.state_.set_frequency(FrequencyParameters<float>{.sample_rate_recip = 1.0F / SAMPLE_RATE, .frequency = FREQUENCY});
        osc.coeffs_.set_amplitude(1.0F, 0);
        std::vector<float> buf(BLOCK_256, 0.0F);
        auto stats = run(
            "oscillator/process_f32_256",
            [&]() {
                process_in_place(osc, std::span{buf});
                do_not_optimize(buf.data());
            },
            cfg);
        report("oscillator/process_f32_256", stats);
    }

    {
        Oscillator<simd_float32x4> osc;
        osc.state_.set_frequency(FrequencyParameters<float>{.sample_rate_recip = 1.0F / SAMPLE_RATE, .frequency = FREQUENCY});
        for (size_t ch = 0; ch < 4; ++ch) {
            osc.coeffs_.set_amplitude(1.0F, ch);
        }
        std::vector<simd_float32x4> buf(BLOCK_256, simd_float32x4::zero());
        auto stats = run(
            "oscillator/process_f32x4_256",
            [&]() {
                process_in_place(osc, std::span{buf});
                do_not_optimize(buf.data());
            },
            cfg);
        report("oscillator/process_f32x4_256", stats);
    }

    std::println("");
}

static void bench_analysis()
{
    std::println("=== Signal Analysis ===\n");

    BenchmarkConfig const cfg{.warmup_iterations = 200, .measurement_iterations = 2000, .batch_size = 10};

    for (size_t sz : {256UL, 1024UL, 4096UL}) {
        auto signal = make_sine_f32(sz);

        {
            auto name = std::format("analysis/rms_f32/{}", sz);
            auto stats = run(
                name,
                [&]() {
                    auto rms = calculate_rms(std::span<float const>{signal});
                    do_not_optimize(rms);
                },
                cfg);
            report(name, stats);
        }

        {
            auto name = std::format("analysis/zero_crossings_f32/{}", sz);
            auto stats = run(
                name,
                [&]() {
                    auto zc = count_zero_crossings(std::span<float const>{signal});
                    do_not_optimize(zc);
                },
                cfg);
            report(name, stats);
        }
    }

    std::println("");
}

static void bench_block_processing()
{
    std::println("=== Block Processing Patterns ===\n");

    BenchmarkConfig const cfg{.warmup_iterations = 200, .measurement_iterations = 2000, .batch_size = 10};
    auto const params = FilterParams<>{
        .sample_rate_recip = 1.0 / static_cast<double>(SAMPLE_RATE), .frequency = static_cast<double>(FREQUENCY), .q = 0.707};

    BiQuad<float> filter;
    filter.coeffs.calculate_lowpass(params);

    {
        auto signal = make_sine_f32(BLOCK_256);
        auto stats = run(
            "block_processing/in_place_256",
            [&]() {
                process_in_place(filter, std::span{signal});
                do_not_optimize(signal.data());
            },
            cfg);
        report("block_processing/in_place_256", stats);
    }

    {
        auto input = make_sine_f32(BLOCK_256);
        std::vector<float> output(BLOCK_256, 0.0F);
        auto stats = run(
            "block_processing/copy_256",
            [&]() {
                process(filter, std::span<float const>{input}, std::span{output});
                do_not_optimize(output.data());
            },
            cfg);
        report("block_processing/copy_256", stats);
    }

    {
        auto input = make_sine_f32(BLOCK_256);
        std::vector<float> output(BLOCK_256, 0.0F);
        auto stats = run(
            "block_processing/accumulate_256",
            [&]() {
                process_accumulate(filter, std::span<float const>{input}, std::span{output});
                do_not_optimize(output.data());
            },
            cfg);
        report("block_processing/accumulate_256", stats);
    }

    {
        auto signal = make_sine_f32(BLOCK_256);
        auto stats = run(
            "block_processing/mix_in_place_256",
            [&]() {
                process_mix_in_place(filter, std::span{signal});
                do_not_optimize(signal.data());
            },
            cfg);
        report("block_processing/mix_in_place_256", stats);
    }

    std::println("");
}

static void bench_db_conversion()
{
    std::println("=== dB Conversion ===\n");

    BenchmarkConfig const cfg{.warmup_iterations = 500, .measurement_iterations = 5000, .batch_size = 100};

    {
        auto stats = run(
            "db_conversion/amplitude_to_db_f32",
            [&]() {
                float sum = 0.0F;
                for (int i = 1; i <= 256; ++i) {
                    sum += static_cast<float>(20.0 * std::log10(static_cast<double>(i) / 256.0));
                }
                do_not_optimize(sum);
            },
            cfg);
        report("db_conversion/amplitude_to_db_f32", stats);
    }

    {
        auto stats = run(
            "db_conversion/db_to_amplitude_f32",
            [&]() {
                float sum = 0.0F;
                for (int i = 0; i < 256; ++i) {
                    sum += static_cast<float>(std::pow(10.0, (-60.0 + static_cast<double>(i) * 0.5) / 20.0));
                }
                do_not_optimize(sum);
            },
            cfg);
        report("db_conversion/db_to_amplitude_f32", stats);
    }

    std::println("");
}

auto main() -> int
{
    std::println("StatusBar DSP Benchmarks (C++)\n");
    std::println("Block size: {} samples, Sample rate: {} Hz, Frequency: {} Hz\n", BLOCK_256, SAMPLE_RATE, FREQUENCY);

    bench_biquad();
    bench_complex_biquad();
    bench_gain();
    bench_oscillator();
    bench_analysis();
    bench_block_processing();
    bench_db_conversion();

    std::println("Done.");
    return 0;
}
