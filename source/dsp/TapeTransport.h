#pragma once

// The tape path between the record and playback heads: a delay line read by a moving
// head, plus the motor that can stop and start the tape.
//
// - Wow and flutter move the read head (an offset from WowFlutter).
// - Tape Stop slows the transport to a halt along a smooth curve; Tape Start brings it
//   back up. While the tape slows, the read head falls behind and the pitch drops, and
//   the playback level falls with speed, as a real playback head's output does.
// - Once back at speed, the read head is still behind; it splices back to its normal
//   place with a short equal-power crossfade.
//
// Reads use a 16-tap Kaiser-windowed sinc interpolator with 256 phases, so the moving
// head stays clean right up to the top of the audio band.

#include "Blocks.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <algorithm>
#include <numbers>
#include <vector>

namespace contrail::dsp
{

class SincTable
{
public:
    static constexpr int kTaps = 16;
    static constexpr int kPhases = 256;

    SincTable()
    {
        constexpr double beta = 7.5;
        auto i0 = [] (double x) {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 40; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        };
        const double norm = i0 (beta);
        for (int p = 0; p <= kPhases; ++p)
        {
            const double frac = static_cast<double> (p) / kPhases;
            double sum = 0.0;
            std::array<double, kTaps> row {};
            for (int t = 0; t < kTaps; ++t)
            {
                // Tap t sits at offset (t - kTaps/2 + 1) - frac from the read point.
                const double x = static_cast<double> (t - kTaps / 2 + 1) - frac;
                const double sinc = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (std::numbers::pi * x) / (std::numbers::pi * x);
                const double r = x / (kTaps / 2.0);
                const double w = std::abs (r) >= 1.0 ? 0.0 : i0 (beta * std::sqrt (1.0 - r * r)) / norm;
                row[static_cast<std::size_t> (t)] = sinc * w;
                sum += row[static_cast<std::size_t> (t)];
            }
            for (int t = 0; t < kTaps; ++t)
                table[static_cast<std::size_t> (p * kTaps + t)] = static_cast<float> (row[static_cast<std::size_t> (t)] / sum);
        }
    }

    const float* row (int phase) const noexcept { return &table[static_cast<std::size_t> (phase * kTaps)]; }

private:
    std::array<float, (kPhases + 1) * kTaps> table {};
};

inline const SincTable& sincTable()
{
    static const SincTable table;
    return table;
}

// Delay line with a fractional, band-limited read.
class InterpolatedDelay
{
public:
    void prepare (std::size_t maxDelaySamples)
    {
        std::size_t size = 1;
        while (size < maxDelaySamples + SincTable::kTaps + 4)
            size <<= 1;
        buffer.assign (size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); writePos = 0; }

