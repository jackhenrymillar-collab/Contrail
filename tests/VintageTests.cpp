// Drive's character, Sampler Crunch, Vintage Chorus and Mono.
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

using namespace contrail;

namespace
{
    struct Harmonics { double h1, h2, h3, h5; };

    Harmonics harmonics (int format, float driveDb, double hz = 1000.0, float level = 0.5f)
    {
        auto p = test::cleanParams (format);
        p.driveDb = driveDb;
        dsp::TapeEngine e;
        test::prepared (e, p);
        auto l = test::sine (hz, 48000.0, 48000 * 2, level);
        auto r = l;
        test::run (e, l, r);
        const std::size_t from = 48000;
        const double h1 = test::toneAmplitude (l, hz, 48000.0, from);
        return { h1, test::toneAmplitude (l, 2 * hz, 48000.0, from) / h1, test::toneAmplitude (l, 3 * hz, 48000.0, from) / h1,
                 test::toneAmplitude (l, 5 * hz, 48000.0, from) / h1 };
    }
}

TEST_CASE ("Drive is warm: second harmonic leads at moderate settings, high harmonics fall away")
{
    for (const int format : { 0, 1, 2 })
    {
        const auto gentle = harmonics (format, 0.0f);
        CHECK (gentle.h2 > gentle.h3);          // valve warmth first
        CHECK (test::db (gentle.h2) < -30.0);   // and still subtle at 0 dB
        const auto hot = harmonics (format, 18.0f);
        CHECK (test::db (hot.h3) > -25.0);      // pushed hard, it saturates for real
        CHECK (hot.h5 < 0.5 * hot.h3);          // but the higher harmonics stay well below
    }
}

TEST_CASE ("Crunch off is untouched; crunch lowers the resolution and the bandwidth")
{
    auto run = [] (float crunch) {
        auto p = test::cleanParams (0);
        p.crunch = crunch;
        dsp::TapeEngine e;
        test::prepared (e, p);
        auto l = test::pinkNoise (48000, 0.1f, 3u);
        auto r = l;
        test::run (e, l, r);
        return l;
    };
    const auto off = run (0.0f);
    const auto off2 = run (0.0f);
    CHECK (off == off2);

    // At the 12-bit, 26 kHz setting there's an audible fizz of quantisation and aliasing,
    // and the very top is gone.
    const auto settings = dsp::crunchSettings (0.4f);
    CHECK (std::abs (settings.bits - 12.0f) < 0.01f);
    CHECK (std::abs (settings.rateHz - 26000.0f) < 1.0f);

    auto tone = [] (float crunch, double hz) {
        auto p = test::cleanParams (0);
        p.crunch = crunch;
        dsp::TapeEngine e;
        test::prepared (e, p);
        auto l = test::sine (hz, 48000.0, 48000, 0.3f);
        auto r = l;
        test::run (e, l, r);
        return test::toneAmplitude (l, hz, 48000.0, 24000);
    };
    CHECK (test::db (tone (1.0f, 6000.0) / tone (0.0f, 6000.0)) < -12.0); // 5 kHz sampler: no 6 kHz
    CHECK (std::abs (test::db (tone (1.0f, 300.0) / tone (0.0f, 300.0))) < 3.0); // lows still there
}

TEST_CASE ("Chorus off is untouched; chorus spreads a mono signal into stereo")
{
    auto sideRatio = [] (float chorus) {
        auto p = test::cleanParams (0);
        p.chorus = chorus;
        dsp::TapeEngine e;
        test::prepared (e, p);
        auto l = test::pinkNoise (96000, 0.1f, 4u);
        auto r = l; // identical channels in
        test::run (e, l, r);
        std::vector<float> side (l.size());
        for (std::size_t i = 0; i < l.size(); ++i)
            side[i] = 0.5f * (l[i] - r[i]);
        return test::rms (side, 24000) / test::rms (l, 24000);
    };
    CHECK (sideRatio (0.0f) < 1.0e-6);
    CHECK (sideRatio (0.5f) > 0.1);
    CHECK (sideRatio (1.0f) > sideRatio (0.2f));
}

TEST_CASE ("Mono makes both channels the same")
{
    auto p = test::formatParams (0);
    p.mono = true;
    p.chorus = 0.6f;
    dsp::TapeEngine e;
    test::prepared (e, p);
    auto l = test::pinkNoise (48000, 0.1f, 5u);
    auto r = test::pinkNoise (48000, 0.1f, 6u);
    test::run (e, l, r);
    double diff = 0.0;
    for (std::size_t i = 4800; i < l.size(); ++i)
        diff = std::max (diff, static_cast<double> (std::abs (l[i] - r[i])));
    CHECK (diff < 1.0e-6);
    CHECK (test::rms (l, 4800) > 0.01);
}
