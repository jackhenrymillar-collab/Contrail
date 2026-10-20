#pragma once

// Wow and flutter: the speed wobble of a tape transport.
//
// The motion is built as a speed deviation s(t) (0.01 = 1% fast), the way wow and
// flutter are specified on real machines, and then integrated into a delay offset for
// the read head. It sums:
//   - reel or capstan eccentricity: a periodic wow with a little second harmonic,
//   - random wow: smooth random swings, so the movement never loops,
//   - slow drift (Drift knob): wandering speed and wandering wow rate,
//   - capstan and pinch-roller flutter: two periodic rates plus random flutter,
//   - scrape flutter: a very fast, tiny band of noise that adds grit.
// Each channel can have its own random motion (Stereo: Wide) or share one (Linked).

#include "Blocks.h"
#include "Formats.h"

#include <cmath>

namespace contrail::dsp
{

// Smooth random swings: a new random point every half cycle, joined with a Catmull-Rom
// curve so the slope (and so the pitch) never jumps. Points alternate up and down with
// random size and timing, so the output stays near +-1 like a sine but never repeats.
class RandomSwing
{
public:
    void prepare (double sampleRate, std::uint32_t seed)
    {
        fs = static_cast<float> (sampleRate);
        rng.setSeed (seed);
        for (auto& p : points)
            p = nextPoint();
        t = rng.unit();
    }

    void setFrequency (float hz) noexcept { increment = 2.0f * hz / fs; }

    float next() noexcept
    {
        const float p0 = points[0], p1 = points[1], p2 = points[2], p3 = points[3];
        const float out = 0.5f * (2.0f * p1 + (p2 - p0) * t
                                  + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t
                                  + (3.0f * (p1 - p2) + p3 - p0) * t * t * t);
        t += increment * pace;
        if (t >= 1.0f)
        {
            t -= 1.0f;
            points = { p1, p2, p3, nextPoint() };
            pace = 0.65f + 0.7f * rng.unit();
        }
        return out;
    }

private:
    float nextPoint() noexcept
    {
        side = -side;
        return side * (0.35f + 0.65f * rng.unit());
    }

    float fs = 48000.0f, increment = 0.0f, t = 0.0f, side = 1.0f, pace = 1.0f;
    Random rng;
    std::array<float, 4> points {};
};

struct WowFlutterParams
{
    float wowPercent = 0.1f;      // peak wow, % speed deviation
    float flutterPercent = 0.05f; // peak flutter, % speed deviation
    float drift = 0.25f;          // 0 = steady periodic wow, 1 = wandering
    bool wide = true;             // independent motion per channel
    bool synced = false;          // periodic wow follows the host tempo
    float syncHz = 0.5f;          // periodic wow rate when synced
    float syncPhase = -1.0f;      // 0..1 phase from the host's beat position, or -1
};

class WowFlutter
{
public:
    void prepare (double sampleRate, float maxOffsetSamples)
    {
        fs = sampleRate;
        maxOffset = maxOffsetSamples;
        // Leak on the integrator: a very slow pull back to the centre (about 0.03 Hz),
        // so the read head never wanders off however long the random motion runs.
        leak = std::exp (-kTwoPi * 0.03 / sampleRate);
        for (std::size_t ch = 0; ch < 2; ++ch)
        {
            const auto seed = static_cast<std::uint32_t> (0xC0FFEE + 7919 * ch);
            auto& c = chans[ch];
            c.randomWow.prepare (sampleRate, seed + 1);
            c.drift.prepare (sampleRate, seed + 2);
            c.rateWander.prepare (sampleRate, seed + 3);
            c.randomFlutter.prepare (sampleRate, seed + 4);
            c.scrape.prepare (sampleRate, seed + 5);
            c.scrapeHp.reset();
            c.drift.setFrequency (0.11f);
            c.rateWander.setFrequency (0.07f);
            c.randomFlutter.setCutoff (14.0f);
            c.scrape.setCutoff (2500.0f);
        }
        scrapeHpCoeff = OnePoleLowpass::coefficientFor (900.0f, sampleRate);
        reset();
    }

    void reset()
    {
        for (std::size_t ch = 0; ch < 2; ++ch)
        {
            auto& c = chans[ch];
            c.offset = 0.0;
            // Wide stereo starts the two sides at different points of the periodic wow.
            c.reelPhase = ch == 0 ? 0.0 : 0.27;
            c.flutterPhase = ch == 0 ? 0.0 : 0.41;
            c.flutterPhase2 = ch == 0 ? 0.0 : 0.63;
            c.scrapeHp.reset();
        }
    }

