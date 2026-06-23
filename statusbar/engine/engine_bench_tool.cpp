// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT
//
// Engine benchmark tool — measures per-cycle / per-leg costs of the
// engine's Tier 0 elements and Tier 1 producers.
//
// Build: make build
// Run:   ./build/build-Release/statusbar/statusbar-engine-bench
//
// Reference budgets at SR = 48 kHz:
//   one cycle = 8 samples = 166.667 µs
//   one leg   = 256 samples = 32 cycles = 5.333 ms

#include "statusbar/benchmark/benchmark.hpp"
#include "statusbar/dsp/dsp.hpp"
#include "statusbar/engine/engine.hpp"

#include <array>
#include <print>
#include <span>
#include <vector>

using namespace statusbar::benchmark;
using namespace statusbar::engine;
using namespace statusbar::itc;
using statusbar::dsp::simd_float32x4;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

constexpr size_t SR = 48000;
using EC = Engine48k;
constexpr size_t SAMPLES_PER_LEG = EC::samples_per_leg;  // 256
constexpr size_t CYCLES_PER_LEG = EC::cycles_per_leg;    // 32
constexpr size_t SPC = EC::samples_per_cycle;            // 8

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

template <typename T>
auto make_lowpass_coeffs(double freq_hz, double q) -> BiquadComplexCoeffs<T>
{
    statusbar::dsp::BiquadDesignParams const params{
        .type = statusbar::dsp::BiquadFilterType::Lowpass,
        .frequency_hz = freq_hz,
        .q = q,
        .gain_db = 0.0,
    };
    auto const pair = statusbar::dsp::design_biquad_pole_zero(params, 1.0 / static_cast<double>(SR));
    BiquadComplexCoeffs<T> c{};
    for (size_t ch = 0; ch < statusbar::dsp::simd_flattened_size<T>::value; ++ch) {
        statusbar::dsp::set_flattened_item(c.stage1.a0_re, pair.first.a0_re, ch);
        statusbar::dsp::set_flattened_item(c.stage1.a0_im, pair.first.a0_im, ch);
        statusbar::dsp::set_flattened_item(c.stage1.a1_re, pair.first.a1_re, ch);
        statusbar::dsp::set_flattened_item(c.stage1.a1_im, pair.first.a1_im, ch);
        statusbar::dsp::set_flattened_item(c.stage1.b1_re, pair.first.b1_re, ch);
        statusbar::dsp::set_flattened_item(c.stage1.b1_im, pair.first.b1_im, ch);
        statusbar::dsp::set_flattened_item(c.stage2.a0_re, pair.second.a0_re, ch);
        statusbar::dsp::set_flattened_item(c.stage2.a0_im, pair.second.a0_im, ch);
        statusbar::dsp::set_flattened_item(c.stage2.a1_re, pair.second.a1_re, ch);
        statusbar::dsp::set_flattened_item(c.stage2.a1_im, pair.second.a1_im, ch);
        statusbar::dsp::set_flattened_item(c.stage2.b1_re, pair.second.b1_re, ch);
        statusbar::dsp::set_flattened_item(c.stage2.b1_im, pair.second.b1_im, ch);
    }
    return c;
}

template <typename T>
void prime_with_coeffs(SmoothedElement<SR, T, BiquadComplexCoeffs<T>, BiquadApply<T>>& el, BiquadComplexCoeffs<T> const& c)
{
    el.prime_segment(make_segment(c, c, EC::recip_samples_per_leg_f));
}

