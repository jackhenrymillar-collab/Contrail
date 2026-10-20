#pragma once

// Linear-phase oversampling for the hysteresis stage, so its harmonics don't fold back
// as aliasing. Cascaded 2x halfband FIR stages (Kaiser-windowed sinc), run as
// polyphase filters so the zero taps cost nothing.
//
// Latency is a whole number of samples at the base rate, so the dry signal can be
// lined up exactly.

#include <cmath>
#include <algorithm>
#include <cstddef>
#include <numbers>
#include <vector>

namespace contrail::dsp
{

namespace detail
{
    inline double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }

    inline double kaiser (std::size_t i, std::size_t n, double beta)
    {
        const double r = 2.0 * static_cast<double> (i) / static_cast<double> (n - 1) - 1.0;
        return besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
    }
}

// One 2x halfband stage. numTaps must be 4m + 3, so the centre tap is at an odd index
// and every other tap (apart from the centre) is zero.
class HalfbandStage
{
public:
    void prepare (std::size_t numTaps, double beta)
    {
        n = numTaps;
        centre = (n - 1) / 2;
        // The non-zero taps sit at even indices (centre is odd).
        evenTaps.assign ((n + 1) / 2, 0.0f);
        double sum = 0.0;
        for (std::size_t j = 0; j < evenTaps.size(); ++j)
        {
            const std::size_t i = 2 * j;
            const double x = (static_cast<double> (i) - static_cast<double> (centre)) / 2.0;
            const double sinc = std::sin (std::numbers::pi * x) / (std::numbers::pi * x);
            const double h = 0.5 * sinc * detail::kaiser (i, n, beta);
            evenTaps[j] = static_cast<float> (h);
            sum += h;
        }
        for (auto& t : evenTaps) // the even taps must sum to exactly one half
            t = static_cast<float> (t * 0.5 / sum);

        upHistory.assign (evenTaps.size() * 2, 0.0f);
        downEven.assign (evenTaps.size() * 2, 0.0f);
        downOdd.assign (centre + 2, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill (upHistory.begin(), upHistory.end(), 0.0f);
        std::fill (downEven.begin(), downEven.end(), 0.0f);
        std::fill (downOdd.begin(), downOdd.end(), 0.0f);
        upPos = downPos = oddPos = 0;
    }

    // Delay in samples at the higher rate, for one up and one down pass together.
    std::size_t roundTripLatency() const noexcept { return n - 1; }

    // One input sample in, two output samples out.
    void up (float x, float& y0, float& y1) noexcept
    {
        const std::size_t len = evenTaps.size();
        upPos = upPos == 0 ? len - 1 : upPos - 1;
        upHistory[upPos] = x;
        upHistory[upPos + len] = x; // mirrored copy: reads never wrap
        const float* hist = &upHistory[upPos];
        float acc = 0.0f;
        for (std::size_t j = 0; j < len; ++j)
            acc += evenTaps[j] * hist[j];
        y0 = 2.0f * acc;
        // The odd phase is just the centre tap (0.5, times the zero-stuffing gain of 2).
        y1 = hist[(centre - 1) / 2];
    }

    // Two input samples in (x0 first), one output sample out, lined up with x0.
    float down (float x0, float x1) noexcept
    {
        const std::size_t len = evenTaps.size();
        downPos = downPos == 0 ? len - 1 : downPos - 1;
        downEven[downPos] = x0;
        downEven[downPos + len] = x0;
        const float* hist = &downEven[downPos];
        float acc = 0.0f;
        for (std::size_t j = 0; j < len; ++j)
            acc += evenTaps[j] * hist[j];

        // Odd-phase samples (x1) only meet the centre tap, (centre + 1) / 2 pairs ago.
        const std::size_t oddLen = downOdd.size();
        const std::size_t back = (centre + 1) / 2;
        const float centreSample = downOdd[(oddPos + oddLen - back) % oddLen];
        downOdd[oddPos] = x1;
        oddPos = (oddPos + 1) % oddLen;
        return acc + 0.5f * centreSample;
    }

private:
    std::size_t n = 3, centre = 1;
    std::vector<float> evenTaps, upHistory, downEven, downOdd;
    std::size_t upPos = 0, downPos = 0, oddPos = 0;
};

// 1x, 2x or 4x oversampling around a per-sample function.
class Oversampler
{
public:
    static constexpr int kMaxFactor = 4;

    // Picks the factor so the inner rate is about 176 to 192 kHz.
    static int factorFor (double sampleRate) noexcept
    {
        return sampleRate <= 50000.0 ? 4 : (sampleRate <= 100000.0 ? 2 : 1);
    }

    void prepare (int oversamplingFactor)
    {
        factor = oversamplingFactor;
        stage1.prepare (63, 8.0);
        stage2.prepare (19, 7.0);
        pad[0] = pad[1] = 0.0f;
    }

    void reset()
    {
        stage1.reset();
        stage2.reset();
        pad[0] = pad[1] = 0.0f;
    }

    int getFactor() const noexcept { return factor; }

    // Latency at the base sample rate.
    int latency() const noexcept
    {
        if (factor == 1)
            return 0;
        const auto l1 = static_cast<int> (stage1.roundTripLatency()) / 2; // 31
        if (factor == 2)
            return l1;
        // Stage 2's 18 samples at 4x are 4.5 base samples; two extra samples at 4x
        // (see process) make that a whole 5.
        return l1 + (static_cast<int> (stage2.roundTripLatency()) + 2) / 4;
    }

    template <typename Fn>
    float process (float x, Fn&& fn) noexcept
    {
        if (factor == 1)
            return fn (x);

        float a0, a1;
        stage1.up (x, a0, a1);
        if (factor == 2)
        {
            // Separate statements: argument evaluation order is unspecified in C++.
            const float y0 = fn (a0);
            const float y1 = fn (a1);
            return stage1.down (y0, y1);
        }

        float b[4];
        stage2.up (a0, b[0], b[1]);
        stage2.up (a1, b[2], b[3]);
        for (auto& v : b)
        {
            // Two-sample delay at 4x to make the total latency whole.
            const float y = fn (v);
            v = pad[1];
            pad[1] = pad[0];
            pad[0] = y;
        }
        const float c0 = stage2.down (b[0], b[1]);
        const float c1 = stage2.down (b[2], b[3]);
        return stage1.down (c0, c1);
    }

private:
    int factor = 1;
    HalfbandStage stage1, stage2;
    float pad[2] {};
};

} // namespace contrail::dsp
