#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace contrail;
using Catch::Approx;

TEST_CASE ("Tape stop slows the pitch to a halt and goes silent")
{
    const double fs = 48000.0;
    dsp::TapeTransport t;
    t.prepare (fs, 1000.0);
    t.setTimes (1.0f, 0.5f);

    const auto x = test::sine (1000.0, fs, static_cast<std::size_t> (fs * 3.0));
    std::vector<float> y (x.size());
    float r;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        if (i == static_cast<std::size_t> (fs))
            t.setStopped (true);
        t.process (x[i], x[i], 0.0f, 0.0f, y[i], r);
    }

    // Running: full level. Halfway through the stop: quieter (and lower). Stopped: silent.
    const auto window = static_cast<std::size_t> (fs * 0.05);
    CHECK (test::rms (y.data() + static_cast<std::size_t> (0.5 * fs), window) == Approx (0.5 / std::sqrt (2.0)).epsilon (0.02));
    const double mid = test::rms (y.data() + static_cast<std::size_t> (1.5 * fs), window);
    CHECK (mid < 0.3);
    CHECK (mid > 0.05);
    CHECK (test::rms (y.data() + static_cast<std::size_t> (2.2 * fs), window) < 1.0e-6);
    CHECK (t.speed() == 0.0f);

    // Pitch halfway through the stop is about half (count zero crossings).
    int crossings = 0;
    const auto from = static_cast<std::size_t> (1.48 * fs), to = static_cast<std::size_t> (1.52 * fs);
    for (std::size_t i = from + 1; i < to; ++i)
        crossings += (y[i - 1] < 0.0f) != (y[i] < 0.0f);
    const double hz = crossings / 2.0 / 0.04;
    CHECK (hz == Approx (500.0).epsilon (0.2));
}

TEST_CASE ("Tape start comes back up to speed and back in time")
{
    const double fs = 48000.0;
    dsp::TapeTransport t;
    t.prepare (fs, 1000.0);
    t.setTimes (0.4f, 0.3f);

    const auto x = test::sine (440.0, fs, static_cast<std::size_t> (fs * 4.0));
    std::vector<float> y (x.size());
    float r;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        if (i == static_cast<std::size_t> (0.5 * fs))
            t.setStopped (true);
        if (i == static_cast<std::size_t> (1.5 * fs))
            t.setStopped (false);
        t.process (x[i], x[i], 0.0f, 0.0f, y[i], r);
    }
    CHECK (t.speed() == 1.0f);
    CHECK (t.lagSamples() == 0.0);

    // Well after the restart, the output is the input delayed by exactly the base delay.
    double worst = 0.0;
    for (auto i = static_cast<std::size_t> (3.0 * fs); i < x.size(); ++i)
        worst = std::max (worst, static_cast<double> (std::abs (y[i] - x[i - 1000])));
    CHECK (worst < 1.0e-3);

    // No clicks anywhere: the biggest step between samples stays near a sine's own.
    double maxStep = 0.0;
    for (std::size_t i = 1; i < y.size(); ++i)
        maxStep = std::max (maxStep, static_cast<double> (std::abs (y[i] - y[i - 1])));
    CHECK (maxStep < 0.5 * 2.0 * std::numbers::pi * 440.0 / fs * 1.6);
}