    void push (float x) noexcept
    {
        buffer[writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    // The signal `delay` samples ago (0 = the newest sample), band-limited
    // interpolation. delay must be at least kMinDelay.
    float read (double delay) const noexcept
    {
        const auto& sinc = sincTable();
        const double whole = std::floor (delay);
        const double phasePos = (delay - whole) * SincTable::kPhases;
        const int phase = std::min (static_cast<int> (phasePos), SincTable::kPhases - 1);
        const float pf = static_cast<float> (phasePos - phase);
        const float* r0 = sinc.row (phase);
        const float* r1 = sinc.row (phase + 1);
        // Tap t reads the sample (whole - kTaps/2 + 1 + t) ago.
        const std::size_t first = writePos - 1 - (static_cast<std::size_t> (whole) - (SincTable::kTaps / 2 - 1));
        float acc = 0.0f;
        for (int t = 0; t < SincTable::kTaps; ++t)
        {
            const float coeff = r0[t] + pf * (r1[t] - r0[t]);
            acc += coeff * buffer[(first - static_cast<std::size_t> (t)) & mask];
        }
        return acc;
    }

    static constexpr double kMinDelay = SincTable::kTaps / 2 + 1;

private:
    std::vector<float> buffer;
    std::size_t mask = 0;
    std::size_t writePos = 0;
};

class TapeTransport
{
public:
    void prepare (double sampleRate, double baseDelaySamples, double maxSeconds = 4.0)
    {
        fs = sampleRate;
        base = baseDelaySamples;
        maxLag = maxSeconds * sampleRate - base - 64.0;
        for (auto& l : lines)
            l.prepare (static_cast<std::size_t> (maxSeconds * sampleRate) + 64);
        spliceStep = 1.0f / static_cast<float> (0.06 * sampleRate);
        reset();
    }

    void reset()
    {
        for (auto& l : lines)
            l.reset();
        u = 0.0;
        lag = 0.0;
        splice = 1.0f;
        spliceFrom = 0.0;
        stopping = false;
    }

    void setTimes (float stopSeconds, float startSeconds) noexcept
    {
        stopInc = 1.0 / (std::max (0.05f, stopSeconds) * fs);
        startInc = 1.0 / (std::max (0.05f, startSeconds) * fs);
    }

    void setStopped (bool shouldStop) noexcept
    {
        stopping = shouldStop;
    }

    bool isStopping() const noexcept { return stopping; }

    // Transport speed: 1 at normal speed, 0 when halted.
    float speed() const noexcept { return currentSpeed; }

    // One sample in per channel; offsets are the wow and flutter read-head offsets.
    void process (float inL, float inR, float offsetL, float offsetR, float& outL, float& outR) noexcept
    {
        lines[0].push (inL);
        lines[1].push (inR);

        // Motor: u runs 0 (at speed) to 1 (halted); speed follows a cosine curve so it
        // eases out of normal speed and settles gently into the stop.
        if (stopping)
            u = std::min (1.0, u + stopInc);
        else if (u > 0.0)
        {
            if (u >= 1.0)
                lag = 0.0; // restarting from a halt: the head can jump while nothing plays
            u = std::max (0.0, u - startInc);
        }
        const double s = 0.5 * (1.0 + std::cos (std::numbers::pi * u));
        currentSpeed = static_cast<float> (s);

        // A slow tape lets the read head fall behind.
        if (u > 0.0)
            lag = std::min (maxLag, lag + (1.0 - s));
        else if (lag > 0.0 && splice >= 1.0f)
        {
            // Back at speed but still behind: splice back to the normal place.
            spliceFrom = lag;
            lag = 0.0;
            splice = 0.0f;
        }

        // Wow scales with speed: a stopped tape doesn't wobble.
        const double oL = static_cast<double> (offsetL) * s;
        const double oR = static_cast<double> (offsetR) * s;
        const double dL = std::max (InterpolatedDelay::kMinDelay, base + lag + oL);
        const double dR = std::max (InterpolatedDelay::kMinDelay, base + lag + oR);
        outL = lines[0].read (dL);
        outR = lines[1].read (dR);

        if (splice < 1.0f)
        {
            const double fL = std::max (InterpolatedDelay::kMinDelay, base + spliceFrom + oL);
            const double fR = std::max (InterpolatedDelay::kMinDelay, base + spliceFrom + oR);
            const float gIn = std::sin (0.5f * kPi * splice);
            const float gOut = std::cos (0.5f * kPi * splice);
            outL = gIn * outL + gOut * lines[0].read (fL);
            outR = gIn * outR + gOut * lines[1].read (fR);
            splice = std::min (1.0f, splice + spliceStep);
        }

        // The playback head's output is proportional to tape speed.
        if (u > 0.0)
        {
            outL *= currentSpeed;
            outR *= currentSpeed;
        }
    }

    double lagSamples() const noexcept { return lag; }

private:
    double fs = 48000.0, base = 1000.0, maxLag = 100000.0;
    double u = 0.0, lag = 0.0, spliceFrom = 0.0;
    double stopInc = 1.0e-5, startInc = 2.0e-5;
    float currentSpeed = 1.0f, splice = 1.0f, spliceStep = 0.001f;
    bool stopping = false;
    std::array<InterpolatedDelay, 2> lines;
};

} // namespace contrail::dsp
