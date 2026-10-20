#pragma once

// Small DSP building blocks used throughout Contrail.
// Plain C++ with no framework dependencies, so they can be unit tested anywhere.
// Nothing here allocates after prepare().

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace contrail::dsp
{

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTwoPi = 2.0f * kPi;

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (std::max (g, 1.0e-12f)); }

// Small, fast random generator (xorshift32). Deterministic for a given seed, so tests
// and previews render the same every time.
class Random
{
public:
    explicit Random (std::uint32_t seed = 0x12345678u) noexcept { setSeed (seed); }

    void setSeed (std::uint32_t seed) noexcept
    {
        state = seed != 0 ? seed : 0x9E3779B9u;
        for (int i = 0; i < 4; ++i)
            nextInt();
    }

    std::uint32_t nextInt() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    // Uniform in [0, 1).
    float unit() noexcept { return static_cast<float> (nextInt() >> 8) * (1.0f / 16777216.0f); }

    // Uniform in [-1, 1).
    float bipolar() noexcept { return 2.0f * unit() - 1.0f; }

    // Roughly Gaussian with unit variance (sum of four uniforms).
    float gaussian() noexcept
    {
        const float s = unit() + unit() + unit() + unit();
        return (s - 2.0f) * 1.7320508f; // variance of the sum is 1/3
    }

private:
    std::uint32_t state = 1u;
};

// Exponential parameter smoother. State is kept in double so it reaches the target
// exactly instead of stalling a few float steps short.
class Smoother
{
public:
    void prepare (double sampleRate, float timeMs)
    {
        coeff = std::exp (-1.0 / (static_cast<double> (timeMs) * 0.001 * sampleRate));
    }

    void setTarget (float t) noexcept { target = t; }
    void snapTo (float t) noexcept { target = t; current = t; }
    float getTarget() const noexcept { return static_cast<float> (target); }
    float getCurrent() const noexcept { return static_cast<float> (current); }
    bool isSettled() const noexcept { return ! (current < target) && ! (current > target); }

    float next() noexcept
    {
        current = target + coeff * (current - target);
        if (std::abs (current - target) < 1.0e-9)
            current = target;
        return static_cast<float> (current);
    }

    // Advance several samples at once (for values updated per block).
    float skip (int numSamples) noexcept
    {
        current = target + std::pow (coeff, numSamples) * (current - target);
        if (std::abs (current - target) < 1.0e-9)
            current = target;
        return static_cast<float> (current);
    }

private:
    double coeff = 0.0;
    double current = 0.0;
    double target = 0.0;
};

// One-pole lowpass: y = (1 - a) x + a y[n-1], a = exp(-2 pi fc / fs).
class OnePoleLowpass
{
public:
    static float coefficientFor (float cutoffHz, double sampleRate) noexcept
    {
        return std::exp (-kTwoPi * cutoffHz / static_cast<float> (sampleRate));
    }

    void reset (float value = 0.0f) noexcept { state = value; }

    float process (float x, float a) noexcept
    {
        state = x + a * (state - x);
        return state;
    }

    float value() const noexcept { return state; }

private:
    float state = 0.0f;
};

// DC blocker: y = x - x[n-1] + r y[n-1]. About 5 Hz at any sample rate.
class DcBlocker
{
public:
    void prepare (double sampleRate) { r = 1.0f - kTwoPi * 5.0f / static_cast<float> (sampleRate); }
    void reset() noexcept { x1 = y1 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }

private:
    float r = 0.9993f, x1 = 0.0f, y1 = 0.0f;
};

// RBJ biquad (Audio EQ Cookbook), transposed direct form II.
class Biquad
{
public:
    void reset() noexcept { z1 = z2 = 0.0f; }

    void setIdentity() noexcept
    {
        b0 = 1.0f;
        b1 = b2 = a1 = a2 = 0.0f;
    }