    // Called once per control block.
    void setParams (const WowFlutterParams& p, const FormatProfile& f) noexcept
    {
        params = p;
        profile = f;
        for (auto& c : chans)
            c.randomWow.setFrequency (f.randomWowHz * (1.0f + 1.5f * p.drift));
    }

    // Advances one sample. Writes each channel's read-head offset in samples
    // (positive = further behind) and its current speed deviation.
    void next (float& offsetL, float& offsetR) noexcept
    {
        tick (chans[0], true);
        if (params.wide)
            tick (chans[1], false);
        else
        {
            chans[1].offset = chans[0].offset;
            chans[1].speed = chans[0].speed;
        }
        offsetL = clipOffset (chans[0].offset);
        offsetR = clipOffset (chans[1].offset);
    }

    float speedDeviation (int ch) const noexcept { return chans[static_cast<std::size_t> (ch & 1)].speed; }

private:
    struct Channel
    {
        double offset = 0.0;
        double reelPhase = 0.0, flutterPhase = 0.0, flutterPhase2 = 0.0;
        float speed = 0.0f;
        RandomSwing randomWow, drift, rateWander;
        SmoothNoise randomFlutter, scrape;
        OnePoleLowpass scrapeHp;
    };

    void tick (Channel& c, bool leader) noexcept
    {
        const float wow = params.wowPercent * 0.01f;
        const float flutter = params.flutterPercent * 0.01f;
        const float drift = params.drift;

        // Periodic wow: free at the format's rate, or locked to the host tempo.
        const float wander = c.rateWander.next();
        double reelHz = params.synced ? params.syncHz : profile.reelHz * (1.0 + 0.25 * drift * wander);
        if (params.synced && params.syncPhase >= 0.0f && leader)
        {
            // Pull gently toward the host's phase so wow stays on the beat without jumps.
            double err = params.syncPhase - c.reelPhase;
            err -= std::floor (err + 0.5);
            reelHz *= 1.0 + std::clamp (err * 2.0, -0.2, 0.2);
        }
        c.reelPhase += reelHz / fs;
        c.reelPhase -= std::floor (c.reelPhase);
        if (params.synced && params.syncPhase >= 0.0f && ! leader)
            c.reelPhase = chans[0].reelPhase + 0.27 - std::floor (chans[0].reelPhase + 0.27);
        const auto rp = static_cast<float> (c.reelPhase);
        const float h = profile.reelHarmonic;
        const float periodic = (std::sin (kTwoPi * rp) + h * std::sin (2.0f * kTwoPi * rp + 0.7f)) / (1.0f + 0.6f * h);

        // Drift trades the steady wow for random swings and adds a slow wander.
        const float randomShare = 0.15f + 0.7f * drift;
        const float wowShape = (1.0f - randomShare) * periodic + randomShare * c.randomWow.next();
        const float slow = c.drift.next() * 0.6f * drift;

        // Flutter: capstan and pinch-roller rates plus random flutter.
        c.flutterPhase += profile.flutterHz * (1.0 + 0.02 * wander) / fs;
        c.flutterPhase -= std::floor (c.flutterPhase);
        c.flutterPhase2 += profile.flutterHz2 / fs;
        c.flutterPhase2 -= std::floor (c.flutterPhase2);
        const float flutterShape = 0.55f * std::sin (kTwoPi * static_cast<float> (c.flutterPhase))
                                 + 0.2f * std::sin (kTwoPi * static_cast<float> (c.flutterPhase2))
                                 + 0.3f * c.randomFlutter.next();

        // Scrape flutter: band-limited noise around 1 to 3 kHz.
        const float sn = c.scrape.next();
        const float scrape = sn - c.scrapeHp.process (sn, scrapeHpCoeff);

        c.speed = wow * (wowShape + slow) + flutter * (flutterShape + profile.scrape * 0.5f * scrape);

        // Faster tape means the read head catches up: the delay shrinks.
        c.offset = c.offset * leak - static_cast<double> (c.speed);
    }

    float clipOffset (double x) const noexcept
    {
        // Soft limit, so even extreme settings stay inside the delay line.
        const double m = maxOffset;
        return static_cast<float> (m * std::tanh (x / m));
    }

    double fs = 48000.0, leak = 1.0;
    float maxOffset = 1000.0f, scrapeHpCoeff = 0.9f;
    WowFlutterParams params;
    FormatProfile profile;
    std::array<Channel, 2> chans;
};

} // namespace contrail::dsp
