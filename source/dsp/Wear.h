#pragma once

// Tape noise and wear: hiss, and dropouts where the oxide is thin or the tape lifts off
// the head for a moment.

#include "Blocks.h"

#include <cmath>

namespace contrail::dsp
{

// Tape hiss: Gaussian noise with a gentle lift in the highs, as heard after playback EQ.
// Each channel has its own noise. Unit RMS, before the level is applied.
class Hiss
{
public:
    void prepare (double sampleRate)
    {
        rngL.setSeed (0xA11CE5u);
        rngR.setSeed (0xB0B0B0u);
        lpCoeff = OnePoleLowpass::coefficientFor (1500.0f, sampleRate);
        // Variance of w - 0.5 lp(w) for unit white w, where lp has gain (1 - a) on w.
        const float a = lpCoeff;
        const float var = 1.0f - (1.0f - a) + 0.25f * (1.0f - a) / (1.0f + a);
        norm = 1.0f / std::sqrt (var);
        reset();
    }

    void reset() noexcept { lpL.reset(); lpR.reset(); }

    void next (float& l, float& r) noexcept
    {
        l = shape (rngL.gaussian(), lpL);
        r = shape (rngR.gaussian(), lpR);
    }

private:
    float shape (float w, OnePoleLowpass& lp) noexcept
    {
        // Half the lows taken out: about +6 dB of tilt from low to high.
        return norm * (w - 0.5f * lp.process (w, lpCoeff));
    }

    Random rngL, rngR;
    OnePoleLowpass lpL, lpR;
    float lpCoeff = 0.8f, norm = 1.0f;
};

// Random dropouts: short dips in level, each one also dulling the highs (the tape is
// further from the head). They arrive at random (a Poisson process), each with its own
// length and depth, with smooth edges so they never click.
class Dropouts
{
public:
    void prepare (double sampleRate)
    {
        fs = static_cast<float> (sampleRate);
        rng.setSeed (0xD809u);
        lpCoeff = OnePoleLowpass::coefficientFor (1800.0f, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        remaining = 0;
        length = 1;
        depth = 0.0f;
        lpL.reset();
        lpR.reset();
    }

    // rate: dropouts per second. maxDepthDb: the deepest a dropout gets.
    void setAmount (float rate, float maxDepthDb) noexcept
    {
        perSample = rate / fs;
        maxDepth = 1.0f - dbToGain (-maxDepthDb);
    }

    bool isActive() const noexcept { return remaining > 0; }

    void process (float& l, float& r) noexcept
    {
        if (remaining <= 0)
        {
            if (perSample <= 0.0f || rng.unit() >= perSample)
            {
                // Keep the dulling filters tracking the signal so the next dropout starts clean.
                lpL.process (l, lpCoeff);
                lpR.process (r, lpCoeff);
                return;
            }
            // A new dropout: 8 to 120 ms, mostly short ones.
            const float u = rng.unit();
            length = std::max (8, static_cast<int> ((0.008f + 0.112f * u * u) * fs));
            remaining = length;
            const float d = rng.unit();
            depth = maxDepth * (0.3f + 0.7f * d);
        }

        // Raised-cosine dip over the dropout's length.
        const float phase = 1.0f - static_cast<float> (remaining) / static_cast<float> (length);
        const float env = 0.5f - 0.5f * std::cos (kTwoPi * phase);
        const float amount = depth * env;
        const float dl = lpL.process (l, lpCoeff);
        const float dr = lpR.process (r, lpCoeff);
        // Dull first, then dip: a lifted tape loses the highs before the level.
        l = (1.0f - amount) * (l + std::min (1.0f, 2.0f * amount) * (dl - l));
        r = (1.0f - amount) * (r + std::min (1.0f, 2.0f * amount) * (dr - r));
        --remaining;
    }

private:
    float fs = 48000.0f, perSample = 0.0f, maxDepth = 0.0f, depth = 0.0f, lpCoeff = 0.8f;
    int remaining = 0, length = 1;
    Random rng;
    OnePoleLowpass lpL, lpR;
};

} // namespace contrail::dsp