    void setHighpass (float hz, float q, double sampleRate)
    {
        const auto [cosw, alpha] = prewarp (hz, q, sampleRate);
        set ((1.0 + cosw) / 2.0, -(1.0 + cosw), (1.0 + cosw) / 2.0, 1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
    }

    void setLowpass (float hz, float q, double sampleRate)
    {
        const auto [cosw, alpha] = prewarp (hz, q, sampleRate);
        set ((1.0 - cosw) / 2.0, 1.0 - cosw, (1.0 - cosw) / 2.0, 1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
    }

    void setBandpass (float hz, float q, double sampleRate)
    {
        const auto [cosw, alpha] = prewarp (hz, q, sampleRate);
        set (alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
    }

    void setPeak (float hz, float q, float gainDb, double sampleRate)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const auto [cosw, alpha] = prewarp (hz, q, sampleRate);
        set (1.0 + alpha * A, -2.0 * cosw, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * cosw, 1.0 - alpha / A);
    }

    void setHighShelf (float hz, float gainDb, double sampleRate)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const auto [cosw, alpha] = prewarp (hz, 0.7071f, sampleRate);
        const double sq = 2.0 * std::sqrt (A) * alpha;
        set (A * ((A + 1.0) + (A - 1.0) * cosw + sq),
             -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw),
             A * ((A + 1.0) + (A - 1.0) * cosw - sq),
             (A + 1.0) - (A - 1.0) * cosw + sq,
             2.0 * ((A - 1.0) - (A + 1.0) * cosw),
             (A + 1.0) - (A - 1.0) * cosw - sq);
    }

    void setLowShelf (float hz, float gainDb, double sampleRate)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const auto [cosw, alpha] = prewarp (hz, 0.7071f, sampleRate);
        const double sq = 2.0 * std::sqrt (A) * alpha;
        set (A * ((A + 1.0) - (A - 1.0) * cosw + sq),
             2.0 * A * ((A - 1.0) - (A + 1.0) * cosw),
             A * ((A + 1.0) - (A - 1.0) * cosw - sq),
             (A + 1.0) + (A - 1.0) * cosw + sq,
             -2.0 * ((A - 1.0) + (A + 1.0) * cosw),
             (A + 1.0) + (A - 1.0) * cosw - sq);
    }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    // Magnitude response at a frequency (for tests).
    double magnitudeAt (double hz, double sampleRate) const noexcept
    {
        const double w = 2.0 * std::numbers::pi * hz / sampleRate;
        const double cr = std::cos (w), ci = -std::sin (w), c2r = std::cos (2 * w), c2i = -std::sin (2 * w);
        const double nr = b0 + b1 * cr + b2 * c2r, ni = b1 * ci + b2 * c2i;
        const double dr = 1.0 + a1 * cr + a2 * c2r, di = a1 * ci + a2 * c2i;
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    }

private:
    struct Warp { double cosw, alpha; };

    static Warp prewarp (float hz, float q, double sampleRate) noexcept
    {
        const double f = std::clamp<double> (hz, 1.0, 0.49 * sampleRate);
        const double w0 = 2.0 * std::numbers::pi * f / sampleRate;
        return { std::cos (w0), std::sin (w0) / (2.0 * std::max (0.05f, q)) };
    }

    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        b0 = static_cast<float> (nb0 / na0);
        b1 = static_cast<float> (nb1 / na0);
        b2 = static_cast<float> (nb2 / na0);
        a1 = static_cast<float> (na1 / na0);
        a2 = static_cast<float> (na2 / na0);
    }

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

// Band-limited random motion: white noise through two one-pole lowpasses, scaled so the
// output has unit RMS. Used for the random parts of wow and flutter, so the movement
// never repeats.
class SmoothNoise
{
public:
    void prepare (double sampleRate, std::uint32_t seed)
    {
        fs = sampleRate;
        rng.setSeed (seed);
        reset();
    }

    void reset() noexcept { s1.reset(); s2.reset(); }

    void setCutoff (float hz) noexcept
    {
        a = OnePoleLowpass::coefficientFor (hz, fs);
        // Two cascaded one-poles have impulse response (1 - a)^2 (n + 1) a^n, so unit
        // white noise comes out with variance (1 - a)^4 (1 + a^2) / (1 - a^2)^3.
        const double ad = a;
        const double var = std::pow (1.0 - ad, 4.0) * (1.0 + ad * ad) / std::pow (1.0 - ad * ad, 3.0);
        norm = static_cast<float> (1.0 / std::sqrt (std::max (var, 1.0e-30)));
    }

    float next() noexcept
    {
        const float w = rng.gaussian();
        return s2.process (s1.process (w, a), a) * norm;
    }

private:
    double fs = 48000.0;
    Random rng;
    OnePoleLowpass s1, s2;
    float a = 0.99f, norm = 1.0f;
};

} // namespace contrail::dsp
