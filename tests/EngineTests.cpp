#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace contrail;
using Catch::Approx;

TEST_CASE ("Latency is 25 ms at every sample rate, and the wet signal lines up with it")
{
    for (const double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        dsp::TapeEngine e;
        test::prepared (e, test::cleanParams (0), fs);
        CHECK (e.getLatencySamples() == static_cast<int> (std::lround (0.025 * fs)));

        // A quiet tone through a clean, steady tape comes out delayed by the latency.
        auto l = test::sine (300.0, fs, static_cast<std::size_t> (fs), 0.05f);
        auto r = l;
        const auto x = l;
        test::run (e, l, r);
        const auto lat = static_cast<std::size_t> (e.getLatencySamples());
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (std::size_t i = static_cast<std::size_t> (fs * 0.3); i < x.size(); ++i)
        {
            ab += static_cast<double> (l[i]) * x[i - lat];
            aa += static_cast<double> (l[i]) * l[i];
            bb += static_cast<double> (x[i - lat]) * x[i - lat];
        }
        CHECK (ab / std::sqrt (aa * bb) > 0.995);
        CHECK (test::db (std::sqrt (aa / bb)) == Approx (0.0).margin (1.5));
    }
}

TEST_CASE ("Mix at 0% and bypass are the dry signal, bit for bit (delayed by the latency)")
{
    for (const bool useBypass : { false, true })
    {
        dsp::TapeEngine e;
        auto p = test::formatParams (3, true);
        if (useBypass)
            p.bypass = true;
        else
            p.mix = 0.0f;
        test::prepared (e, p);

        auto l = test::pinkNoise (48000, 0.2f, 1u);
        auto r = test::pinkNoise (48000, 0.2f, 2u);
        const auto xl = l, xr = r;
        test::run (e, l, r, 173);
        const auto lat = static_cast<std::size_t> (e.getLatencySamples());
        bool exact = true;
        for (std::size_t i = lat; i < l.size(); ++i)
            exact = exact && l[i] == xl[i - lat] && r[i] == xr[i - lat];
        CHECK (exact);
    }
}

TEST_CASE ("Bypass fades in and out without clicks")
{
    dsp::TapeEngine e;
    auto p = test::formatParams (2);
    test::prepared (e, p);
    auto l = test::sine (200.0, 48000.0, 48000 * 2, 0.5f);
    auto r = l;
    for (std::size_t pos = 0; pos < l.size(); pos += 256)
    {
        p.bypass = (pos / 12000) % 2 == 1;
        e.setParams (p);
        e.process (l.data() + pos, r.data() + pos, 256);
    }
    double maxStep = 0.0;
    for (std::size_t i = 4800; i < l.size(); ++i)
        maxStep = std::max (maxStep, static_cast<double> (std::abs (l[i] - l[i - 1])));
    CHECK (maxStep < 0.05);
}

TEST_CASE ("Silence in is silence out when Noise is off, on every format")
{
    for (int f = 0; f < dsp::kNumFormats; ++f)
    {
        dsp::TapeEngine e;
        test::prepared (e, test::formatParams (f, false));
        std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
        test::run (e, l, r);
        CHECK (test::peak (l) == 0.0);
        CHECK (test::peak (r) == 0.0);
    }
}

TEST_CASE ("Hiss sits at each format's level")
{
    for (int f = 0; f < dsp::kNumFormats; ++f)
    {
        auto p = test::formatParams (f, true);
        p.age = 0.0f;
        p.tone = 0.0f;
        dsp::TapeEngine e;
        test::prepared (e, p);
        std::vector<float> l (48000 * 2, 0.0f), r (48000 * 2, 0.0f);
        test::run (e, l, r);
        const double level = test::db (test::rms (l, 9600));
        const auto& prof = dsp::formatProfile (f);
        // The format's bandwidth and low cut take some of it away.
        CHECK (level < prof.hissDb + prof.trimDb + 1.0);
        CHECK (level > prof.hissDb + prof.trimDb - 12.0);
    }
}

TEST_CASE ("Formats are loudness matched at their factory settings")
{
    std::array<double, dsp::kNumFormats> levels {};
    for (int f = 0; f < dsp::kNumFormats; ++f)
    {
        dsp::TapeEngine e;
        test::prepared (e, test::formatParams (f));
        auto l = test::pinkNoise (48000 * 4, 0.125f, 11u); // -18 dBFS
        auto r = test::pinkNoise (48000 * 4, 0.125f, 12u);
        test::run (e, l, r);
        levels[static_cast<std::size_t> (f)] = test::db (test::rms (l, 48000));
        UNSCOPED_INFO ("format " << f << ": " << levels[static_cast<std::size_t> (f)] << " dB");
    }
    for (const double level : levels)
        CHECK (level == Approx (-18.0).margin (1.0));
}

