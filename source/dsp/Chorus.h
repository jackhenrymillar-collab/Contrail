#pragma once

// Vintage Chorus: the bucket-brigade chorus of early-80s polysynths and pedals.
//
// Each side runs through a short delay swept by a slow triangle wave, the two sides in
// opposite directions, so the sound spreads and shimmers. The delayed copy is darkened
// and gently rounded, as a bucket-brigade chip does, and carries a faint hiss (with the
// Noise switch). One knob: low settings are a slow, subtle shimmer; high settings a
// faster, deeper wobble.

#include "Blocks.h"

#include <array>
#include <cmath>
#include <vector>

namespace contrail::dsp
{

inline const char* chorusWord (float amount01) noexcept
{
    return amount01 < 0.005f ? "Off" : amount01 < 0.25f ? "Subtle" : amount01 < 0.5f ? "Lush"
         : amount01 < 0.75f ? "Deep" : "Seasick";
}

class Chorus
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const auto size = static_cast<std::size_t> (std::ceil (kMaxDelayMs * 0.001 * sampleRate)) + 8;
        for (auto& line : lines)
            line.assign (size, 0.0f);
        amountSmooth.prepare (sampleRate, 40.0f);
        for (auto& f : darken) f.setLowpass (kBbdToneHz, 0.6f, sampleRate);
        for (auto& f : darken2) f.setLowpass (kBbdToneHz * 1.3f, 0.6f, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        for (auto& line : lines)
            std::fill (line.begin(), line.end(), 0.0f);
        pos = 0;
        lfo = 0.0;
        for (auto& f : darken) f.reset();
        for (auto& f : darken2) f.reset();
        amountSmooth.snapTo (target);
    }

    void setAmount (float amount01) noexcept { target = std::clamp (amount01, 0.0f, 1.0f); }
    void setNoise (bool on) noexcept { noiseOn = on; }

    void process (float& l, float& r) noexcept
    {
        const float amount = amountSmooth.next();
        const auto size = static_cast<int> (lines[0].size());
        lines[0][static_cast<std::size_t> (pos)] = l;
        lines[1][static_cast<std::size_t> (pos)] = r;
        if (amount < 1.0e-4f && target < 1.0e-4f)
        {
            pos = pos + 1 >= size ? 0 : pos + 1; // keep the line current so it starts cleanly
            return;
        }

        // Slow at low settings, a little faster and much deeper at high ones.
        const double rate = 0.45 + 0.5 * amount;
        lfo += rate / fs;
        if (lfo >= 1.0)
            lfo -= 1.0;
        const float tri = static_cast<float> (4.0 * std::abs (lfo - 0.5) - 1.0); // -1..1
        const float swingMs = 0.4f + 1.8f * amount;
        const float centreMs = 3.6f;
        const float msToSamples = static_cast<float> (fs * 0.001);

        const float dl = (centreMs + swingMs * tri) * msToSamples;
        const float dr = (centreMs - swingMs * tri) * msToSamples;
        float wl = read (0, dl, size);
        float wr = read (1, dr, size);

        // The bucket-brigade copy: darker, softly rounded, a little hiss.
        wl = std::tanh (1.4f * darken2[0].process (darken[0].process (wl))) / 1.4f;
        wr = std::tanh (1.4f * darken2[1].process (darken[1].process (wr))) / 1.4f;
        if (noiseOn)
        {
            wl += kHiss * random.gaussian();
            wr += kHiss * random.gaussian();
        }

        pos = pos + 1 >= size ? 0 : pos + 1;

        // Dry plus the chorus copy at full, faded in over the first 15 %. The two are
        // mostly unrelated, so the sum is scaled to keep the level about where it was
        // (the extra 18 % makes up for the darker copy).
        const float wet = std::clamp (amount / 0.15f, 0.0f, 1.0f);
        const float g = (1.0f + 0.18f * wet) / std::sqrt (1.0f + wet * wet);
        l = g * (l + wet * wl);
        r = g * (r + wet * wr);
    }

private:
    static constexpr float kMaxDelayMs = 7.0f;
    static constexpr float kBbdToneHz = 8000.0f;
    static constexpr float kHiss = 0.00012f; // about -78 dBFS

    // Cubic (Hermite) read, `delay` samples behind the newest.
    float read (std::size_t ch, float delay, int size) const noexcept
    {
        const auto& line = lines[ch];
        const float readPos = static_cast<float> (pos) - delay;
        const int i = static_cast<int> (std::floor (readPos));
        const float f = readPos - static_cast<float> (i);
        auto at = [&] (int k) {
            int idx = (i + k) % size;
            if (idx < 0) idx += size;
            return line[static_cast<std::size_t> (idx)];
        };
        const float xm1 = at (-1), x0 = at (0), x1 = at (1), x2 = at (2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    double fs = 48000.0, lfo = 0.0;
    std::array<std::vector<float>, 2> lines;
    int pos = 0;
    float target = 0.0f;
    bool noiseOn = true;
    Smoother amountSmooth;
    std::array<Biquad, 2> darken, darken2;
    Random random;
};

} // namespace contrail::dsp
