#pragma once

// Sampler Crunch: the grit of the old 12-bit drum samplers (SP-1200, S950 and friends).
//
// The signal is resampled at a lower rate with a plain sample-and-hold (no clever
// interpolation, so some aliasing gets through, which is the point), quantised to fewer
// bits, then smoothed by a "dull" output filter like those machines' reconstruction
// filters. One knob walks from 16-bit / 46 kHz, through the classic 12-bit / 26 kHz,
// down to 6-bit / 5 kHz.

#include "Blocks.h"

#include <array>
#include <cmath>
#include <cstdio>

namespace contrail::dsp
{

struct CrunchSettings
{
    float bits = 16.0f;
    float rateHz = 46000.0f;
};

// The Crunch knob (0 to 1) as bit depth and sample rate. Also used for the knob's text.
inline CrunchSettings crunchSettings (float amount) noexcept
{
    const float c = std::clamp (amount, 0.0f, 1.0f);
    CrunchSettings s;
    s.bits = 16.0f - 10.0f * c;
    constexpr float knee = 0.4f; // 12-bit, 26 kHz
    s.rateHz = c <= knee ? 46000.0f * std::pow (26000.0f / 46000.0f, c / knee)
                         : 26000.0f * std::pow (5000.0f / 26000.0f, (c - knee) / (1.0f - knee));
    return s;
}

// "12-bit 26k", for the knob. Off below 0.5 %.
inline void crunchText (float amount, char* out, std::size_t size) noexcept
{
    if (amount < 0.005f)
    {
        std::snprintf (out, size, "Off");
        return;
    }
    const auto s = crunchSettings (amount);
    std::snprintf (out, size, "%d-bit %dk", static_cast<int> (std::lround (s.bits)), static_cast<int> (std::lround (s.rateHz / 1000.0f)));
}

class Crunch
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        amountSmooth.prepare (sampleRate / kControlBlock, 40.0f);
        reset();
    }

    void reset() noexcept
    {
        phase = 1.0;
        for (auto& h : held) h = 0.0f;
        for (auto& f : inputFilter) f.reset();
        for (auto& f : outputFilter) f.reset();
        amountSmooth.snapTo (target);
        appliedRate = -1.0f;
        countdown = 0;
    }

    void setAmount (float amount01) noexcept { target = std::clamp (amount01, 0.0f, 1.0f); }

    void process (float& l, float& r) noexcept
    {
        if (--countdown < 0)
        {
            update();
            countdown = kControlBlock - 1;
        }
        if (wet <= 0.0f)
            return;

        // Sample-and-hold at the sampler's rate.
        phase += step;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            held[0] = quantise (inputFilter[0].process (l));
            held[1] = quantise (inputFilter[1].process (r));
        }
        else
        {
            // Keep the input filters running between samples.
            (void) inputFilter[0].process (l);
            (void) inputFilter[1].process (r);
        }

        const float cl = outputFilter[0].process (held[0]);
        const float cr = outputFilter[1].process (held[1]);
        l += wet * (cl - l);
        r += wet * (cr - r);
    }

private:
    static constexpr int kControlBlock = 32;

    float quantise (float x) const noexcept
    {
        return std::round (x * levels) / levels;
    }

    void update() noexcept
    {
        amountSmooth.setTarget (target);
        const float amount = amountSmooth.skip (kControlBlock);
        // Fade in over the first 5 % so turning it on doesn't click.
        wet = std::clamp (amount / 0.05f, 0.0f, 1.0f);
        if (amount < 1.0e-4f)
        {
            wet = 0.0f;
            return;
        }

        const auto s = crunchSettings (amount);
        levels = std::pow (2.0f, s.bits - 1.0f);
        const float rate = std::min (s.rateHz, static_cast<float> (fs));
        step = static_cast<double> (rate) / fs;

        if (std::abs (rate - appliedRate) > 1.0f)
        {
            appliedRate = rate;
            const float nyquist = static_cast<float> (fs) * 0.45f;
            // The input filter lets a little aliasing through; the output one is the
            // machine's dull reconstruction filter.
            for (auto& f : inputFilter) f.setLowpass (std::min (nyquist, rate * 0.55f), 0.6f, fs);
            for (auto& f : outputFilter) f.setLowpass (std::min (nyquist, rate * 0.42f), 0.7071f, fs);
        }
    }

    double fs = 48000.0, phase = 1.0, step = 1.0;
    float target = 0.0f, wet = 0.0f, levels = 32768.0f, appliedRate = -1.0f;
    int countdown = 0;
    Smoother amountSmooth;
    std::array<float, 2> held {};
    std::array<Biquad, 2> inputFilter, outputFilter;
};

} // namespace contrail::dsp
