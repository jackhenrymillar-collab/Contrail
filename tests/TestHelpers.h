#pragma once

#include "dsp/TapeEngine.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace contrail::test
{

inline std::vector<float> sine (double hz, double sampleRate, std::size_t n, float amplitude = 0.5f)
{
    std::vector<float> x (n);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = amplitude * static_cast<float> (std::sin (2.0 * std::numbers::pi * hz * static_cast<double> (i) / sampleRate));
    return x;
}

inline double rms (const float* x, std::size_t n)
{
    double e = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        e += static_cast<double> (x[i]) * x[i];
    return std::sqrt (e / static_cast<double> (std::max<std::size_t> (1, n)));
}

inline double rms (const std::vector<float>& x, std::size_t from = 0)
{
    return from >= x.size() ? 0.0 : rms (x.data() + from, x.size() - from);
}

inline double peak (const std::vector<float>& x, std::size_t from = 0)
{
    double p = 0.0;
    for (std::size_t i = from; i < x.size(); ++i)
        p = std::max (p, static_cast<double> (std::abs (x[i])));
    return p;
}

inline double db (double gain) { return 20.0 * std::log10 (std::max (gain, 1.0e-12)); }

inline bool allFinite (const std::vector<float>& x)
{
    return std::all_of (x.begin(), x.end(), [] (float v) { return std::isfinite (v); });
}

// Amplitude of one frequency in a signal (single-bin DFT, Hann windowed).
inline double toneAmplitude (const std::vector<float>& x, double hz, double sampleRate, std::size_t from = 0)
{
    std::complex<double> acc = 0.0;
    double wsum = 0.0;
    const std::size_t n = x.size() - from;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (i) / static_cast<double> (n - 1));
        const double ph = -2.0 * std::numbers::pi * hz * static_cast<double> (i) / sampleRate;
        acc += w * static_cast<double> (x[from + i]) * std::complex<double> (std::cos (ph), std::sin (ph));
        wsum += w;
    }
    return 2.0 * std::abs (acc) / wsum;
}

// Pink-ish noise (Paul Kellet's economy filter), deterministic.
inline std::vector<float> pinkNoise (std::size_t n, float rmsLevel, std::uint32_t seed = 1234u)
{
    dsp::Random rng (seed);
    std::vector<float> x (n);
    double b0 = 0, b1 = 0, b2 = 0;
    for (auto& v : x)
    {
        const double w = rng.bipolar();
        b0 = 0.99765 * b0 + w * 0.0990460;
        b1 = 0.96300 * b1 + w * 0.2965164;
        b2 = 0.57000 * b2 + w * 1.0526913;
        v = static_cast<float> (b0 + b1 + b2 + w * 0.1848);
    }
    const double r = rms (x);
    for (auto& v : x)
        v = static_cast<float> (v * rmsLevel / r);
    return x;
}

// Runs a stereo signal through an engine in blocks of the given size.
inline void run (dsp::TapeEngine& engine, std::vector<float>& l, std::vector<float>& r, int blockSize = 256)
{
    for (std::size_t pos = 0; pos < l.size(); pos += static_cast<std::size_t> (blockSize))
    {
        const int n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (blockSize), l.size() - pos));
        engine.process (l.data() + pos, r.data() + pos, n);
    }
}

// Engine settings with every movement and noise turned off: a clean, static tape.
inline dsp::EngineParams cleanParams (int format = 0)
{
    dsp::EngineParams p;
    p.format = format;
    p.wowPercent = 0.0f;
    p.flutterPercent = 0.0f;
    p.drift = 0.0f;
    p.age = 0.0f;
    p.noise = false;
    p.driveDb = -6.0f;
    p.tone = 0.0f;
    p.mix = 1.0f;
    return p;
}

// Engine settings for a format's factory knobs.
inline dsp::EngineParams formatParams (int format, bool noise = false)
{
    const auto& k = dsp::kFormatDefaults[static_cast<std::size_t> (format)];
    dsp::EngineParams p;
    p.format = format;
    p.wowPercent = k.wowPercent;
    p.flutterPercent = k.flutterPercent;
    p.drift = k.drift * 0.01f;
    p.driveDb = k.driveDb;
    p.age = k.age * 0.01f;
    p.tone = k.tone * 0.01f;
    p.mix = k.mix * 0.01f;
    p.noise = noise;
    return p;
}

inline dsp::TapeEngine& prepared (dsp::TapeEngine& e, const dsp::EngineParams& p, double fs = 48000.0)
{
    e.setParams (p);
    e.prepare (fs, 512);
    return e;
}

} // namespace contrail::test