TEST_CASE ("Every format sounds the same at 44.1 and 96 kHz")
{
    for (int f = 0; f < dsp::kNumFormats; ++f)
    {
        std::array<double, 2> levels {}, highs {};
        const std::array<double, 2> rates { 44100.0, 96000.0 };
        for (std::size_t k = 0; k < 2; ++k)
        {
            const double fs = rates[k];
            auto p = test::formatParams (f);
            p.wowPercent = p.flutterPercent = 0.0f;
            p.age = std::min (p.age, 0.5f);
            dsp::TapeEngine e;
            test::prepared (e, p, fs);
            auto l = test::sine (1000.0, fs, static_cast<std::size_t> (fs * 2), 0.3f);
            auto r = l;
            test::run (e, l, r);
            levels[k] = test::db (test::toneAmplitude (l, 1000.0, fs, static_cast<std::size_t> (fs * 0.5)));
            auto hl = test::sine (4000.0, fs, static_cast<std::size_t> (fs * 2), 0.05f);
            auto hr = hl;
            dsp::TapeEngine e2;
            test::prepared (e2, p, fs);
            test::run (e2, hl, hr);
            highs[k] = test::db (test::toneAmplitude (hl, 4000.0, fs, static_cast<std::size_t> (fs * 0.5)));
        }
        CHECK (levels[0] == Approx (levels[1]).margin (0.5));
        CHECK (highs[0] == Approx (highs[1]).margin (0.75));
    }
}

TEST_CASE ("Ten seconds of random automation never blows up")
{
    dsp::Random rng (42u);
    for (const double fs : { 44100.0, 96000.0 })
    {
        dsp::TapeEngine e;
        auto p = test::formatParams (0, true);
        test::prepared (e, p, fs);
        auto l = test::pinkNoise (static_cast<std::size_t> (fs * 10), 0.7f, 5u);
        auto r = test::pinkNoise (static_cast<std::size_t> (fs * 10), 0.7f, 6u);
        for (std::size_t pos = 0; pos < l.size(); pos += 480)
        {
            if (rng.unit() < 0.3f)
            {
                p.format = static_cast<int> (rng.unit() * dsp::kNumFormats);
                p.wowPercent = 2.0f * rng.unit();
                p.flutterPercent = rng.unit();
                p.drift = rng.unit();
                p.driveDb = -6.0f + 24.0f * rng.unit();
                p.age = rng.unit();
                p.tone = rng.bipolar();
                p.mix = rng.unit();
                p.bias = rng.unit();
                p.outputDb = -18.0f + 36.0f * rng.unit();
                p.tapeStop = rng.unit() < 0.2f;
                p.sync = rng.unit() < 0.5f;
                p.syncRate = static_cast<int> (rng.unit() * dsp::kNumSyncRates);
                p.wide = rng.unit() < 0.5f;
                p.noise = rng.unit() < 0.5f;
                p.bypass = rng.unit() < 0.1f;
                e.setParams (p);
                dsp::HostTransport host;
                host.valid = true;
                host.playing = rng.unit() < 0.7f;
                host.bpm = 40.0 + 200.0 * rng.unit();
                host.ppqPosition = static_cast<double> (pos) / fs * 2.0;
                e.setHostTransport (host);
            }
            const int n = static_cast<int> (std::min<std::size_t> (480, l.size() - pos));
            e.process (l.data() + pos, r.data() + pos, n);
        }
        CHECK (test::allFinite (l));
        CHECK (test::allFinite (r));
        CHECK (test::peak (l) < 16.0); // +24 dB at most (+18 dB output on a hot signal)
    }
}

TEST_CASE ("Changing format glides instead of clicking")
{
    dsp::TapeEngine e;
    auto p = test::cleanParams (0);
    test::prepared (e, p);
    auto l = test::sine (150.0, 48000.0, 48000 * 3, 0.3f);
    auto r = l;
    for (std::size_t pos = 0; pos < l.size(); pos += 128)
    {
        p.format = static_cast<int> (pos / 24000) % dsp::kNumFormats;
        e.setParams (p);
        e.process (l.data() + pos, r.data() + pos, 128);
    }
    double maxStep = 0.0;
    for (std::size_t i = 4800; i < l.size(); ++i)
        maxStep = std::max (maxStep, static_cast<double> (std::abs (l[i] - l[i - 1])));
    // A 150 Hz sine at 0.3 moves at most 0.006 per sample; leave room for gentle change.
    CHECK (maxStep < 0.02);
}

