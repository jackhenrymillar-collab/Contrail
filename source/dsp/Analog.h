#pragma once

// The record electronics in front of the tape: an input transformer and a valve
// (triode) gain stage. Together with the tape's own hysteresis they make Drive.
//
// Both run at the oversampled rate, inside the same oversampler as the tape, so the
// harmonics they make don't fold back as aliasing.

#include "Blocks.h"

#include <cmath>

namespace contrail::dsp
{

// Transformer core saturation. The flux in the core is the integral of the voltage,
// so it's the low end that saturates: the bass thickens and rounds off while the mids
// and highs pass through clean. Modelled as a one-pole "flux" path (an integrator
// above a few tens of Hz) that is soft-clipped and put back in place of the clean one.
class Transformer
{
public:
    void prepare (double sampleRate)
    {
        a = 1.0f - OnePoleLowpass::coefficientFor (kCornerHz, sampleRate); // step size toward the input
        reset();
    }

    void reset() noexcept { flux = 0.0f; }

    // amount 0 is a clean transformer; 1 is a small, easily saturated one.
    void setAmount (float amount) noexcept { drive = amount * kMaxDrive; }

    float process (float x) noexcept
    {
        flux += a * (x - flux);
        if (drive <= 1.0e-4f)
            return x;
        // tanh(d f) / d keeps unity gain for small signals and rounds off big ones.
        const float saturated = std::tanh (drive * flux) / drive;
        return x + (saturated - flux);
    }

private:
    static constexpr float kCornerHz = 45.0f;
    static constexpr float kMaxDrive = 1.0f;
    float a = 0.001f, flux = 0.0f, drive = 0.0f;
};

// A single-ended triode stage. The valve's curve is lopsided, so it clips softly and
// earlier on one side: mostly second harmonic, with the higher ones falling away fast.
// That's the "warm" part of Drive. Its gain into the curve sets how soon it bends,
// and the bias sets how lopsided it is. Small signals pass at unity gain.
//
// As the stage works harder its operating point creeps (the grid draws current and
// the cathode bias shifts), which compresses a little and adds a touch of bloom. A
// slow follower of the signal's level moves the bias to model that.
class Triode
{
public:
    void prepare (double sampleRate)
    {
        envAttack = 1.0f - OnePoleLowpass::coefficientFor (1.0f / (2.0f * 3.14159265f * 0.010f), sampleRate);   // ~10 ms
        envRelease = 1.0f - OnePoleLowpass::coefficientFor (1.0f / (2.0f * 3.14159265f * 0.120f), sampleRate);  // ~120 ms
        reset();
    }

    void reset() noexcept { env = 0.0f; }

    void setParams (float driveIntoCurve, float bias) noexcept
    {
        d = std::max (0.05f, driveIntoCurve);
        b = bias;
    }

    float process (float x) noexcept
    {
        const float u = d * x;

        // Bias creep: louder passages pull the operating point toward the middle of the
        // curve, so the stage gets a little cleaner and a little quieter as it's pushed.
        const float level = std::abs (u);
        env += (level > env ? envAttack : envRelease) * (level - env);
        const float bias = b / (1.0f + 0.6f * env);

        const float t = std::tanh (bias);
        const float slope = 1.0f - t * t; // the curve's slope at rest, so small signals see unity gain
        return (std::tanh (u + bias) - t) / (slope * d);
    }

private:
    float d = 1.0f, b = 0.0f, env = 0.0f;
    float envAttack = 0.1f, envRelease = 0.001f;
};

} // namespace contrail::dsp
