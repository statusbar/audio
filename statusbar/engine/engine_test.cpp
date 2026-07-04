// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/engine/engine.hpp"

#include "statusbar/test/test.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <thread>

using namespace statusbar::engine;
using namespace statusbar::itc;

namespace {

// Trivial Coeffs for testing: a single float gain. Models the smallest valid
// SegmentableCoeffs — one scalar with the required arithmetic.
struct GainCoeffs
{
    float amplitude{};

    constexpr auto operator+(GainCoeffs const& o) const noexcept -> GainCoeffs { return {amplitude + o.amplitude}; }
    constexpr auto operator-(GainCoeffs const& o) const noexcept -> GainCoeffs { return {amplitude - o.amplitude}; }
    constexpr auto operator*(float s) const noexcept -> GainCoeffs { return {amplitude * s}; }
    constexpr auto operator==(GainCoeffs const&) const -> bool = default;
};

static_assert(SegmentableCoeffs<GainCoeffs>);

}  // namespace

TEST(engine_constants, derives_from_sample_rate)
{
    EXPECT_EQ(Engine48k::samples_per_cycle, 8U);
    EXPECT_EQ(Engine48k::cycles_per_leg, 32U);
    EXPECT_EQ(Engine48k::samples_per_leg, 256U);

    EXPECT_EQ(Engine96k::samples_per_cycle, 16U);
    EXPECT_EQ(Engine96k::samples_per_leg, 512U);

    EXPECT_EQ(Engine192k::samples_per_cycle, 32U);
    EXPECT_EQ(Engine192k::samples_per_leg, 1024U);
}

TEST(engine_constants, cycle_period_is_166_667_ns)
{
    // 1/6000 s = 166666.666... ns; integer division floors to 166666.
    EXPECT_EQ(Engine48k::cycle_period_ns, 166666U);
    EXPECT_EQ(Engine96k::cycle_period_ns, 166666U);
    EXPECT_EQ(Engine192k::cycle_period_ns, 166666U);
}

TEST(engine_constants, recip_is_exactly_representable)
{
    // 1/256, 1/512, 1/1024 are exactly representable in float (powers of two).
    EXPECT_EQ(Engine48k::recip_samples_per_leg_f * 256.0F, 1.0F);
    EXPECT_EQ(Engine96k::recip_samples_per_leg_f * 512.0F, 1.0F);
    EXPECT_EQ(Engine192k::recip_samples_per_leg_f * 1024.0F, 1.0F);
}

TEST(engine_segment, make_segment_computes_delta)
{
    auto const seg = make_segment(GainCoeffs{0.0F}, GainCoeffs{1.0F}, Engine48k::recip_samples_per_leg_f);
    EXPECT_EQ(seg.start.amplitude, 0.0F);
    EXPECT_EQ(seg.end.amplitude, 1.0F);
    EXPECT_EQ(seg.delta.amplitude, 1.0F / 256.0F);
}

TEST(engine_segment, hold_segment_has_zero_delta)
{
    auto const seg = hold_segment(GainCoeffs{0.5F});
    EXPECT_EQ(seg.start.amplitude, 0.5F);
    EXPECT_EQ(seg.end.amplitude, 0.5F);
    EXPECT_EQ(seg.delta.amplitude, 0.0F);
}

TEST(engine_segment, accumulation_lands_on_end_within_roundoff)
{
    auto const seg = make_segment(GainCoeffs{0.5F}, GainCoeffs{0.75F}, Engine48k::recip_samples_per_leg_f);

    GainCoeffs acc = seg.start;
    for (size_t i = 0; i < Engine48k::samples_per_leg; ++i) {
        acc = acc + seg.delta;
    }
    EXPECT_TRUE(std::abs(acc.amplitude - seg.end.amplitude) < 1e-6F);
}

TEST(engine_segment, formula_lands_on_end_exactly_for_power_of_two_legs)
{
    auto const seg = make_segment(GainCoeffs{0.5F}, GainCoeffs{0.75F}, Engine48k::recip_samples_per_leg_f);

    GainCoeffs const formula = seg.start + (seg.delta * static_cast<float>(Engine48k::samples_per_leg));
    // 0.5 + (0.25/256) * 256 = 0.5 + 0.25 = 0.75 exactly in float when N is a
    // power of two and recip is exactly representable.
    EXPECT_EQ(formula.amplitude, seg.end.amplitude);
}

// ─────────────────────────────────────────────────────────────────────────────
// SmoothedElement and scheduler tests.
//
// Use a trivial gain Apply: output = coeffs.amplitude * input.
// Demonstrates the full Tier 0 path: pipe → snap-at-leg → per-sample
// accumulate → apply.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

struct GainApply
{
    constexpr auto operator()(GainCoeffs const& c, float input) const noexcept -> float { return c.amplitude * input; }
};

using TestGainElement = SmoothedElement<48000, float, GainCoeffs, GainApply>;

}  // namespace

TEST(engine_smoothed_element, satisfies_is_element)
{
    static_assert(IsElement<TestGainElement>);
    EXPECT_EQ(TestGainElement::level, 0U);
    EXPECT_EQ(TestGainElement::data_in::extent, 8U);
}

TEST(engine_smoothed_element, primed_segment_ramps_amplitude_linearly)
{
    TestGainElement el;
    auto const seg = make_segment(GainCoeffs{0.0F}, GainCoeffs{1.0F}, Engine48k::recip_samples_per_leg_f);
    el.prime_segment(seg);

    constexpr size_t N = Engine48k::samples_per_leg;
    std::array<float, N> input{};
    std::array<float, N> output{};
    for (size_t i = 0; i < N; ++i) {
        input[i] = 1.0F;  // unity input → output equals current amplitude
    }

    drive_cycles(el, input.data(), output.data(), Engine48k::cycles_per_leg);

    // Sample 0 was processed with current = start + delta = 1/256.
    EXPECT_TRUE(std::abs(output[0] - (1.0F / 256.0F)) < 1e-6F);
    // Final sample lands on end (1.0) modulo round-off.
    EXPECT_TRUE(std::abs(output[N - 1] - 1.0F) < 1e-6F);
}

TEST(engine_smoothed_element, snaps_to_segment_end_at_leg_boundary)
{
    TestGainElement el;
    auto const seg = make_segment(GainCoeffs{0.0F}, GainCoeffs{0.5F}, Engine48k::recip_samples_per_leg_f);
    el.prime_segment(seg);

    constexpr size_t N = Engine48k::samples_per_leg;
    std::array<float, N> input{};
    std::array<float, N> output{};
    for (auto& v : input) {
        v = 1.0F;
    }
    drive_cycles(el, input.data(), output.data(), Engine48k::cycles_per_leg);

    // After the leg boundary, the snap discipline forces current to a known
    // value. With no pipe connected, the element holds at active_.end (= 0.5).
    EXPECT_EQ(el.current_coeffs().amplitude, 0.5F);
}

TEST(engine_smoothed_element, freezes_on_pipe_miss)
{
    TestGainElement el;
    LatestPipe<Segment<GainCoeffs>> pipe;
    el.connect(pipe);
    el.prime_segment(make_segment(GainCoeffs{0.0F}, GainCoeffs{0.5F}, Engine48k::recip_samples_per_leg_f));

    constexpr size_t N = Engine48k::samples_per_leg;
    std::array<float, N> input{};
    std::array<float, N> output{};
    for (auto& v : input) {
        v = 1.0F;
    }
    // Run two legs back to back. No new segment was published, so the
    // second leg should hold at 0.5 with zero delta.
    drive_cycles(el, input.data(), output.data(), Engine48k::cycles_per_leg);
    drive_cycles(el, input.data(), output.data(), Engine48k::cycles_per_leg);

    EXPECT_EQ(el.current_coeffs().amplitude, 0.5F);
    // Last output of second leg should still be 0.5 * 1.0 = 0.5.
    EXPECT_EQ(output[N - 1], 0.5F);
}

