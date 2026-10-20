#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace contrail;
using Catch::Approx;

namespace
{
    std::vector<float> throughHysteresis (const std::vector<float>& x, float saturation, float width, double fs = 192000.0)
    {
        dsp::Hysteresis h;
        h.prepare (fs);
        h.setParams (saturation, width);
        std::vector<float> y (x.size());
        for (std::size_t i = 0; i < x.size(); ++i)
            y[i] = h.process (x[i]);
        return y;
    }

    // Level of the odd harmonics relative to the fundamental.
    double oddHarmonics (const std::vector<float>& y, double hz, double fs, std::size_t from)
    {
        const double f1 = test::toneAmplitude (y, hz, fs, from);
        const double h3 = test::toneAmplitude (y, 3.0 * hz, fs, from);
        const double h5 = test::toneAmplitude (y, 5.0 * hz, fs, from);
        return std::sqrt (h3 * h3 + h5 * h5) / f1;
    }
}

TEST_CASE ("Quiet signals pass the tape at about their own level")
{
    for (const float sat : { 0.4f, 0.6f, 0.8f })
    {
        const auto x = test::sine (1000.0, 192000.0, 192000 / 2, 0.03f);
        const auto y = throughHysteresis (x, sat, 0.5f);
        const double gain = test::toneAmplitude (y, 1000.0, 192000.0, 20000) / 0.03;
        CHECK (test::db (gain) == Approx (0.0).margin (2.0));
    }
}

TEST_CASE ("Loud signals saturate, and harder for more saturation")
{
    const double fs = 192000.0;
    const auto x = test::sine (200.0, fs, 192000, 1.5f);
    const auto soft = throughHysteresis (x, 0.3f, 0.5f);
    const auto hard = throughHysteresis (x, 0.8f, 0.5f);
    CHECK (test::peak (soft, 20000) < 1.5);
    CHECK (oddHarmonics (hard, 200.0, fs, 20000) > oddHarmonics (soft, 200.0, fs, 20000));

    // Quieter is cleaner.
    const auto quiet = throughHysteresis (test::sine (200.0, fs, 192000, 0.1f), 0.8f, 0.5f);
    CHECK (oddHarmonics (quiet, 200.0, fs, 20000) < oddHarmonics (hard, 200.0, fs, 20000));
}

TEST_CASE ("Magnetisation lags the field: the output has a hysteresis loop")
{
    // A slow triangle up and down: on the way up the output is below the way down at the
    // same input (the loop), and the response is symmetric.
    const double fs = 192000.0;
    std::vector<float> x;
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        for (int i = 0; i < 4000; ++i) x.push_back (static_cast<float> (i) / 4000.0f);
        for (int i = 0; i < 8000; ++i) x.push_back (1.0f - static_cast<float> (i) / 4000.0f);
        for (int i = 0; i < 4000; ++i) x.push_back (-1.0f + static_cast<float> (i) / 4000.0f);
    }
    const auto y = throughHysteresis (x, 0.5f, 0.8f, fs);
    // Last cycle: crossing zero going up (index 2*16000 + 0) vs coming down (2*16000 + 8000).
    const std::size_t start = 2 * 16000;
    const float upAtZero = y[start];
    const float downAtZero = y[start + 8000];
    CHECK (downAtZero > upAtZero + 0.01f);
    CHECK (std::abs (downAtZero + upAtZero) < 0.05f * std::abs (downAtZero - upAtZero) + 1.0e-3f);
}

TEST_CASE ("Hysteresis stays finite for extreme input")
{
    dsp::Random rng (7u);
    std::vector<float> x (192000);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (i / 4000) % 2 ? 40.0f * rng.bipolar() : 1.0e-6f * rng.bipolar();
    for (const float w : { 0.0f, 0.5f, 0.98f })
    {
        const auto y = throughHysteresis (x, 1.0f, w);
        CHECK (test::allFinite (y));
        CHECK (test::peak (y) < 10.0);
    }
}
