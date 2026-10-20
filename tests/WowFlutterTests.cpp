#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace contrail;
using Catch::Approx;

namespace
{
    struct Motion
    {
        std::vector<float> offL, offR, speedL;
    };

    Motion runMotion (const dsp::WowFlutterParams& p, int format, double seconds, double fs = 48000.0)
    {
        dsp::WowFlutter wf;
        wf.prepare (fs, 1100.0f);
        wf.setParams (p, dsp::formatProfile (format));
        Motion m;
        const auto n = static_cast<std::size_t> (seconds * fs);
        m.offL.resize (n);
        m.offR.resize (n);
        m.speedL.resize (n);
        for (std::size_t i = 0; i < n; ++i)
        {
            wf.next (m.offL[i], m.offR[i]);
            m.speedL[i] = wf.speedDeviation (0);
        }
        return m;
    }
}

TEST_CASE ("Wow and flutter at zero leave the tape perfectly steady")
{
    dsp::WowFlutterParams p;
    p.wowPercent = 0.0f;
    p.flutterPercent = 0.0f;
    const auto m = runMotion (p, 3, 5.0);
    CHECK (test::peak (m.offL) == 0.0);
    CHECK (test::peak (m.offR) == 0.0);
}

TEST_CASE ("The Wow knob sets the peak speed deviation")
{
    for (const float wow : { 0.1f, 0.5f, 1.0f })
    {
        dsp::WowFlutterParams p;
        p.wowPercent = wow;
        p.flutterPercent = 0.0f;
        p.drift = 0.0f;
        const auto m = runMotion (p, 0, 20.0);
        // Peak speed deviation, in %, is close to the knob.
        CHECK (test::peak (m.speedL, 48000) * 100.0 == Approx (wow).epsilon (0.25));
    }
}

TEST_CASE ("The Flutter knob sets the peak flutter")
{
    dsp::WowFlutterParams p;
    p.wowPercent = 0.0f;
    p.flutterPercent = 0.3f;
    const auto m = runMotion (p, 2, 10.0);
    const double pk = test::peak (m.speedL, 48000) * 100.0;
    CHECK (pk > 0.2);
    CHECK (pk < 0.6);
}

TEST_CASE ("Wide stereo moves the channels differently; Linked moves them together")
{
    dsp::WowFlutterParams p;
    p.wowPercent = 0.5f;
    p.flutterPercent = 0.2f;
    p.drift = 0.5f;
    p.wide = false;
    auto m = runMotion (p, 2, 4.0);
    CHECK (m.offL == m.offR);

    p.wide = true;
    m = runMotion (p, 2, 4.0);
    double diff = 0.0;
    for (std::size_t i = 0; i < m.offL.size(); ++i)
        diff = std::max (diff, static_cast<double> (std::abs (m.offL[i] - m.offR[i])));
    CHECK (diff > 5.0);
}

TEST_CASE ("The read head never leaves the delay line, however extreme the settings")
{
    dsp::WowFlutterParams p;
    p.wowPercent = 2.0f;
    p.flutterPercent = 1.0f;
    p.drift = 1.0f;
    p.synced = true;
    p.syncHz = 0.0625f; // 4 bars at 60 bpm: the slowest, deepest wow
    const auto m = runMotion (p, 4, 60.0);
    CHECK (test::peak (m.offL) < 1100.0);
    CHECK (test::peak (m.offR) < 1100.0);
    CHECK (test::allFinite (m.offL));
}

TEST_CASE ("Drift adds random wander, so the motion never repeats")
{
    dsp::WowFlutterParams p;
    p.wowPercent = 0.5f;
    p.flutterPercent = 0.0f;
    p.drift = 1.0f;
    const auto m = runMotion (p, 0, 12.0, 4000.0);
    // Compare two windows one reel period apart: a pure sine would match exactly.
    const double period = 4000.0 / dsp::formatProfile (0).reelHz;
    double diff = 0.0, energy = 0.0;
    for (std::size_t i = 8000; i < 16000; ++i)
    {
        const double a = m.speedL[i];
        const double b = m.speedL[i + static_cast<std::size_t> (period)];
        diff += (a - b) * (a - b);
        energy += a * a;
    }
    CHECK (diff > 0.2 * energy);
}

TEST_CASE ("Sync locks the periodic wow to the host tempo")
{
    dsp::WowFlutterParams p;
    p.wowPercent = 0.5f;
    p.flutterPercent = 0.0f;
    p.drift = 0.0f;
    p.synced = true;
    p.syncHz = 2.0f; // a quarter note at 120 bpm
    const double fs = 8000.0;
    const auto m = runMotion (p, 0, 20.0, fs);
    const double atSync = test::toneAmplitude (m.speedL, 2.0, fs, 8000);
    const double atFree = test::toneAmplitude (m.speedL, dsp::formatProfile (0).reelHz, fs, 8000);
    CHECK (atSync > 4.0 * atFree);
}