TEST(engine_smoothed_element, picks_up_published_segment_at_leg_boundary)
{
    TestGainElement el;
    LatestPipe<Segment<GainCoeffs>> pipe;
    el.connect(pipe);
    el.prime_segment(make_segment(GainCoeffs{0.0F}, GainCoeffs{0.5F}, Engine48k::recip_samples_per_leg_f));

    pipe.publish(make_segment(GainCoeffs{0.5F}, GainCoeffs{1.0F}, Engine48k::recip_samples_per_leg_f));

    constexpr size_t N = Engine48k::samples_per_leg;
    std::array<float, N> input{};
    std::array<float, N> output{};
    for (auto& v : input) {
        v = 1.0F;
    }
    drive_cycles(el, input.data(), output.data(), Engine48k::cycles_per_leg);  // leg 1
    drive_cycles(el, input.data(), output.data(), Engine48k::cycles_per_leg);  // leg 2

    // After two legs the new published segment is in effect and we've reached its end.
    EXPECT_TRUE(std::abs(el.current_coeffs().amplitude - 1.0F) < 1e-6F);
}

// ─────────────────────────────────────────────────────────────────────────────
// GainMatrix tests.
// ─────────────────────────────────────────────────────────────────────────────

TEST(engine_gain_matrix, satisfies_is_element)
{
    using Matrix = GainMatrix<2, 2, 48000>;
    static_assert(IsElement<Matrix>);
    EXPECT_EQ(Matrix::in_count, 2U);
    EXPECT_EQ(Matrix::out_count, 2U);
}

TEST(engine_gain_matrix, identity_matrix_passes_inputs_through)
{
    GainMatrix<2, 2, 48000> mix;
    MatrixCoeffs<2, 2, float> coeffs;
    coeffs.set(0, 0, 1.0F);  // in0 → out0
    coeffs.set(1, 1, 1.0F);  // in1 → out1
    mix.prime_segment(Segment<MatrixCoeffs<2, 2, float>>{.start = coeffs, .end = coeffs, .delta = MatrixCoeffs<2, 2, float>{}});

    std::array<float, 8> in0{}, in1{}, out0{}, out1{};
    for (size_t n = 0; n < 8; ++n) {
        in0[n] = static_cast<float>(n);
        in1[n] = static_cast<float>(n) * 10.0F;
    }
    std::array<std::span<float const, 8>, 2> ins{
        std::span<float const, 8>{in0.data(), 8}, std::span<float const, 8>{in1.data(), 8}};
    std::array<std::span<float, 8>, 2> outs{std::span<float, 8>{out0.data(), 8}, std::span<float, 8>{out1.data(), 8}};
    mix.process_cycle(ins, outs);

    for (size_t n = 0; n < 8; ++n) {
        EXPECT_EQ(out0[n], in0[n]);
        EXPECT_EQ(out1[n], in1[n]);
    }
}

TEST(engine_gain_matrix, sums_inputs_per_output)
{
    GainMatrix<2, 1, 48000> mix;
    MatrixCoeffs<2, 1, float> coeffs;
    coeffs.set(0, 0, 0.5F);   // in0 → out0 at 0.5
    coeffs.set(1, 0, 0.25F);  // in1 → out0 at 0.25
    mix.prime_segment(Segment<MatrixCoeffs<2, 1, float>>{.start = coeffs, .end = coeffs, .delta = MatrixCoeffs<2, 1, float>{}});

    std::array<float, 8> in0{}, in1{}, out0{};
    for (size_t n = 0; n < 8; ++n) {
        in0[n] = 4.0F;
        in1[n] = 8.0F;
    }
    std::array<std::span<float const, 8>, 2> ins{
        std::span<float const, 8>{in0.data(), 8}, std::span<float const, 8>{in1.data(), 8}};
    std::array<std::span<float, 8>, 1> outs{std::span<float, 8>{out0.data(), 8}};
    mix.process_cycle(ins, outs);

    // out0 = 4 * 0.5 + 8 * 0.25 = 2 + 2 = 4
    for (auto v : out0) {
        EXPECT_TRUE(std::abs(v - 4.0F) < 1e-6F);
    }
}

TEST(engine_gain_matrix, ramps_all_crosspoints_via_whole_matrix_segment)
{
    GainMatrix<2, 2, 48000> mix;
    MatrixCoeffs<2, 2, float> start{};  // all zeros
    MatrixCoeffs<2, 2, float> end{};
    end.set(0, 0, 1.0F);
    end.set(0, 1, 0.5F);
    end.set(1, 0, 0.25F);
    end.set(1, 1, 0.125F);
    auto const seg = make_segment(start, end, Engine48k::recip_samples_per_leg_f);
    mix.prime_segment(seg);

    std::array<float, 8> in0{}, in1{}, out0{}, out1{};
    for (auto& v : in0) {
        v = 1.0F;
    }
    for (auto& v : in1) {
        v = 1.0F;
    }
    std::array<std::span<float const, 8>, 2> ins{
        std::span<float const, 8>{in0.data(), 8}, std::span<float const, 8>{in1.data(), 8}};
    std::array<std::span<float, 8>, 2> outs{std::span<float, 8>{out0.data(), 8}, std::span<float, 8>{out1.data(), 8}};

    // Drive a full leg: each crosspoint should ramp from 0 to its target.
    for (size_t cycle = 0; cycle < Engine48k::cycles_per_leg; ++cycle) {
        mix.process_cycle(ins, outs);
    }
    // After the full leg, internal coefficients should be ~end values.
    auto const& current = mix.current_coeffs();
    EXPECT_TRUE(std::abs(current.at(0, 0) - 1.0F) < 1e-5F);
    EXPECT_TRUE(std::abs(current.at(0, 1) - 0.5F) < 1e-5F);
    EXPECT_TRUE(std::abs(current.at(1, 0) - 0.25F) < 1e-5F);
    EXPECT_TRUE(std::abs(current.at(1, 1) - 0.125F) < 1e-5F);
}

TEST(engine_gain_matrix, freezes_on_pipe_miss)
{
    GainMatrix<2, 1, 48000> mix;
    LatestPipe<Segment<MatrixCoeffs<2, 1, float>>> pipe;
    mix.connect(pipe);

    MatrixCoeffs<2, 1, float> coeffs;
    coeffs.set(0, 0, 1.0F);
    coeffs.set(1, 0, 1.0F);
    mix.prime_segment(Segment<MatrixCoeffs<2, 1, float>>{.start = coeffs, .end = coeffs, .delta = MatrixCoeffs<2, 1, float>{}});

    std::array<float, 8> in0{}, in1{}, out0{};
    for (auto& v : in0) {
        v = 1.0F;
    }
    for (auto& v : in1) {
        v = 1.0F;
    }
    std::array<std::span<float const, 8>, 2> ins{
        std::span<float const, 8>{in0.data(), 8}, std::span<float const, 8>{in1.data(), 8}};
    std::array<std::span<float, 8>, 1> outs{std::span<float, 8>{out0.data(), 8}};

    // Two legs back to back, no publish between them.
    for (size_t i = 0; i < Engine48k::cycles_per_leg * 2; ++i) {
        mix.process_cycle(ins, outs);
    }
    // Coefficients should still be at the primed endpoint (1.0 each).
    EXPECT_EQ(mix.current_coeffs().at(0, 0), 1.0F);
    EXPECT_EQ(mix.current_coeffs().at(1, 0), 1.0F);
    // Output is in0*1 + in1*1 = 2.0.
    EXPECT_EQ(out0[0], 2.0F);
}