TEST_CASE ("Wow bends the pitch of a held note")
{
    const double fs = 48000.0;
    auto measureSpread = [fs] (float wow) {
        auto p = test::cleanParams (0);
        p.wowPercent = wow;
        p.drift = 0.0f;
        dsp::TapeEngine e;
        test::prepared (e, p, fs);
        auto l = test::sine (1000.0, fs, static_cast<std::size_t> (fs * 6), 0.1f);
        auto r = l;
        test::run (e, l, r);
        // Period-by-period frequency from zero crossings.
        std::vector<double> freqs;
        double last = -1.0;
        for (std::size_t i = static_cast<std::size_t> (fs); i < l.size(); ++i)
            if (l[i - 1] < 0.0f && l[i] >= 0.0f)
            {
                const double t = static_cast<double> (i - 1) + l[i - 1] / (l[i - 1] - l[i]);
                if (last > 0.0)
                    freqs.push_back (fs / (t - last));
                last = t;
            }
        const auto [mn, mx] = std::minmax_element (freqs.begin(), freqs.end());
        return (*mx - *mn) / 2.0 / 1000.0 * 100.0; // peak deviation in %
    };
    CHECK (measureSpread (0.0f) < 0.01);
    const double dev = measureSpread (0.5f);
    CHECK (dev > 0.3);
    CHECK (dev < 0.8);
}

TEST_CASE ("Drive saturates, Age dulls the highs, Tone tilts")
{
    const double fs = 48000.0;
    auto thd = [fs] (float driveDb) {
        auto p = test::cleanParams (2);
        p.driveDb = driveDb;
        dsp::TapeEngine e;
        test::prepared (e, p, fs);
        auto l = test::sine (100.0, fs, static_cast<std::size_t> (fs * 2), 0.5f);
        auto r = l;
        test::run (e, l, r);
        const auto from = static_cast<std::size_t> (fs * 0.5);
        return test::toneAmplitude (l, 300.0, fs, from) / test::toneAmplitude (l, 100.0, fs, from);
    };
    CHECK (thd (12.0f) > 3.0 * thd (-6.0f));

    auto highs = [fs] (float age, float tone) {
        auto p = test::cleanParams (2);
        p.age = age;
        p.tone = tone;
        dsp::TapeEngine e;
        test::prepared (e, p, fs);
        auto l = test::sine (8000.0, fs, static_cast<std::size_t> (fs), 0.05f);
        auto r = l;
        test::run (e, l, r);
        return test::db (test::toneAmplitude (l, 8000.0, fs, static_cast<std::size_t> (fs * 0.3)));
    };
    CHECK (highs (1.0f, 0.0f) < highs (0.0f, 0.0f) - 4.0);
    CHECK (highs (0.0f, -1.0f) < highs (0.0f, 0.0f) - 4.0);
    CHECK (highs (0.0f, 1.0f) > highs (0.0f, 0.0f) + 4.0);
}

TEST_CASE ("Tape Stop through the engine halts the sound, and Start brings it back")
{
    const double fs = 48000.0;
    auto p = test::formatParams (2);
    dsp::TapeEngine e;
    test::prepared (e, p, fs);
    auto l = test::sine (500.0, fs, static_cast<std::size_t> (fs * 5), 0.3f);
    auto r = l;
    for (std::size_t pos = 0; pos < l.size(); pos += 256)
    {
        p.tapeStop = pos >= static_cast<std::size_t> (fs) && pos < static_cast<std::size_t> (fs * 2.5);
        e.setParams (p);
        e.process (l.data() + pos, r.data() + pos, static_cast<int> (std::min<std::size_t> (256, l.size() - pos)));
    }
    const auto w = static_cast<std::size_t> (fs * 0.1);
    const double before = test::rms (l.data() + static_cast<std::size_t> (0.6 * fs), w);
    const double stopped = test::rms (l.data() + static_cast<std::size_t> (2.3 * fs), w);
    const double after = test::rms (l.data() + static_cast<std::size_t> (4.5 * fs), w);
    CHECK (stopped < 1.0e-4);
    CHECK (test::db (after / before) == Approx (0.0).margin (1.0));
}

TEST_CASE ("Mono formats narrow the stereo image")
{
    auto sideLevel = [] (int format) {
        auto p = test::cleanParams (format);
        dsp::TapeEngine e;
        test::prepared (e, p);
        auto l = test::pinkNoise (48000, 0.1f, 1u);
        auto r = test::pinkNoise (48000, 0.1f, 2u);
        test::run (e, l, r);
        std::vector<float> side (l.size());
        for (std::size_t i = 0; i < l.size(); ++i)
            side[i] = 0.5f * (l[i] - r[i]);
        return test::rms (side, 4800) / test::rms (l, 4800);
    };
    CHECK (sideLevel (5) < 0.5 * sideLevel (0));
}
