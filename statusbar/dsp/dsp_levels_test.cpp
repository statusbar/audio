// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/dsp/dsp_levels.hpp"

#include "statusbar/dsp/dsp_math.hpp"  // approx_equal
#include "statusbar/test/test.hpp"

#include <cmath>
#include <limits>

using statusbar::dsp::amplitude_to_db;
using statusbar::dsp::approx_equal;
using statusbar::dsp::db_to_amplitude;

TEST(dsp_levels, db_to_amplitude_anchor_points)
{
    EXPECT_TRUE(approx_equal(db_to_amplitude(0.0), 1.0));
    EXPECT_TRUE(approx_equal(db_to_amplitude(-6.0), 0.501187, 1e-5));
    EXPECT_TRUE(approx_equal(db_to_amplitude(-20.0), 0.1, 1e-9));
    EXPECT_TRUE(approx_equal(db_to_amplitude(-60.0), 0.001, 1e-9));
    EXPECT_TRUE(approx_equal(db_to_amplitude(6.0), 1.995262, 1e-5));
}

TEST(dsp_levels, amplitude_to_db_anchor_points)
{
    EXPECT_TRUE(approx_equal(amplitude_to_db(1.0), 0.0));
    EXPECT_TRUE(approx_equal(amplitude_to_db(0.5), -6.020600, 1e-5));
    EXPECT_TRUE(approx_equal(amplitude_to_db(0.1), -20.0));
    EXPECT_TRUE(approx_equal(amplitude_to_db(0.001), -60.0));
}

TEST(dsp_levels, round_trip_double)
{
    for (double db : {-90.0, -60.0, -12.5, -6.0, 0.0, 3.0, 12.0}) {
        EXPECT_TRUE(approx_equal(amplitude_to_db(db_to_amplitude(db)), db, 1e-9));
    }
}

TEST(dsp_levels, round_trip_float)
{
    for (float db : {-90.0F, -60.0F, -12.5F, -6.0F, 0.0F, 3.0F, 12.0F}) {
        EXPECT_TRUE(approx_equal(amplitude_to_db(db_to_amplitude(db)), db, 1e-4F));
    }
}

TEST(dsp_levels, amplitude_to_db_at_zero_is_neg_inf)
{
    // log10(0) = -inf; this is the documented behavior for a fully muted
    // signal. Callers that want a finite floor should clamp before calling.
    EXPECT_EQ(amplitude_to_db(0.0), -std::numeric_limits<double>::infinity());
}

TEST_MAIN(statusbar_dsp, dsp_levels_test)