// ─────────────────────────────────────────────────────────────────────────────
// BiquadApply / BiquadElement (Tier 0) tests.
// ─────────────────────────────────────────────────────────────────────────────

TEST(engine_biquad_element, biquad_apply_matches_dsp_complex_biquad)
{
    // Same coeffs + same input + zero initial state should produce identical
    // output between the engine's BiquadApply and the dsp module's
    // ComplexBiQuad reference implementation.
    statusbar::dsp::ComplexBiQuad<float> ref;
    statusbar::dsp::FilterParams<double> fp{};
    fp.sample_rate_recip = 1.0 / 48000.0;
    fp.frequency = 1500.0;
    fp.q = 0.707;
    fp.gain_db = 0.0;
    ref.coeffs.calculate_lowpass(fp);

    BiquadComplexCoeffs<float> coeffs{
        .stage1 = ref.coeffs.stage1,
        .stage2 = ref.coeffs.stage2,
    };
    BiquadApply<float> ours{};

    for (int i = 0; i < 200; ++i) {
        float const input = std::sin(static_cast<float>(i) * 0.1F);
        float const ref_out = ref(input);
        float const our_out = ours(coeffs, input);
        EXPECT_TRUE(std::abs(ref_out - our_out) < 1e-6F);
    }
}

TEST(engine_biquad_element, biquad_element_satisfies_is_element)
{
    using BE = BiquadElement<48000>;
    static_assert(IsElement<BE>);
    EXPECT_EQ(BE::level, 0U);
    EXPECT_EQ(BE::data_in::extent, 8U);
}

TEST(engine_biquad_element, simd_instantiation_satisfies_concepts)
{
    // SIMD instantiation: T = simd_float32x4 means one element processes
    // 4 independent channels in parallel. Verify the templated machinery
    // satisfies the engine's concepts at compile time.
    using SimdT = statusbar::dsp::simd_float32x4;
    static_assert(SegmentableCoeffs<BiquadComplexCoeffs<SimdT>>);
    using BE_simd = BiquadElement<48000, SimdT>;
    static_assert(IsElement<BE_simd>);
    EXPECT_EQ(BE_simd::level, 0U);
    EXPECT_EQ(BE_simd::data_in::extent, 8U);
}

