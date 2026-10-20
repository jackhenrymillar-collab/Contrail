// Renders audio previews: a short electric-piano clip through every format at its
// factory settings, plus a Tape Stop and Start demo. Development only.
//
//   contrail_render <out-dir>
//
// Writes dry.wav, one file per format, and tape-stop.wav (48 kHz, 16-bit stereo).

#include "dsp/TapeEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

using namespace contrail;

namespace
{
constexpr double kRate = 48000.0;

struct Stereo
{
    std::vector<float> l, r;
};

// Two-operator FM electric piano note with a soft attack and a long decay.
void addNote (Stereo& out, double start, double length, double midi, float level, float pan)
{
    const double f = 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
    const auto s0 = static_cast<std::size_t> (start * kRate);
    const auto n = static_cast<std::size_t> ((length + 1.5) * kRate);
    const float gl = std::cos (pan * std::numbers::pi_v<float> * 0.5f), gr = std::sin (pan * std::numbers::pi_v<float> * 0.5f);
    for (std::size_t i = 0; i < n && s0 + i < out.l.size(); ++i)
    {
        const double t = static_cast<double> (i) / kRate;
        const double release = t > length ? std::exp (-(t - length) * 6.0) : 1.0;
        const double env = (1.0 - std::exp (-t * 400.0)) * std::exp (-t * 1.1) * release;
        const double index = 1.6 * std::exp (-t * 5.0) + 0.2;
        const double mod = index * std::sin (2.0 * std::numbers::pi * f * t);
        const double tine = 0.15 * std::exp (-t * 14.0) * std::sin (2.0 * std::numbers::pi * f * 7.0 * t);
        const auto v = static_cast<float> (2.0 * level * env * (std::sin (2.0 * std::numbers::pi * f * t + mod) + tine));
        out.l[s0 + i] += gl * v;
        out.r[s0 + i] += gr * v;
    }
}

Stereo clip (double seconds)
{
    Stereo s;
    s.l.assign (static_cast<std::size_t> (seconds * kRate), 0.0f);
    s.r = s.l;
    // Cmaj9, Am9, Fmaj7, G13 at 84 bpm, one chord per bar, with a simple top line.
    const double bar = 60.0 / 84.0 * 4.0;
    const std::vector<std::vector<double>> chords {
        { 48, 55, 59, 62, 64 }, { 45, 52, 55, 59, 60 }, { 41, 48, 52, 55, 57 }, { 43, 50, 53, 57, 64 } };
    const std::vector<double> melody { 76, 74, 72, 71, 72, 69, 67, 69 };
    for (std::size_t c = 0; c < chords.size(); ++c)
    {
        const double t = static_cast<double> (c) * bar + 0.05;
        for (std::size_t k = 0; k < chords[c].size(); ++k)
            addNote (s, t + 0.012 * static_cast<double> (k), bar * 0.92, chords[c][k], k == 0 ? 0.16f : 0.09f,
                     0.25f + 0.5f * static_cast<float> (k) / static_cast<float> (chords[c].size()));
        addNote (s, t + bar * 0.5, bar * 0.45, melody[2 * c], 0.08f, 0.6f);
        addNote (s, t + bar * 0.75, bar * 0.6, melody[2 * c + 1], 0.08f, 0.6f);
    }
    return s;
}

void writeWav (const std::string& path, const Stereo& s)
{
    std::ofstream f (path, std::ios::binary);
    auto u32 = [&f] (std::uint32_t v) { f.write (reinterpret_cast<const char*> (&v), 4); };
    auto u16 = [&f] (std::uint16_t v) { f.write (reinterpret_cast<const char*> (&v), 2); };
    const auto frames = static_cast<std::uint32_t> (s.l.size());
    f.write ("RIFF", 4);
    u32 (36 + frames * 4);
    f.write ("WAVEfmt ", 8);
    u32 (16);
    u16 (1);
    u16 (2);
    u32 (static_cast<std::uint32_t> (kRate));
    u32 (static_cast<std::uint32_t> (kRate) * 4);
    u16 (4);
    u16 (16);
    f.write ("data", 4);
    u32 (frames * 4);
    for (std::size_t i = 0; i < s.l.size(); ++i)
        for (const float v : { s.l[i], s.r[i] })
        {
            const auto q = static_cast<std::int16_t> (std::lround (std::clamp (v, -1.0f, 1.0f) * 32767.0f));
            u16 (static_cast<std::uint16_t> (q));
        }
}

dsp::EngineParams factory (int format)
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
    return p;
}

// Runs the clip through an engine, taking out the latency so files line up.
Stereo process (const Stereo& in, dsp::EngineParams p, double stopAt = -1.0, double startAt = -1.0)
{
    dsp::TapeEngine engine;
    engine.setParams (p);
    engine.prepare (kRate, 512);
    const auto latency = static_cast<std::size_t> (engine.getLatencySamples());
    Stereo out = in;
    out.l.resize (in.l.size() + latency, 0.0f);
    out.r.resize (in.r.size() + latency, 0.0f);
    for (std::size_t pos = 0; pos < out.l.size(); pos += 256)
    {
        const double t = static_cast<double> (pos) / kRate;
        p.tapeStop = stopAt >= 0.0 && t >= stopAt && t < startAt;
        engine.setParams (p);
        engine.process (out.l.data() + pos, out.r.data() + pos, static_cast<int> (std::min<std::size_t> (256, out.l.size() - pos)));
    }
    out.l.erase (out.l.begin(), out.l.begin() + static_cast<std::ptrdiff_t> (latency));
    out.r.erase (out.r.begin(), out.r.begin() + static_cast<std::ptrdiff_t> (latency));
    return out;
}
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: contrail_render <out-dir>\n");
        return 1;
    }
    const std::string dir = argv[1];
    const char* names[dsp::kNumFormats] = { "1-studio-reel", "2-home-reel", "3-cassette", "4-worn-cassette", "5-vhs", "6-dictaphone" };

    const auto dry = clip (12.5);
    writeWav (dir + "/0-dry.wav", dry);
    for (int f = 0; f < dsp::kNumFormats; ++f)
    {
        auto p = factory (f);
        p.noise = true;
        writeWav (dir + "/" + names[f] + ".wav", process (dry, p));
        std::printf ("wrote %s\n", names[f]);
    }

    // Tape Stop at 4 s, Start at 6.5 s, on Cassette.
    auto p = factory (static_cast<int> (dsp::FormatId::cassette));
    writeWav (dir + "/7-tape-stop-and-start.wav", process (dry, p, 4.0, 6.5));
    std::printf ("wrote tape stop demo\n");
    return 0;
}
