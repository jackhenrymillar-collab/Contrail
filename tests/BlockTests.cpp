#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace contrail;
using Catch::Approx;

TEST_CASE ("Smooth noise has unit RMS")
{
    for (const float hz : { 1.0f, 14.0f, 2500.0f })
    {
        dsp::SmoothNoise n;
        n.prepare (48000.0, 99u);
        n.setCutoff (hz);
        std::vector<float> x (48000 * 20);
        for (auto& v : x)
            v = n.next();
        CHECK (test::rms (x, 48000 * 2) == Approx (1.0).epsilon (0.15));
    }
}

TEST_CASE ("RBJ shelves of opposite gain cancel exactly")
{
    dsp::Biquad up, down;
    up.setHighShelf (3000.0f, 6.0f, 48000.0);
    down.setHighShelf (3000.0f, -6.0f, 48000.0);
    for (const double hz : { 50.0, 1000.0, 3000.0, 10000.0, 20000.0 })
        CHECK (up.magnitudeAt (hz, 48000.0) * down.magnitudeAt (hz, 48000.0) == Approx (1.0).epsilon (1.0e-4));
    CHECK (test::db (up.magnitudeAt (20000.0, 48000.0)) == Approx (6.0).margin (0.3));
}

TEST_CASE ("Oversampler is flat in the audio band and has the latency it reports")
{
    for (const double fs : { 44100.0, 48000.0, 96000.0 })
    {
        dsp::Oversampler os;
        os.prepare (dsp::Oversampler::factorFor (fs));
        const auto identity = [] (float v) { return v; };

        // An impulse comes out exactly `latency` samples later.
        std::vector<float> y (400);
        for (std::size_t i = 0; i < y.size(); ++i)
            y[i] = os.process (i == 10 ? 1.0f : 0.0f, identity);
        const auto peakAt = static_cast<int> (std::max_element (y.begin(), y.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); }) - y.begin());
        CHECK (peakAt == 10 + os.latency());

        // Passband: within 0.1 dB up to 18 kHz.
        for (const double hz : { 100.0, 1000.0, 10000.0, 18000.0 })
        {
            os.reset();
            const auto x = test::sine (hz, fs, 16384);
            std::vector<float> out (x.size());
            for (std::size_t i = 0; i < x.size(); ++i)
                out[i] = os.process (x[i], identity);
            CHECK (test::db (test::toneAmplitude (out, hz, fs, 2000) / 0.5) == Approx (0.0).margin (0.1));
        }
    }
}

TEST_CASE ("Oversampling keeps distortion products from folding back")
{
    // A saturator at 4x on a 15 kHz tone: its 3rd harmonic (45 kHz) would fold to
    // 3 kHz at 48 kHz without oversampling.
    dsp::Oversampler os;
    os.prepare (4);
    const auto x = test::sine (15000.0, 48000.0, 32768, 0.9f);
    std::vector<float> y (x.size());
    for (std::size_t i = 0; i < x.size(); ++i)
        y[i] = os.process (x[i], [] (float v) { return std::tanh (2.0f * v); });
    const double fundamental = test::toneAmplitude (y, 15000.0, 48000.0, 4000);
    const double alias = test::toneAmplitude (y, 3000.0, 48000.0, 4000);
    CHECK (test::db (alias / fundamental) < -60.0);
}

TEST_CASE ("Band-limited delay reads are accurate at any fraction")
{
    dsp::InterpolatedDelay line;
    line.prepare (4096);
    const double fs = 48000.0;
    const auto x = test::sine (5000.0, fs, 8192);
    for (const double frac : { 0.0, 0.25, 0.5, 0.77 })
    {
        line.reset();
        const double delay = 100.0 + frac;
        double worst = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            line.push (x[i]);
            if (i > 400)
            {
                const double t = static_cast<double> (i) - delay;
                const double expected = 0.5 * std::sin (2.0 * std::numbers::pi * 5000.0 * t / fs);
                worst = std::max (worst, std::abs (line.read (delay) - expected));
            }
        }
        CHECK (worst < 2.0e-4); // better than -74 dB at 5 kHz
    }
}

TEST_CASE ("Playback losses follow the Bertram model")
{
    const auto& cassette = dsp::formatProfile (static_cast<int> (dsp::FormatId::cassette));

    // Spacing loss is 54.6 dB per wavelength of spacing.
    const double v = cassette.speedIps * 0.0254;
    const double hz = 8000.0;
    const double lambda = v / hz;
    const double withSpacing = dsp::PlaybackLosses::response (hz, cassette.speedIps, 0.0f, 1.0f, 1.0e6f, 1.0f);
    CHECK (test::db (withSpacing) == Approx (-54.6 * 1.0e-6 / lambda).epsilon (0.01));

    // Gap loss: the first null is where the wavelength equals the gap.
    const double nullHz = v / (cassette.gapUm * 1.0e-6);
    CHECK (dsp::PlaybackLosses::response (nullHz * 0.999, cassette.speedIps, cassette.gapUm, 0.0f, 1.0e6f, 1.0f) < 0.01);

    // The FIR matches the target response in the band.
    dsp::PlaybackLosses losses;
    losses.prepare (48000.0);
    losses.update (cassette, 1.0f);
    for (const double f : { 100.0, 1000.0, 4000.0, 8000.0 })
    {
        const double want = dsp::PlaybackLosses::response (f, cassette.speedIps, cassette.gapUm, cassette.ageSpacingUm, cassette.bandwidthHz, cassette.bandwidthOrder);
        CHECK (test::db (losses.designedMagnitude (f)) == Approx (test::db (want)).margin (0.5));
    }

    // More wear, duller sound; slower tape, duller still for the same wear.
    const double fastTape = dsp::PlaybackLosses::response (8000.0, 15.0f, 2.5f, 1.0f, 1.0e6f, 1.0f);
    const double slowTape = dsp::PlaybackLosses::response (8000.0, 1.875f, 2.5f, 1.0f, 1.0e6f, 1.0f);
    CHECK (slowTape < fastTape);
}