TEST(engine_biquad_element, simd_apply_runs_lane_wise)
{
    // BiquadApply<simd_float32x4> with the same coefficients across all
    // lanes, fed the same input across all lanes, should produce
    // identical output across all lanes — confirming the math is
    // genuinely lane-wise and not somehow contaminating channels.
    using SimdT = statusbar::dsp::simd_float32x4;

    // Compute a single-channel reference filter response.
    statusbar::dsp::ComplexBiQuad<float> ref;
    statusbar::dsp::FilterParams<double> fp{};
    fp.sample_rate_recip = 1.0 / 48000.0;
    fp.frequency = 1500.0;
    fp.q = 0.707;
    ref.coeffs.calculate_lowpass(fp);

    BiquadComplexCoeffs<SimdT> coeffs{};
    coeffs.stage1.a0_re = SimdT::splat(ref.coeffs.stage1.a0_re);
    coeffs.stage1.a0_im = SimdT::splat(ref.coeffs.stage1.a0_im);
    coeffs.stage1.a1_re = SimdT::splat(ref.coeffs.stage1.a1_re);
    coeffs.stage1.a1_im = SimdT::splat(ref.coeffs.stage1.a1_im);
    coeffs.stage1.b1_re = SimdT::splat(ref.coeffs.stage1.b1_re);
    coeffs.stage1.b1_im = SimdT::splat(ref.coeffs.stage1.b1_im);
    coeffs.stage2.a0_re = SimdT::splat(ref.coeffs.stage2.a0_re);
    coeffs.stage2.a0_im = SimdT::splat(ref.coeffs.stage2.a0_im);
    coeffs.stage2.a1_re = SimdT::splat(ref.coeffs.stage2.a1_re);
    coeffs.stage2.a1_im = SimdT::splat(ref.coeffs.stage2.a1_im);
    coeffs.stage2.b1_re = SimdT::splat(ref.coeffs.stage2.b1_re);
    coeffs.stage2.b1_im = SimdT::splat(ref.coeffs.stage2.b1_im);

    BiquadApply<SimdT> simd_apply;
    for (int i = 0; i < 100; ++i) {
        float const input_scalar = std::sin(static_cast<float>(i) * 0.1F);
        float const ref_out = ref(input_scalar);
        SimdT const input_simd = SimdT::splat(input_scalar);
        SimdT const out_simd = simd_apply(coeffs, input_simd);
        // All four lanes should match the scalar reference.
        for (size_t lane = 0; lane < 4; ++lane) {
            EXPECT_TRUE(std::abs(out_simd[lane] - ref_out) < 1e-5F);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Bode plot / z-domain evaluation tests.
// ─────────────────────────────────────────────────────────────────────────────

TEST(engine_bode, identity_biquad_has_unity_magnitude_at_all_frequencies)
{
    // Identity biquad: a0 = 1, all other coeffs zero ⇒ H(z) = 1 for any z.
    auto const bypass = BiquadComplexCoeffs<float>::identity();
    EXPECT_EQ(bypass.stage1.a0_re, 1.0F);
    EXPECT_EQ(bypass.stage2.a0_re, 1.0F);
    constexpr double sr_recip = 1.0 / 48000.0;
    for (double f : {20.0, 100.0, 1000.0, 5000.0, 20000.0}) {
        auto const bp = bode_point_at_frequency(bypass, f, sr_recip);
        EXPECT_TRUE(std::abs(bp.magnitude - 1.0) < 1e-9);
    }
}

TEST(engine_bode, lowpass_at_cutoff_is_minus_3_db)
{
    // Standard 2nd-order LP at 1 kHz, Q=0.707 should be ~-3 dB at 1 kHz.
    statusbar::dsp::BiquadDesignParams const p{
        .type = statusbar::dsp::BiquadFilterType::Lowpass,
        .frequency_hz = 1000.0,
        .q = 0.707,
    };
    constexpr double sr_recip = 1.0 / 48000.0;
    auto const pair = statusbar::dsp::design_biquad_pole_zero(p, sr_recip);
    BiquadComplexCoeffs<float> coeffs{.stage1 = pair.first, .stage2 = pair.second};

    auto const bp = bode_point_at_frequency(coeffs, 1000.0, sr_recip);
    double const db = 20.0 * std::log10(bp.magnitude);
    // Within 0.5 dB of -3 dB (depends slightly on exact Q convention).
    EXPECT_TRUE(std::abs(db - (-3.0)) < 0.5);
}

TEST(engine_bode, lowpass_response_drops_above_cutoff)
{
    statusbar::dsp::BiquadDesignParams const p{
        .type = statusbar::dsp::BiquadFilterType::Lowpass,
        .frequency_hz = 1000.0,
        .q = 0.707,
    };
    constexpr double sr_recip = 1.0 / 48000.0;
    auto const pair = statusbar::dsp::design_biquad_pole_zero(p, sr_recip);
    BiquadComplexCoeffs<float> coeffs{.stage1 = pair.first, .stage2 = pair.second};

    auto const bp_low = bode_point_at_frequency(coeffs, 100.0, sr_recip);
    auto const bp_high = bode_point_at_frequency(coeffs, 10000.0, sr_recip);
    // Low frequency well below cutoff: ~0 dB. High well above: heavily
    // attenuated.
    EXPECT_TRUE(bp_low.magnitude > 0.95);
    EXPECT_TRUE(bp_high.magnitude < 0.05);
}

TEST(engine_bode, chain_response_equals_product_of_individual_responses)
{
    constexpr double sr_recip = 1.0 / 48000.0;
    auto const lp = statusbar::dsp::design_biquad_pole_zero(
        {.type = statusbar::dsp::BiquadFilterType::Lowpass, .frequency_hz = 4000.0, .q = 0.707}, sr_recip);
    auto const hp = statusbar::dsp::design_biquad_pole_zero(
        {.type = statusbar::dsp::BiquadFilterType::Highpass, .frequency_hz = 200.0, .q = 0.707}, sr_recip);

    BiquadChainCoeffs<2, float> chain{};
    chain.stages[0] = BiquadComplexCoeffs<float>{.stage1 = lp.first, .stage2 = lp.second};
    chain.stages[1] = BiquadComplexCoeffs<float>{.stage1 = hp.first, .stage2 = hp.second};

    constexpr double test_freq = 1000.0;  // mid-band
    auto const z_inv = z_inv_at_frequency(test_freq, sr_recip);

    auto const h_chain = evaluate_z_domain(chain, z_inv);
    auto const h_lp = evaluate_z_domain(chain.stages[0], z_inv);
    auto const h_hp = evaluate_z_domain(chain.stages[1], z_inv);
    auto const h_prod = h_lp * h_hp;
    EXPECT_TRUE(std::abs(h_chain - h_prod) < 1e-9);
}

TEST(engine_bode, simd_per_channel_independent_responses)
{
    // Pack two different filter designs into lanes 0 and 1 of a SIMD
    // BiquadComplexCoeffs<simd_float32x4>. Bode point at the same
    // frequency, queried per channel, returns the per-lane responses.
    using SimdT = statusbar::dsp::simd_float32x4;
    constexpr double sr_recip = 1.0 / 48000.0;
    auto const lp_500 = statusbar::dsp::design_biquad_pole_zero(
        {.type = statusbar::dsp::BiquadFilterType::Lowpass, .frequency_hz = 500.0, .q = 0.707}, sr_recip);
    auto const lp_5000 = statusbar::dsp::design_biquad_pole_zero(
        {.type = statusbar::dsp::BiquadFilterType::Lowpass, .frequency_hz = 5000.0, .q = 0.707}, sr_recip);

    BiquadComplexCoeffs<SimdT> coeffs{};
    using statusbar::dsp::set_flattened_item;
    set_flattened_item(coeffs.stage1.a0_re, lp_500.first.a0_re, 0);
    set_flattened_item(coeffs.stage1.a0_im, lp_500.first.a0_im, 0);
    set_flattened_item(coeffs.stage1.a1_re, lp_500.first.a1_re, 0);
    set_flattened_item(coeffs.stage1.a1_im, lp_500.first.a1_im, 0);
    set_flattened_item(coeffs.stage1.b1_re, lp_500.first.b1_re, 0);
    set_flattened_item(coeffs.stage1.b1_im, lp_500.first.b1_im, 0);
    set_flattened_item(coeffs.stage2.a0_re, lp_500.second.a0_re, 0);
    set_flattened_item(coeffs.stage2.a0_im, lp_500.second.a0_im, 0);
    set_flattened_item(coeffs.stage2.a1_re, lp_500.second.a1_re, 0);
    set_flattened_item(coeffs.stage2.a1_im, lp_500.second.a1_im, 0);
    set_flattened_item(coeffs.stage2.b1_re, lp_500.second.b1_re, 0);
    set_flattened_item(coeffs.stage2.b1_im, lp_500.second.b1_im, 0);

    set_flattened_item(coeffs.stage1.a0_re, lp_5000.first.a0_re, 1);
    set_flattened_item(coeffs.stage1.a0_im, lp_5000.first.a0_im, 1);
    set_flattened_item(coeffs.stage1.a1_re, lp_5000.first.a1_re, 1);
    set_flattened_item(coeffs.stage1.a1_im, lp_5000.first.a1_im, 1);
    set_flattened_item(coeffs.stage1.b1_re, lp_5000.first.b1_re, 1);
    set_flattened_item(coeffs.stage1.b1_im, lp_5000.first.b1_im, 1);
    set_flattened_item(coeffs.stage2.a0_re, lp_5000.second.a0_re, 1);
    set_flattened_item(coeffs.stage2.a0_im, lp_5000.second.a0_im, 1);
    set_flattened_item(coeffs.stage2.a1_re, lp_5000.second.a1_re, 1);
    set_flattened_item(coeffs.stage2.a1_im, lp_5000.second.a1_im, 1);
    set_flattened_item(coeffs.stage2.b1_re, lp_5000.second.b1_re, 1);
    set_flattened_item(coeffs.stage2.b1_im, lp_5000.second.b1_im, 1);

    // At 2 kHz: lane 0 (LP500) is heavily attenuated; lane 1 (LP5000) is
    // still in passband.
    constexpr double test_freq = 2000.0;
    auto const bp0 = bode_point_at_frequency(coeffs, test_freq, sr_recip, 0);
    auto const bp1 = bode_point_at_frequency(coeffs, test_freq, sr_recip, 1);
    EXPECT_TRUE(bp0.magnitude < 0.1);  // lane 0 (LP500) attenuates 2 kHz
    EXPECT_TRUE(bp1.magnitude > 0.7);  // lane 1 (LP5000) passes 2 kHz
}

// ─────────────────────────────────────────────────────────────────────────────
// BiquadChain<N, SR, T> (Tier 0) tests.
// ─────────────────────────────────────────────────────────────────────────────

TEST(engine_biquad_chain, coeffs_satisfy_segmentable_coeffs)
{
    static_assert(SegmentableCoeffs<BiquadChainCoeffs<1, float>>);
    static_assert(SegmentableCoeffs<BiquadChainCoeffs<3, float>>);
    static_assert(SegmentableCoeffs<BiquadChainCoeffs<3, statusbar::dsp::simd_float32x4>>);

    BiquadChainCoeffs<2, float> a{};
    BiquadChainCoeffs<2, float> b{};
    a.stages[0].stage1.a0_re = 1.0F;
    b.stages[0].stage1.a0_re = 2.0F;
    a.stages[1].stage2.b1_re = 0.5F;

    auto sum = a + b;
    EXPECT_EQ(sum.stages[0].stage1.a0_re, 3.0F);
    EXPECT_EQ(sum.stages[1].stage2.b1_re, 0.5F);

    auto scaled = a * 4.0F;
    EXPECT_EQ(scaled.stages[1].stage2.b1_re, 2.0F);
}

TEST(engine_biquad_chain, element_satisfies_is_element)
{
    using Chain = BiquadChain<3, 48000>;
    static_assert(IsElement<Chain>);
    EXPECT_EQ(Chain::level, 0U);
    EXPECT_EQ(Chain::data_in::extent, 8U);
}

TEST(engine_biquad_chain, two_stage_chain_matches_cascaded_individual_biquads)
{
    // Drive the same input through (a) BiquadChain<2> with two filter
    // stages and (b) two individual BiquadElement<48000> wired in
    // series. Outputs should match bit-for-bit since the math is
    // structurally identical.
    statusbar::dsp::FilterParams<double> fp1{};
    fp1.sample_rate_recip = 1.0 / 48000.0;
    fp1.frequency = 1500.0;
    fp1.q = 0.707;
    statusbar::dsp::FilterParams<double> fp2{};
    fp2.sample_rate_recip = 1.0 / 48000.0;
    fp2.frequency = 5000.0;
    fp2.q = 1.2;

    statusbar::dsp::ComplexBiQuad<float> ref1;
    statusbar::dsp::ComplexBiQuad<float> ref2;
    ref1.coeffs.calculate_lowpass(fp1);
    ref2.coeffs.calculate_highpass(fp2);

    BiquadChainCoeffs<2, float> chain_coeffs{};
    chain_coeffs.stages[0].stage1 = ref1.coeffs.stage1;
    chain_coeffs.stages[0].stage2 = ref1.coeffs.stage2;
    chain_coeffs.stages[1].stage1 = ref2.coeffs.stage1;
    chain_coeffs.stages[1].stage2 = ref2.coeffs.stage2;

    BiquadChainApply<2, float> chain_apply{};

    BiquadComplexCoeffs<float> bc1{.stage1 = ref1.coeffs.stage1, .stage2 = ref1.coeffs.stage2};
    BiquadComplexCoeffs<float> bc2{.stage1 = ref2.coeffs.stage1, .stage2 = ref2.coeffs.stage2};
    BiquadApply<float> single1{};
    BiquadApply<float> single2{};

    for (int i = 0; i < 200; ++i) {
        float const input = std::sin(static_cast<float>(i) * 0.1F);
        float const chain_out = chain_apply(chain_coeffs, input);
        float const single_out = single2(bc2, single1(bc1, input));
        EXPECT_TRUE(std::abs(chain_out - single_out) < 1e-6F);
    }
}

TEST(engine_biquad_chain, simd_instantiation_runs_lane_wise)
{
    using SimdT = statusbar::dsp::simd_float32x4;
    using Chain = BiquadChain<3, 48000, SimdT>;
    static_assert(IsElement<Chain>);
    EXPECT_EQ(Chain::data_in::extent, 8U);
    // Construct the apply functor and ensure it default-initializes.
    BiquadChainApply<3, SimdT> apply{};
    BiquadChainCoeffs<3, SimdT> c{};
    SimdT in{1.0F, 2.0F, 3.0F, 4.0F};
    auto out = apply(c, in);
    // All-zero coeffs through the chain produce all-zero output.
    EXPECT_EQ(out[0], 0.0F);
    EXPECT_EQ(out[1], 0.0F);
    EXPECT_EQ(out[2], 0.0F);
    EXPECT_EQ(out[3], 0.0F);
}

// ─────────────────────────────────────────────────────────────────────────────
// BiquadDesigner (Tier 1) tests.
// ─────────────────────────────────────────────────────────────────────────────

TEST(engine_biquad_coeffs, satisfies_segmentable_coeffs)
{
    static_assert(SegmentableCoeffs<BiquadComplexCoeffs<float>>);
    BiquadComplexCoeffs<float> a{}, b{};
    a.stage1.a0_re = 1.0F;
    b.stage1.a0_re = 2.0F;
    auto sum = a + b;
    EXPECT_EQ(sum.stage1.a0_re, 3.0F);
    auto diff = b - a;
    EXPECT_EQ(diff.stage1.a0_re, 1.0F);
    auto scaled = a * 0.5F;
    EXPECT_EQ(scaled.stage1.a0_re, 0.5F);
}

TEST(engine_biquad_designer, set_initial_publishes_hold_segment_on_first_step)
{
    BiquadDesigner<float> designer{1.0 / 48000.0};
    LatestPipe<Segment<BiquadComplexCoeffs<float>>> out_pipe;
    designer.connect_output(out_pipe);

    designer.set_initial(statusbar::dsp::BiquadDesignParams{
        .type = statusbar::dsp::BiquadFilterType::Lowpass, .frequency_hz = 1000.0, .q = 0.707, .gain_db = 0.0});

    EXPECT_EQ(designer.legs_remaining(0), 0U);
    designer.step(Engine48k::recip_samples_per_leg_f);

    auto seg = out_pipe.try_consume();
    EXPECT_TRUE(seg.has_value());
    // First step with no incoming target = hold at the initial design.
    EXPECT_EQ(seg->start.stage1.a0_re, seg->end.stage1.a0_re);
    EXPECT_EQ(seg->delta.stage1.a0_re, 0.0F);
}

TEST(engine_biquad_designer, new_target_triggers_three_leg_morph)
{
    BiquadDesigner<float> designer{1.0 / 48000.0};
    LatestPipe<statusbar::dsp::BiquadDesignParams> in_pipe;
    LatestPipe<Segment<BiquadComplexCoeffs<float>>> out_pipe;
    designer.connect_input(0, in_pipe);
    designer.connect_output(out_pipe);

    designer.set_initial(
        statusbar::dsp::BiquadDesignParams{.type = statusbar::dsp::BiquadFilterType::Lowpass, .frequency_hz = 1000.0});

    // Target: same type but higher frequency.
    in_pipe.publish(statusbar::dsp::BiquadDesignParams{.type = statusbar::dsp::BiquadFilterType::Lowpass, .frequency_hz = 4000.0});

    designer.step(Engine48k::recip_samples_per_leg_f);
    EXPECT_EQ(designer.legs_remaining(0), 2U);
    EXPECT_TRUE(std::abs(designer.current_design(0).frequency_hz - 2000.0) < 1e-9);

    designer.step(Engine48k::recip_samples_per_leg_f);
    EXPECT_EQ(designer.legs_remaining(0), 1U);
    EXPECT_TRUE(std::abs(designer.current_design(0).frequency_hz - 3000.0) < 1e-9);

    designer.step(Engine48k::recip_samples_per_leg_f);
    EXPECT_EQ(designer.legs_remaining(0), 0U);
    EXPECT_TRUE(std::abs(designer.current_design(0).frequency_hz - 4000.0) < 1e-9);

    designer.step(Engine48k::recip_samples_per_leg_f);
    EXPECT_EQ(designer.legs_remaining(0), 0U);  // holding now
    EXPECT_TRUE(std::abs(designer.current_design(0).frequency_hz - 4000.0) < 1e-9);
}

TEST(engine_biquad_designer, type_change_snaps_in_one_leg)
{
    BiquadDesigner<float> designer{1.0 / 48000.0};
    LatestPipe<statusbar::dsp::BiquadDesignParams> in_pipe;
    LatestPipe<Segment<BiquadComplexCoeffs<float>>> out_pipe;
    designer.connect_input(0, in_pipe);
    designer.connect_output(out_pipe);

    designer.set_initial(
        statusbar::dsp::BiquadDesignParams{.type = statusbar::dsp::BiquadFilterType::Lowpass, .frequency_hz = 1000.0});

    in_pipe.publish(statusbar::dsp::BiquadDesignParams{.type = statusbar::dsp::BiquadFilterType::Highpass, .frequency_hz = 1000.0});

    designer.step(Engine48k::recip_samples_per_leg_f);
    // Type change should snap (no morph): legs_remaining stays 0, current = target.
    EXPECT_EQ(designer.legs_remaining(0), 0U);
    EXPECT_EQ(designer.current_design(0).type, statusbar::dsp::BiquadFilterType::Highpass);
}

TEST(engine_biquad_designer, segments_chain_together_at_endpoints)
{
    // Tier 0 invariant: each new segment's start equals the previous
    // segment's end. Verify the designer maintains this across a morph.
    BiquadDesigner<float> designer{1.0 / 48000.0};
    LatestPipe<statusbar::dsp::BiquadDesignParams> in_pipe;
    LatestPipe<Segment<BiquadComplexCoeffs<float>>> out_pipe;
    designer.connect_input(0, in_pipe);
    designer.connect_output(out_pipe);

    designer.set_initial(
        statusbar::dsp::BiquadDesignParams{.type = statusbar::dsp::BiquadFilterType::Lowpass, .frequency_hz = 500.0});

    in_pipe.publish(statusbar::dsp::BiquadDesignParams{.type = statusbar::dsp::BiquadFilterType::Lowpass, .frequency_hz = 8000.0});

    BiquadComplexCoeffs prev_end{};
    bool first = true;
    for (int i = 0; i < 3; ++i) {
        designer.step(Engine48k::recip_samples_per_leg_f);
        auto seg = out_pipe.try_consume();
        EXPECT_TRUE(seg.has_value());
        if (!first) {
            // start of this leg must equal end of previous leg, exactly
            EXPECT_EQ(seg->start.stage1.a0_re, prev_end.stage1.a0_re);
            EXPECT_EQ(seg->start.stage1.b1_re, prev_end.stage1.b1_re);
            EXPECT_EQ(seg->start.stage2.b1_im, prev_end.stage2.b1_im);
        }
        prev_end = seg->end;
        first = false;
    }
}

TEST(engine_biquad_coeffs, nested_simd_arithmetic_works_via_recursive_mul)
{
    // Nested type: SIMDVec<simd_float32x4, 2> = 8 flattened lanes.
    // The Coeffs operator*(C, float) must recurse through the nesting
    // via dsp::mul to broadcast the scalar to every leaf.
    using NestedT = statusbar::dsp::SIMDVec<statusbar::dsp::simd_float32x4, 2>;
    static_assert(SegmentableCoeffs<BiquadComplexCoeffs<NestedT>>);

    BiquadComplexCoeffs<NestedT> c{};
    // Set lane 5 (outer=1, inner=1) to a known value via flattened addressing.
    statusbar::dsp::set_flattened_item(c.stage1.a0_re, 2.5F, 5);
    EXPECT_EQ(statusbar::dsp::get_flattened_item(c.stage1.a0_re, 5), 2.5F);

    // Multiply by 4.0F — every lane should scale, including the nested ones.
    auto const scaled = c * 4.0F;
    EXPECT_EQ(statusbar::dsp::get_flattened_item(scaled.stage1.a0_re, 5), 10.0F);
    // Other lanes (which were 0) stay at 0.
    EXPECT_EQ(statusbar::dsp::get_flattened_item(scaled.stage1.a0_re, 0), 0.0F);
    EXPECT_EQ(statusbar::dsp::get_flattened_item(scaled.stage1.a0_re, 7), 0.0F);
}

TEST(engine_biquad_designer, simd_per_channel_independent_designs)
{
    // BiquadDesigner<simd_float32x4>: 4 independent channels in one designer.
    // Each channel can have its own filter design and morph independently.
    // Verify that a per-channel set_initial places different coefficients in
    // different lanes of the SIMD-packed BiquadComplexCoeffs<simd_float32x4>.
    using SimdT = statusbar::dsp::simd_float32x4;
    BiquadDesigner<SimdT> designer{1.0 / 48000.0};

    // Lane 0: LP 500 Hz; lane 1: LP 2000 Hz; lane 2: LP 8000 Hz; lane 3: bypass-ish (HP 5 Hz).
    designer.set_initial(
        0, statusbar::dsp::BiquadDesignParams{.type = statusbar::dsp::BiquadFilterType::Lowpass, .frequency_hz = 500.0});
    designer.set_initial(
        1, statusbar::dsp::BiquadDesignParams{.type = statusbar::dsp::BiquadFilterType::Lowpass, .frequency_hz = 2000.0});
    designer.set_initial(
        2, statusbar::dsp::BiquadDesignParams{.type = statusbar::dsp::BiquadFilterType::Lowpass, .frequency_hz = 8000.0});
    designer.set_initial(
        3, statusbar::dsp::BiquadDesignParams{.type = statusbar::dsp::BiquadFilterType::Highpass, .frequency_hz = 5.0});

    // Verify per-channel design state stays independent.
    EXPECT_EQ(designer.current_design(0).frequency_hz, 500.0);
    EXPECT_EQ(designer.current_design(1).frequency_hz, 2000.0);
    EXPECT_EQ(designer.current_design(2).frequency_hz, 8000.0);
    EXPECT_EQ(designer.current_design(3).type, statusbar::dsp::BiquadFilterType::Highpass);

    // Verify the SIMD endpoint has different coefficients per lane —
    // pull a known scalar field (a0_re of stage1) out of each lane.
    auto const& ep = designer.last_endpoint();
    using statusbar::dsp::get_flattened_item;
    float const a0_re_lane0 = get_flattened_item(ep.stage1.a0_re, 0);
    float const a0_re_lane1 = get_flattened_item(ep.stage1.a0_re, 1);
    float const a0_re_lane2 = get_flattened_item(ep.stage1.a0_re, 2);
    float const a0_re_lane3 = get_flattened_item(ep.stage1.a0_re, 3);
    // Different filters → different coefficients per lane.
    EXPECT_NE(a0_re_lane0, a0_re_lane1);
    EXPECT_NE(a0_re_lane1, a0_re_lane2);
    EXPECT_NE(a0_re_lane2, a0_re_lane3);
}

TEST(engine_biquad_designer, simd_independent_morphs_per_channel)
{
    // Push a new target on lane 0, leave lane 1 alone. After step():
    //   lane 0 should be morphing (legs_remaining == 2).
    //   lane 1 should be holding (legs_remaining == 0).
    using SimdT = statusbar::dsp::simd_float32x4;
    BiquadDesigner<SimdT> designer{1.0 / 48000.0};
    LatestPipe<statusbar::dsp::BiquadDesignParams> lane0_pipe;
    designer.connect_input(0, lane0_pipe);

    designer.set_initial(
        statusbar::dsp::BiquadDesignParams{.type = statusbar::dsp::BiquadFilterType::Lowpass, .frequency_hz = 1000.0});

    lane0_pipe.publish(
        statusbar::dsp::BiquadDesignParams{.type = statusbar::dsp::BiquadFilterType::Lowpass, .frequency_hz = 4000.0});

    designer.step(Engine48k::recip_samples_per_leg_f);

    EXPECT_EQ(designer.legs_remaining(0), 2U);  // morphing
    EXPECT_EQ(designer.legs_remaining(1), 0U);  // holding
    EXPECT_EQ(designer.legs_remaining(2), 0U);
    EXPECT_EQ(designer.legs_remaining(3), 0U);

    // Lane 0's current design should be at the 1/3 morph point.
    EXPECT_TRUE(std::abs(designer.current_design(0).frequency_hz - 2000.0) < 1e-9);
    // Lane 1 is still at initial.
    EXPECT_EQ(designer.current_design(1).frequency_hz, 1000.0);
}

TEST(engine_biquad_designer, no_input_pipe_holds_at_initial)
{
    BiquadDesigner<float> designer{1.0 / 48000.0};
    LatestPipe<Segment<BiquadComplexCoeffs<float>>> out_pipe;
    designer.connect_output(out_pipe);

    designer.set_initial(
        statusbar::dsp::BiquadDesignParams{.type = statusbar::dsp::BiquadFilterType::Lowpass, .frequency_hz = 1000.0});

    auto const initial_endpoint = designer.last_endpoint();

    for (int i = 0; i < 5; ++i) {
        designer.step(Engine48k::recip_samples_per_leg_f);
    }

    // After many steps with no input, current is still the initial design.
    EXPECT_EQ(designer.last_endpoint().stage1.a0_re, initial_endpoint.stage1.a0_re);
    EXPECT_EQ(designer.last_endpoint().stage2.b1_im, initial_endpoint.stage2.b1_im);
}

// ─────────────────────────────────────────────────────────────────────────────
// GainElement (Tier 0) tests.
// ─────────────────────────────────────────────────────────────────────────────

TEST(engine_gain_coeffs, satisfies_segmentable_coeffs)
{
    static_assert(SegmentableCoeffs<GainAmplitudeCoeffs<float>>);
    GainAmplitudeCoeffs<float> a{0.25F};
    GainAmplitudeCoeffs<float> b{0.75F};
    EXPECT_EQ((a + b).amplitude, 1.0F);
    EXPECT_EQ((b - a).amplitude, 0.5F);
    EXPECT_EQ((a * 2.0F).amplitude, 0.5F);
}

TEST(engine_gain_element, gain_apply_multiplies)
{
    statusbar::engine::GainApply<float> ga;
    GainAmplitudeCoeffs<float> c{0.5F};
    EXPECT_EQ(ga(c, 1.0F), 0.5F);
    EXPECT_EQ(ga(c, -2.0F), -1.0F);
    EXPECT_EQ(ga({0.0F}, 100.0F), 0.0F);
}

TEST(engine_gain_element, gain_element_satisfies_is_element)
{
    using GE = GainElement<48000>;
    static_assert(IsElement<GE>);
    EXPECT_EQ(GE::level, 0U);
    EXPECT_EQ(GE::data_in::extent, 8U);
}

TEST(engine_gain_element, simd_instantiation_runs_lane_wise)
{
    // SIMD gain: 4 independent channels with potentially different
    // amplitudes per lane. Verify lane-wise multiply works.
    using SimdT = statusbar::dsp::simd_float32x4;
    static_assert(SegmentableCoeffs<GainAmplitudeCoeffs<SimdT>>);
    static_assert(IsElement<GainElement<48000, SimdT>>);

    statusbar::engine::GainApply<SimdT> ga;
    GainAmplitudeCoeffs<SimdT> c{SimdT{0.5F, 1.0F, 2.0F, 0.0F}};
    SimdT const input{1.0F, 1.0F, 1.0F, 1.0F};
    SimdT const out = ga(c, input);
    EXPECT_EQ(out[0], 0.5F);
    EXPECT_EQ(out[1], 1.0F);
    EXPECT_EQ(out[2], 2.0F);
    EXPECT_EQ(out[3], 0.0F);
}

// ─────────────────────────────────────────────────────────────────────────────
// GainDesigner (Tier 1) tests.
// ─────────────────────────────────────────────────────────────────────────────

TEST(engine_gain_designer, set_initial_publishes_hold_segment_on_first_step)
{
    GainDesigner<float> designer;
    LatestPipe<Segment<GainAmplitudeCoeffs<float>>> out_pipe;
    designer.connect_output(out_pipe);

    designer.set_initial(0.5F);
    designer.step(Engine48k::recip_samples_per_leg_f);

    auto seg = out_pipe.try_consume();
    EXPECT_TRUE(seg.has_value());
    EXPECT_EQ(seg->start.amplitude, 0.5F);
    EXPECT_EQ(seg->end.amplitude, 0.5F);
    EXPECT_EQ(seg->delta.amplitude, 0.0F);
}

TEST(engine_gain_designer, new_target_produces_one_leg_ramp)
{
    GainDesigner<float> designer;
    LatestPipe<float> in_pipe;
    LatestPipe<Segment<GainAmplitudeCoeffs<float>>> out_pipe;
    designer.connect_input(0, in_pipe);
    designer.connect_output(out_pipe);

    designer.set_initial(0.0F);
    in_pipe.publish(1.0F);

    designer.step(Engine48k::recip_samples_per_leg_f);

    auto seg = out_pipe.try_consume();
    EXPECT_TRUE(seg.has_value());
    EXPECT_EQ(seg->start.amplitude, 0.0F);
    EXPECT_EQ(seg->end.amplitude, 1.0F);
    EXPECT_EQ(seg->delta.amplitude, 1.0F / 256.0F);
    EXPECT_EQ(designer.target_amplitude(0), 1.0F);
}

TEST(engine_gain_designer, segments_chain_at_endpoints)
{
    GainDesigner<float> designer;
    LatestPipe<float> in_pipe;
    LatestPipe<Segment<GainAmplitudeCoeffs<float>>> out_pipe;
    designer.connect_input(0, in_pipe);
    designer.connect_output(out_pipe);

    designer.set_initial(0.0F);

    in_pipe.publish(0.4F);
    designer.step(Engine48k::recip_samples_per_leg_f);
    auto seg1 = out_pipe.try_consume();
    EXPECT_TRUE(seg1.has_value());

    in_pipe.publish(0.7F);
    designer.step(Engine48k::recip_samples_per_leg_f);
    auto seg2 = out_pipe.try_consume();
    EXPECT_TRUE(seg2.has_value());

    // Chain invariant: each new segment's start = previous segment's end.
    EXPECT_EQ(seg2->start.amplitude, seg1->end.amplitude);
    EXPECT_EQ(seg2->end.amplitude, 0.7F);
}

TEST(engine_gain_designer, simd_per_channel_independent_amplitudes)
{
    // GainDesigner<simd_float32x4>: 4 independent gain channels.
    using SimdT = statusbar::dsp::simd_float32x4;
    GainDesigner<SimdT> designer;

    designer.set_initial(0, 0.25F);
    designer.set_initial(1, 0.50F);
    designer.set_initial(2, 0.75F);
    designer.set_initial(3, 1.00F);

    auto const& ep = designer.last_endpoint();
    using statusbar::dsp::get_flattened_item;
    EXPECT_EQ(get_flattened_item(ep.amplitude, 0), 0.25F);
    EXPECT_EQ(get_flattened_item(ep.amplitude, 1), 0.50F);
    EXPECT_EQ(get_flattened_item(ep.amplitude, 2), 0.75F);
    EXPECT_EQ(get_flattened_item(ep.amplitude, 3), 1.00F);
}

TEST(engine_gain_designer, simd_per_channel_pipe_updates_only_target_channel)
{
    // Push a new amplitude on lane 2 only; lanes 0/1/3 should hold.
    using SimdT = statusbar::dsp::simd_float32x4;
    GainDesigner<SimdT> designer;
    LatestPipe<Segment<GainAmplitudeCoeffs<SimdT>>> out_pipe;
    LatestPipe<float> lane2_pipe;
    designer.connect_input(2, lane2_pipe);
    designer.connect_output(out_pipe);

    designer.set_initial(0.0F);  // all lanes start at 0
    lane2_pipe.publish(0.6F);

    designer.step(Engine48k::recip_samples_per_leg_f);
    auto seg = out_pipe.try_consume();
    EXPECT_TRUE(seg.has_value());

    using statusbar::dsp::get_flattened_item;
    EXPECT_EQ(get_flattened_item(seg->end.amplitude, 0), 0.0F);
    EXPECT_EQ(get_flattened_item(seg->end.amplitude, 1), 0.0F);
    EXPECT_EQ(get_flattened_item(seg->end.amplitude, 2), 0.6F);
    EXPECT_EQ(get_flattened_item(seg->end.amplitude, 3), 0.0F);
    // delta on lane 2 only
    EXPECT_EQ(get_flattened_item(seg->delta.amplitude, 2), 0.6F / 256.0F);
    EXPECT_EQ(get_flattened_item(seg->delta.amplitude, 0), 0.0F);
}

TEST(engine_gain_designer, holds_at_target_when_no_new_input)
{
    GainDesigner<float> designer;
    LatestPipe<float> in_pipe;
    LatestPipe<Segment<GainAmplitudeCoeffs<float>>> out_pipe;
    designer.connect_input(0, in_pipe);
    designer.connect_output(out_pipe);

    designer.set_initial(0.0F);
    in_pipe.publish(0.3F);
    designer.step(Engine48k::recip_samples_per_leg_f);
    (void)out_pipe.try_consume();  // discard the ramp segment

    // No more input; subsequent steps should publish hold segments at 0.3.
    for (int i = 0; i < 4; ++i) {
        designer.step(Engine48k::recip_samples_per_leg_f);
        auto seg = out_pipe.try_consume();
        EXPECT_TRUE(seg.has_value());
        EXPECT_EQ(seg->start.amplitude, 0.3F);
        EXPECT_EQ(seg->end.amplitude, 0.3F);
        EXPECT_EQ(seg->delta.amplitude, 0.0F);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Meter<SR, T> (Tier 0 passthrough + telemetry) tests.
// ─────────────────────────────────────────────────────────────────────────────

TEST(engine_meter, satisfies_is_element)
{
    using M = Meter<48000, float>;
    static_assert(IsElement<M>);
    EXPECT_EQ(M::level, 0U);
    EXPECT_EQ(M::data_in::extent, 8U);
}

TEST(engine_meter, audio_passes_through_unchanged)
{
    Meter<48000, float> meter;
    std::array<float, 8> in{1.0F, -2.0F, 3.0F, -4.0F, 5.0F, -6.0F, 7.0F, -8.0F};
    std::array<float, 8> out{};
    meter.process_cycle(std::span<float const, 8>{in.data(), 8}, std::span<float, 8>{out.data(), 8});
    for (size_t n = 0; n < 8; ++n) {
        EXPECT_EQ(out[n], in[n]);
    }
}

TEST(engine_meter, dc_signal_yields_correct_peak_and_rms)
{
    // Constant DC at 0.5: peak = 0.5, RMS = 0.5.
    Meter<48000, float> meter;
    LatestPipe<Measurement<float>> pipe;
    meter.connect_telemetry(pipe);

    std::array<float, 8> in{};
    std::array<float, 8> out{};
    for (auto& v : in) {
        v = 0.5F;
    }

    // Drive a full leg (32 cycles of 8 samples).
    for (size_t c = 0; c < Engine48k::cycles_per_leg; ++c) {
        meter.process_cycle(std::span<float const, 8>{in.data(), 8}, std::span<float, 8>{out.data(), 8});
    }

    auto m = pipe.try_consume();
    EXPECT_TRUE(m.has_value());
    EXPECT_EQ(m->peak, 0.5F);
    EXPECT_TRUE(std::abs(m->rms - 0.5F) < 1e-6F);
}

TEST(engine_meter, square_wave_alternating_yields_unity_peak_and_rms)
{
    // Alternating ±1.0 — peak = 1.0, RMS = 1.0.
    Meter<48000, float> meter;
    std::array<float, 8> in{1.0F, -1.0F, 1.0F, -1.0F, 1.0F, -1.0F, 1.0F, -1.0F};
    std::array<float, 8> out{};
    for (size_t c = 0; c < Engine48k::cycles_per_leg; ++c) {
        meter.process_cycle(std::span<float const, 8>{in.data(), 8}, std::span<float, 8>{out.data(), 8});
    }
    auto const& m = meter.last_measurement();
    EXPECT_EQ(m.peak, 1.0F);
    EXPECT_TRUE(std::abs(m.rms - 1.0F) < 1e-6F);
}

TEST(engine_meter, resets_between_legs)
{
    // Leg 1: signal 0.5. Leg 2: signal 0.0 (silence). Leg 2's
    // measurement should reflect silence, not retain leg 1's stats.
    Meter<48000, float> meter;
    std::array<float, 8> in_loud{};
    std::array<float, 8> in_quiet{};
    std::array<float, 8> out{};
    for (auto& v : in_loud) {
        v = 0.5F;
    }

    for (size_t c = 0; c < Engine48k::cycles_per_leg; ++c) {
        meter.process_cycle(std::span<float const, 8>{in_loud.data(), 8}, std::span<float, 8>{out.data(), 8});
    }
    EXPECT_EQ(meter.last_measurement().peak, 0.5F);

    for (size_t c = 0; c < Engine48k::cycles_per_leg; ++c) {
        meter.process_cycle(std::span<float const, 8>{in_quiet.data(), 8}, std::span<float, 8>{out.data(), 8});
    }
    EXPECT_EQ(meter.last_measurement().peak, 0.0F);
    EXPECT_EQ(meter.last_measurement().rms, 0.0F);
}

TEST(engine_meter, no_telemetry_pipe_still_updates_last_measurement)
{
    Meter<48000, float> meter;
    std::array<float, 8> in{};
    std::array<float, 8> out{};
    for (auto& v : in) {
        v = 0.25F;
    }
    for (size_t c = 0; c < Engine48k::cycles_per_leg; ++c) {
        meter.process_cycle(std::span<float const, 8>{in.data(), 8}, std::span<float, 8>{out.data(), 8});
    }
    EXPECT_EQ(meter.last_measurement().peak, 0.25F);
}

TEST(engine_meter, simd_per_lane_independent_metering)
{
    // SIMD meter: 4 independent per-lane meters in one element.
    using SimdT = statusbar::dsp::simd_float32x4;
    Meter<48000, SimdT> meter;
    std::array<SimdT, 8> in{};
    std::array<SimdT, 8> out{};
    // Fill with per-lane different DC: lane 0 = 0.1, lane 1 = 0.5,
    // lane 2 = 0.9, lane 3 = 0.0 (silent).
    SimdT const dc{0.1F, 0.5F, 0.9F, 0.0F};
    for (auto& v : in) {
        v = dc;
    }
    for (size_t c = 0; c < Engine48k::cycles_per_leg; ++c) {
        meter.process_cycle(std::span<SimdT const, 8>{in.data(), 8}, std::span<SimdT, 8>{out.data(), 8});
    }
    auto const& m = meter.last_measurement();
    using statusbar::dsp::get_flattened_item;
    EXPECT_EQ(get_flattened_item(m.peak, 0), 0.1F);
    EXPECT_EQ(get_flattened_item(m.peak, 1), 0.5F);
    EXPECT_EQ(get_flattened_item(m.peak, 2), 0.9F);
    EXPECT_EQ(get_flattened_item(m.peak, 3), 0.0F);
    EXPECT_TRUE(std::abs(get_flattened_item(m.rms, 0) - 0.1F) < 1e-5F);
    EXPECT_TRUE(std::abs(get_flattened_item(m.rms, 1) - 0.5F) < 1e-5F);
    EXPECT_TRUE(std::abs(get_flattened_item(m.rms, 2) - 0.9F) < 1e-5F);
    EXPECT_EQ(get_flattened_item(m.rms, 3), 0.0F);
}

TEST_MAIN(statusbar_engine, engine_test)