template <typename Element, typename T>
void drive_full_leg(Element& element, std::span<T const> input_buf, std::span<T> output_buf)
{
    for (size_t c = 0; c < CYCLES_PER_LEG; ++c) {
        element.process_cycle(
            std::span<T const, SPC>{input_buf.data() + (c * SPC), SPC}, std::span<T, SPC>{output_buf.data() + (c * SPC), SPC});
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// MessagePipe benchmarks
// ---------------------------------------------------------------------------

static void bench_message_pipe()
{
    std::println("=== MessagePipe ===");

    BenchmarkConfig const cfg{.warmup_iterations = 500, .measurement_iterations = 5000, .batch_size = 100};

    // LatestPipe: publish + try_consume single-thread roundtrip.
    {
        LatestPipe<int> pipe;
        int value = 0;
        auto stats = run(
            "latest_pipe/publish_then_consume",
            [&]() {
                pipe.publish(++value);
                auto v = pipe.try_consume();
                do_not_optimize(v);
            },
            cfg);
        report("latest_pipe/publish_then_consume", stats);
    }

    // QueuedPipe: enqueue + dequeue roundtrip
    {
        QueuedPipe<int, 64> pipe;
        int value = 0;
        auto stats = run(
            "queued_pipe/publish_then_consume",
            [&]() {
                (void)pipe.try_publish(++value);
                auto v = pipe.try_consume();
                do_not_optimize(v);
            },
            cfg);
        report("queued_pipe/publish_then_consume", stats);
    }

    // TimedPipe: publish-with-timestamp + try_consume
    {
        TimedPipe<int, 64> pipe;
        uint64_t ts = 0;
        int value = 0;
        auto stats = run(
            "timed_pipe/publish_then_consume",
            [&]() {
                (void)pipe.try_publish(ts++, ++value);
                auto v = pipe.try_consume();
                do_not_optimize(v);
            },
            cfg);
        report("timed_pipe/publish_then_consume", stats);
    }

    std::println("");
}

// ---------------------------------------------------------------------------
// GainElement benchmarks
// ---------------------------------------------------------------------------

static void bench_gain_element()
{
    std::println("=== GainElement ({}-sample leg) ===", SAMPLES_PER_LEG);

    BenchmarkConfig const cfg{.warmup_iterations = 200, .measurement_iterations = 2000, .batch_size = 10};

    // Scalar float, full leg
    {
        GainElement<SR, float> el;
        el.prime_segment(
            make_segment(GainAmplitudeCoeffs<float>{0.5F}, GainAmplitudeCoeffs<float>{0.7F}, EC::recip_samples_per_leg_f));
        std::vector<float> in(SAMPLES_PER_LEG, 0.5F);
        std::vector<float> out(SAMPLES_PER_LEG);
        auto stats = run(
            "gain_element/leg_f32_1ch",
            [&]() {
                drive_full_leg(el, std::span<float const>{in}, std::span<float>{out});
                do_not_optimize(out.data());
            },
            cfg);
        report("gain_element/leg_f32_1ch", stats);
    }

    // SIMD float32x4, full leg (4 channels)
    {
        GainElement<SR, simd_float32x4> el;
        GainAmplitudeCoeffs<simd_float32x4> start{};
        GainAmplitudeCoeffs<simd_float32x4> end{};
        start.amplitude = simd_float32x4::splat(0.5F);
        end.amplitude = simd_float32x4::splat(0.7F);
        el.prime_segment(make_segment(start, end, EC::recip_samples_per_leg_f));
        std::vector<simd_float32x4> in(SAMPLES_PER_LEG, simd_float32x4::splat(0.5F));
        std::vector<simd_float32x4> out(SAMPLES_PER_LEG);
        auto stats = run(
            "gain_element/leg_f32x4_4ch",
            [&]() {
                drive_full_leg(el, std::span<simd_float32x4 const>{in}, std::span<simd_float32x4>{out});
                do_not_optimize(out.data());
            },
            cfg);
        report("gain_element/leg_f32x4_4ch", stats);
    }

    std::println("");
}

// ---------------------------------------------------------------------------
// BiquadElement benchmarks
// ---------------------------------------------------------------------------

static void bench_biquad_element()
{
    std::println("=== BiquadElement ({}-sample leg) ===", SAMPLES_PER_LEG);

    BenchmarkConfig const cfg{.warmup_iterations = 200, .measurement_iterations = 2000, .batch_size = 10};

    // Scalar float
    {
        BiquadElement<SR, float> el;
        prime_with_coeffs(el, make_lowpass_coeffs<float>(1500.0, 0.707));
        std::vector<float> in(SAMPLES_PER_LEG, 0.5F);
        std::vector<float> out(SAMPLES_PER_LEG);
        auto stats = run(
            "biquad_element/leg_f32_1ch",
            [&]() {
                drive_full_leg(el, std::span<float const>{in}, std::span<float>{out});
                do_not_optimize(out.data());
            },
            cfg);
        report("biquad_element/leg_f32_1ch", stats);
    }

    // SIMD float32x4 (4 channels)
    {
        BiquadElement<SR, simd_float32x4> el;
        prime_with_coeffs(el, make_lowpass_coeffs<simd_float32x4>(1500.0, 0.707));
        std::vector<simd_float32x4> in(SAMPLES_PER_LEG, simd_float32x4::splat(0.5F));
        std::vector<simd_float32x4> out(SAMPLES_PER_LEG);
        auto stats = run(
            "biquad_element/leg_f32x4_4ch",
            [&]() {
                drive_full_leg(el, std::span<simd_float32x4 const>{in}, std::span<simd_float32x4>{out});
                do_not_optimize(out.data());
            },
            cfg);
        report("biquad_element/leg_f32x4_4ch", stats);
    }

    std::println("");
}

// ---------------------------------------------------------------------------
// BiquadChain benchmarks (N = 3 cascaded biquads)
// ---------------------------------------------------------------------------

static void bench_biquad_chain()
{
    std::println("=== BiquadChain<3> ({}-sample leg) ===", SAMPLES_PER_LEG);

    BenchmarkConfig const cfg{.warmup_iterations = 200, .measurement_iterations = 2000, .batch_size = 10};

    // Scalar float
    {
        BiquadChain<3, SR, float> el;
        BiquadChainCoeffs<3, float> c{};
        c.stages[0] = make_lowpass_coeffs<float>(500.0, 0.707);
        c.stages[1] = make_lowpass_coeffs<float>(2000.0, 0.707);
        c.stages[2] = make_lowpass_coeffs<float>(8000.0, 0.707);
        el.prime_segment(make_segment(c, c, EC::recip_samples_per_leg_f));
        std::vector<float> in(SAMPLES_PER_LEG, 0.5F);
        std::vector<float> out(SAMPLES_PER_LEG);
        auto stats = run(
            "biquad_chain3/leg_f32_1ch",
            [&]() {
                drive_full_leg(el, std::span<float const>{in}, std::span<float>{out});
                do_not_optimize(out.data());
            },
            cfg);
        report("biquad_chain3/leg_f32_1ch", stats);
    }

    // SIMD float32x4 (4 channels × 3 stages each)
    {
        BiquadChain<3, SR, simd_float32x4> el;
        BiquadChainCoeffs<3, simd_float32x4> c{};
        c.stages[0] = make_lowpass_coeffs<simd_float32x4>(500.0, 0.707);
        c.stages[1] = make_lowpass_coeffs<simd_float32x4>(2000.0, 0.707);
        c.stages[2] = make_lowpass_coeffs<simd_float32x4>(8000.0, 0.707);
        el.prime_segment(make_segment(c, c, EC::recip_samples_per_leg_f));
        std::vector<simd_float32x4> in(SAMPLES_PER_LEG, simd_float32x4::splat(0.5F));
        std::vector<simd_float32x4> out(SAMPLES_PER_LEG);
        auto stats = run(
            "biquad_chain3/leg_f32x4_4ch",
            [&]() {
                drive_full_leg(el, std::span<simd_float32x4 const>{in}, std::span<simd_float32x4>{out});
                do_not_optimize(out.data());
            },
            cfg);
        report("biquad_chain3/leg_f32x4_4ch", stats);
    }

    std::println("");
}

// ---------------------------------------------------------------------------
// Meter benchmarks
// ---------------------------------------------------------------------------

static void bench_meter()
{
    std::println("=== Meter ({}-sample leg) ===", SAMPLES_PER_LEG);

    BenchmarkConfig const cfg{.warmup_iterations = 200, .measurement_iterations = 2000, .batch_size = 10};

    // Scalar float
    {
        Meter<SR, float> meter;
        std::vector<float> in(SAMPLES_PER_LEG, 0.5F);
        std::vector<float> out(SAMPLES_PER_LEG);
        auto stats = run(
            "meter/leg_f32_1ch",
            [&]() {
                drive_full_leg(meter, std::span<float const>{in}, std::span<float>{out});
                do_not_optimize(out.data());
            },
            cfg);
        report("meter/leg_f32_1ch", stats);
    }

    // SIMD float32x4 (4 channels)
    {
        Meter<SR, simd_float32x4> meter;
        std::vector<simd_float32x4> in(SAMPLES_PER_LEG, simd_float32x4::splat(0.5F));
        std::vector<simd_float32x4> out(SAMPLES_PER_LEG);
        auto stats = run(
            "meter/leg_f32x4_4ch",
            [&]() {
                drive_full_leg(meter, std::span<simd_float32x4 const>{in}, std::span<simd_float32x4>{out});
                do_not_optimize(out.data());
            },
            cfg);
        report("meter/leg_f32x4_4ch", stats);
    }

    std::println("");
}

// ---------------------------------------------------------------------------
// GainMatrix benchmarks
// ---------------------------------------------------------------------------

static void bench_gain_matrix()
{
    std::println("=== GainMatrix<8,8> ({}-sample leg) ===", SAMPLES_PER_LEG);

    BenchmarkConfig const cfg{.warmup_iterations = 100, .measurement_iterations = 1000, .batch_size = 5};

    GainMatrix<8, 8, SR> mix;
    MatrixCoeffs<8, 8, float> coeffs{};
    for (size_t o = 0; o < 8; ++o) {
        for (size_t i = 0; i < 8; ++i) {
            coeffs.set(i, o, (i == o) ? 1.0F : 0.1F);  // identity-ish
        }
    }
    mix.prime_segment(Segment<MatrixCoeffs<8, 8, float>>{.start = coeffs, .end = coeffs, .delta = MatrixCoeffs<8, 8, float>{}});

    std::array<std::vector<float>, 8> in_storage;
    std::array<std::vector<float>, 8> out_storage;
    for (size_t i = 0; i < 8; ++i) {
        in_storage[i].assign(SAMPLES_PER_LEG, 0.5F);
        out_storage[i].assign(SAMPLES_PER_LEG, 0.0F);
    }

    auto stats = run(
        "gain_matrix_8x8/leg_f32",
        [&]() {
            for (size_t c = 0; c < CYCLES_PER_LEG; ++c) {
                std::array<std::span<float const, SPC>, 8> ins{
                    std::span<float const, SPC>{in_storage[0].data() + (c * SPC), SPC},
                    std::span<float const, SPC>{in_storage[1].data() + (c * SPC), SPC},
                    std::span<float const, SPC>{in_storage[2].data() + (c * SPC), SPC},
                    std::span<float const, SPC>{in_storage[3].data() + (c * SPC), SPC},
                    std::span<float const, SPC>{in_storage[4].data() + (c * SPC), SPC},
                    std::span<float const, SPC>{in_storage[5].data() + (c * SPC), SPC},
                    std::span<float const, SPC>{in_storage[6].data() + (c * SPC), SPC},
                    std::span<float const, SPC>{in_storage[7].data() + (c * SPC), SPC},
                };
                std::array<std::span<float, SPC>, 8> outs{
                    std::span<float, SPC>{out_storage[0].data() + (c * SPC), SPC},
                    std::span<float, SPC>{out_storage[1].data() + (c * SPC), SPC},
                    std::span<float, SPC>{out_storage[2].data() + (c * SPC), SPC},
                    std::span<float, SPC>{out_storage[3].data() + (c * SPC), SPC},
                    std::span<float, SPC>{out_storage[4].data() + (c * SPC), SPC},
                    std::span<float, SPC>{out_storage[5].data() + (c * SPC), SPC},
                    std::span<float, SPC>{out_storage[6].data() + (c * SPC), SPC},
                    std::span<float, SPC>{out_storage[7].data() + (c * SPC), SPC},
                };
                mix.process_cycle(ins, outs);
            }
            do_not_optimize(out_storage[0].data());
        },
        cfg);
    report("gain_matrix_8x8/leg_f32", stats);

    std::println("");
}

// ---------------------------------------------------------------------------
// Tier 1 designer benchmarks (per step / per leg)
// ---------------------------------------------------------------------------

static void bench_designers()
{
    std::println("=== Tier 1 designers (per step) ===");

    BenchmarkConfig const cfg{.warmup_iterations = 500, .measurement_iterations = 5000, .batch_size = 50};

    // GainDesigner<float> step (no input — hold)
    {
        GainDesigner<float> d;
        LatestPipe<Segment<GainAmplitudeCoeffs<float>>> out_pipe;
        d.connect_output(out_pipe);
        d.set_initial(0.5F);
        auto stats = run(
            "gain_designer/step_f32_1ch",
            [&]() {
                d.step(EC::recip_samples_per_leg_f);
                (void)out_pipe.try_consume();
            },
            cfg);
        report("gain_designer/step_f32_1ch", stats);
    }

    // GainDesigner<simd_float32x4> step (4 channels)
    {
        GainDesigner<simd_float32x4> d;
        LatestPipe<Segment<GainAmplitudeCoeffs<simd_float32x4>>> out_pipe;
        d.connect_output(out_pipe);
        d.set_initial(0.5F);
        auto stats = run(
            "gain_designer/step_f32x4_4ch",
            [&]() {
                d.step(EC::recip_samples_per_leg_f);
                (void)out_pipe.try_consume();
            },
            cfg);
        report("gain_designer/step_f32x4_4ch", stats);
    }

    // BiquadDesigner<float> step holding (no morph in progress)
    {
        BiquadDesigner<float> d{1.0 / static_cast<double>(SR)};
        LatestPipe<Segment<BiquadComplexCoeffs<float>>> out_pipe;
        d.connect_output(out_pipe);
        d.set_initial(
            statusbar::dsp::BiquadDesignParams{.type = statusbar::dsp::BiquadFilterType::Lowpass, .frequency_hz = 1500.0});
        auto stats = run(
            "biquad_designer/step_f32_hold",
            [&]() {
                d.step(EC::recip_samples_per_leg_f);
                (void)out_pipe.try_consume();
            },
            cfg);
        report("biquad_designer/step_f32_hold", stats);
    }

    // BiquadDesigner<float> step morphing (re-design every step)
    {
        BiquadDesigner<float> d{1.0 / static_cast<double>(SR)};
        LatestPipe<statusbar::dsp::BiquadDesignParams> in_pipe;
        LatestPipe<Segment<BiquadComplexCoeffs<float>>> out_pipe;
        d.connect_input(0, in_pipe);
        d.connect_output(out_pipe);
        d.set_initial(
            statusbar::dsp::BiquadDesignParams{.type = statusbar::dsp::BiquadFilterType::Lowpass, .frequency_hz = 1500.0});
        bool flip = false;
        auto stats = run(
            "biquad_designer/step_f32_morphing",
            [&]() {
                // Push a new target each step to keep the morph machine busy
                in_pipe.publish(statusbar::dsp::BiquadDesignParams{
                    .type = statusbar::dsp::BiquadFilterType::Lowpass,
                    .frequency_hz = flip ? 1000.0 : 4000.0,
                });
                flip = !flip;
                d.step(EC::recip_samples_per_leg_f);
                (void)out_pipe.try_consume();
            },
            cfg);
        report("biquad_designer/step_f32_morphing", stats);
    }

    // BiquadDesigner<simd_float32x4> step morphing (4 channels active)
    {
        BiquadDesigner<simd_float32x4> d{1.0 / static_cast<double>(SR)};
        std::array<LatestPipe<statusbar::dsp::BiquadDesignParams>, 4> in_pipes;
        LatestPipe<Segment<BiquadComplexCoeffs<simd_float32x4>>> out_pipe;
        for (size_t ch = 0; ch < 4; ++ch) {
            d.connect_input(ch, in_pipes[ch]);
        }
        d.connect_output(out_pipe);
        d.set_initial(
            statusbar::dsp::BiquadDesignParams{.type = statusbar::dsp::BiquadFilterType::Lowpass, .frequency_hz = 1500.0});
        bool flip = false;
        auto stats = run(
            "biquad_designer/step_f32x4_morphing",
            [&]() {
                for (size_t ch = 0; ch < 4; ++ch) {
                    in_pipes[ch].publish(statusbar::dsp::BiquadDesignParams{
                        .type = statusbar::dsp::BiquadFilterType::Lowpass,
                        .frequency_hz = flip ? 1000.0 + 500.0 * ch : 4000.0 - 500.0 * ch,
                    });
                }
                flip = !flip;
                d.step(EC::recip_samples_per_leg_f);
                (void)out_pipe.try_consume();
            },
            cfg);
        report("biquad_designer/step_f32x4_morphing", stats);
    }

    std::println("");
}

// ---------------------------------------------------------------------------
// Reference budgets reminder
// ---------------------------------------------------------------------------

static void print_budgets()
{
    std::println("");
    std::println("Reference budgets at SR={} Hz:", SR);
    std::println("  cycle (8 samples):   166666 ns");
    std::println("  leg   (256 samples): 5333333 ns");
    std::println("");
    std::println("A per-leg measurement of N ns means:");
    std::println("  N / 5333333 of the leg budget consumed by this element.");
    std::println("  N / 256 ns per sample.");
    std::println("");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

auto main() -> int
{
    std::println("Engine benchmark tool");
    print_budgets();

    bench_message_pipe();
    bench_gain_element();
    bench_biquad_element();
    bench_biquad_chain();
    bench_meter();
    bench_gain_matrix();
    bench_designers();

    return 0;
}
