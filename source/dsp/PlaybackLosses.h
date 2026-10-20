#pragma once

// Playback losses, after H. N. Bertram, "Theory of Magnetic Recording" (1994):
//
//   spacing loss  exp(-2 pi d / lambda)            d = head-to-tape spacing
//   gap loss      sin(pi g / lambda) / (pi g / lambda)   g = playback head gap
//
// where lambda = v / f is the recorded wavelength at tape speed v. A machine's playback
// EQ makes up for the losses of a new, clean tape, so only the spacing that Age adds
// (worn oxide, dirt, a tape that no longer sits flat) counts here, on top of the gap
// loss and the format's own bandwidth. Slower formats lose more top end for the same
// wear, as real tape does.
//
// The response is built as a linear-phase FIR (windowed frequency sampling), so it has
// a fixed, whole-sample latency that the dry signal is lined up with.

#include "Blocks.h"
#include "Formats.h"

#include <cmath>
#include <array>
#include <numbers>
#include <vector>

namespace contrail::dsp
{

class PlaybackLosses
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        // Keep the filter about 1.3 ms long at any rate, so it resolves the same detail.
        numTaps = sampleRate <= 50000.0 ? 63 : (sampleRate <= 100000.0 ? 127 : 255);
        centre = (numTaps - 1) / 2;
        grid = numTaps + 1;

        cosTable.assign (static_cast<std::size_t> ((centre + 1) * (grid + 1)), 0.0f);
        for (int m = 0; m <= centre; ++m)
            for (int k = 0; k <= grid; ++k)
                cosTable[static_cast<std::size_t> (m * (grid + 1) + k)]
                    = static_cast<float> (std::cos (std::numbers::pi * k * m / grid));
        window.assign (static_cast<std::size_t> (centre + 1), 0.0f);
        for (int m = 0; m <= centre; ++m) // Blackman, by distance from the centre tap
        {
            const double x = std::numbers::pi * m / (centre + 1);
            window[static_cast<std::size_t> (m)] = static_cast<float> (0.42 + 0.5 * std::cos (x) + 0.08 * std::cos (2.0 * x));
        }
        target.assign (static_cast<std::size_t> (grid + 1), 1.0f);
        half.assign (static_cast<std::size_t> (centre + 1), 0.0f);
        for (auto& h : history)
            h.assign (static_cast<std::size_t> (numTaps * 2), 0.0f);
        lastSpeed = lastGap = lastSpacing = lastBw = lastOrder = -1.0f;
        reset();
    }

    void reset()
    {
        for (auto& h : history)
            std::fill (h.begin(), h.end(), 0.0f);
        pos = 0;
    }

    int latency() const noexcept { return centre; }

    // Magnitude of the target response (before windowing) at one frequency.
    static double response (double hz, float speedIps, float gapUm, float spacingUm, float bandwidthHz, float order) noexcept
    {
        const double v = std::max (0.001, static_cast<double> (speedIps) * 0.0254);
        const double spacing = std::exp (-2.0 * std::numbers::pi * spacingUm * 1.0e-6 * hz / v);
        const double x = gapUm * 1.0e-6 * hz / v;
        const double gap = x < 1.0e-9 ? 1.0 : (x < 1.0 ? std::sin (std::numbers::pi * x) / (std::numbers::pi * x) : 0.0);
        const double bw = 1.0 / std::sqrt (1.0 + std::pow (hz / std::max (100.0f, bandwidthHz), 2.0 * order));
        return spacing * gap * bw;
    }

    // Redesigns the filter if the settings moved. Cheap enough to call every control block.
    void update (const FormatProfile& f, float age) noexcept
    {
        const float spacing = f.ageSpacingUm * age * age;
        if (std::abs (f.speedIps - lastSpeed) < 1.0e-4f && std::abs (f.gapUm - lastGap) < 1.0e-4f
            && std::abs (spacing - lastSpacing) < 1.0e-4f && std::abs (f.bandwidthHz - lastBw) < 1.0f
            && std::abs (f.bandwidthOrder - lastOrder) < 1.0e-3f)
            return;
        lastSpeed = f.speedIps;
        lastGap = f.gapUm;
        lastSpacing = spacing;
        lastBw = f.bandwidthHz;
        lastOrder = f.bandwidthOrder;

        for (int k = 0; k <= grid; ++k)
        {
            const double hz = 0.5 * fs * k / grid;
            target[static_cast<std::size_t> (k)] = static_cast<float> (response (hz, f.speedIps, f.gapUm, spacing, f.bandwidthHz, f.bandwidthOrder));
        }

        // h[m] = (1/pi) * integral over 0..pi of H(w) cos(w m), by the trapezoid rule.
        double sum = 0.0;
        for (int m = 0; m <= centre; ++m)
        {
            const float* c = &cosTable[static_cast<std::size_t> (m * (grid + 1))];
            double acc = 0.5 * (target[0] * c[0] + target[static_cast<std::size_t> (grid)] * c[grid]);
            for (int k = 1; k < grid; ++k)
                acc += target[static_cast<std::size_t> (k)] * c[k];
            const double h = acc / grid * window[static_cast<std::size_t> (m)];
            half[static_cast<std::size_t> (m)] = static_cast<float> (h);
            sum += m == 0 ? h : 2.0 * h;
        }
        // Unity at DC, whatever the windowing did.
        const double dc = target[0];
        for (auto& h : half)
            h = static_cast<float> (h * dc / sum);
    }

    void process (float& l, float& r) noexcept
    {
        pos = pos == 0 ? numTaps - 1 : pos - 1;
        l = run (history[0], l);
        r = run (history[1], r);
    }

    // Response of the designed filter at one frequency (for tests).
    double designedMagnitude (double hz) const noexcept
    {
        const double w = 2.0 * std::numbers::pi * hz / fs;
        double acc = half[0];
        for (int m = 1; m <= centre; ++m)
            acc += 2.0 * half[static_cast<std::size_t> (m)] * std::cos (w * m);
        return std::abs (acc);
    }

private:
    float run (std::vector<float>& h, float x) noexcept
    {
        const auto n = static_cast<std::size_t> (numTaps);
        h[static_cast<std::size_t> (pos)] = x;
        h[static_cast<std::size_t> (pos) + n] = x;
        const float* d = &h[static_cast<std::size_t> (pos)]; // d[0] newest ... d[numTaps-1] oldest
        float acc = half[0] * d[centre];
        for (int m = 1; m <= centre; ++m)
            acc += half[static_cast<std::size_t> (m)] * (d[centre - m] + d[centre + m]);
        return acc;
    }

    double fs = 48000.0;
    int numTaps = 63, centre = 31, grid = 64, pos = 0;
    std::vector<float> cosTable, window, target, half;
    std::array<std::vector<float>, 2> history;
    float lastSpeed = -1.0f, lastGap = -1.0f, lastSpacing = -1.0f, lastBw = -1.0f, lastOrder = -1.0f;
};

} // namespace contrail::dsp
