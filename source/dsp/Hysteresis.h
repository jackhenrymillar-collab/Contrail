#pragma once

// Tape magnetisation: the Jiles-Atherton hysteresis model, solved in real time as
// described in J. Chowdhury, "Real-time physical modelling for analog tape machines",
// DAFx 2019. Written from the paper's equations (no code from ChowTape, which is GPL).
//
// The record head's field H drives the tape's magnetisation M:
//
//   dM/dt = [ (1-c) dM_irr + c (Ms/a) L'(Q) ] dH/dt / [ 1 - c alpha (Ms/a) L'(Q) ]
//   dM_irr = delta_M (M_an - M) / ( (1-c) delta k - alpha (M_an - M) )
//   M_an = Ms L(Q),  Q = (H + alpha M) / a,  L(x) = coth(x) - 1/x
//
// delta is the sign of dH/dt, and delta_M is 1 when M is moving toward M_an (else 0).
// It is integrated with fourth-order Runge-Kutta, one step per (oversampled) sample.
//
// Ms (saturation), a (shape), c (reversibility) and k (loop width) come from
// Saturation and Width, mapped the way the paper's plugin does: Width near 1 is a
// wide loop (strong hysteresis), near 0 almost no loop. Higher bias narrows the loop.

#include <algorithm>
#include <array>
#include <cmath>

namespace contrail::dsp
{

class Hysteresis
{
public:
    void prepare (double internalSampleRate)
    {
        fs = internalSampleRate;
        T = 1.0 / internalSampleRate;
        (void) levelMakeup (0.5f, 0.5f); // builds the makeup table here, off the audio thread
        reset();
    }

    void reset() noexcept { M = H1 = Hd1 = 0.0; }

    // saturation and width are 0 to 1.
    void setParams (float saturation, float width) noexcept
    {
        setRaw (saturation, width);
        makeup = levelMakeup (saturation, width);
    }

    float process (float x) noexcept
    {
        const double H = static_cast<double> (x);
        const double Hd = (H - H1) * fs;

        const double Hmid = 0.5 * (H + H1);
        const double Hdmid = 0.5 * (Hd + Hd1);
        const double k1 = T * dMdt (M, H1, Hd1);
        const double k2 = T * dMdt (M + 0.5 * k1, Hmid, Hdmid);
        const double k3 = T * dMdt (M + 0.5 * k2, Hmid, Hdmid);
        const double k4 = T * dMdt (M + k3, H, Hd);
        double next = M + (k1 + 2.0 * k2 + 2.0 * k3 + k4) / 6.0;

        // Physical magnetisation can't pass saturation; this also catches any
        // numerical blow-up so the plugin never outputs garbage.
        if (! std::isfinite (next))
            next = 0.0;
        M = std::clamp (next, -Ms, Ms);
        H1 = H;
        Hd1 = Hd;
        return static_cast<float> (M * makeup);
    }

    double saturationLevel() const noexcept { return Ms * makeup; }

private:
    void setRaw (float saturation, float width) noexcept
    {
        Ms = 0.5 + 1.5 * (1.0 - std::clamp<double> (saturation, 0.0, 1.0));
        a = Ms / (0.01 + 6.0 * kShape);
        c = std::sqrt (1.0 - std::clamp<double> (width, 0.0, 0.99)) - 0.01;
    }

    // A wider loop gives a quieter tape at normal levels (only the reversible part
    // responds to small fields). This finds the gain that brings a -12 dBFS tone back to
    // its own level, so Width and Bias change the character, not the loudness.
    // Measured once for a grid of settings, then interpolated.
    static double levelMakeup (float saturation, float width) noexcept
    {
        static const auto table = [] {
            std::array<std::array<float, kGrid>, kGrid> t {};
            for (int si = 0; si < kGrid; ++si)
                for (int wi = 0; wi < kGrid; ++wi)
                {
                    Hysteresis h; // not prepare(): that would ask for this table
                    h.fs = 192000.0;
                    h.T = 1.0 / 192000.0;
                    h.setRaw (static_cast<float> (si) / (kGrid - 1), 0.98f * static_cast<float> (wi) / (kGrid - 1));
                    h.makeup = (3.0 * h.a - alpha * h.Ms) / h.Ms;
                    // 1 kHz at 0.25: settle for two cycles, measure the third.
                    double num = 0.0, den = 0.0;
                    for (int i = 0; i < 576; ++i)
                    {
                        const double ref = std::sin (2.0 * 3.14159265358979 * 1000.0 * i / 192000.0);
                        const double y = h.process (static_cast<float> (0.25 * ref));
                        if (i >= 384) { num += y * ref; den += 0.25 * ref * ref; }
                    }
                    t[static_cast<std::size_t> (si)][static_cast<std::size_t> (wi)]
                        = static_cast<float> (h.makeup * den / std::max (1.0e-9, num));
                }
            return t;
        }();
        const double sp = std::clamp<double> (saturation, 0.0, 1.0) * (kGrid - 1);
        const double wp = std::clamp<double> (width / 0.98, 0.0, 1.0) * (kGrid - 1);
        const int s0 = std::min (static_cast<int> (sp), kGrid - 2);
        const int w0 = std::min (static_cast<int> (wp), kGrid - 2);
        const double sf = sp - s0, wf = wp - w0;
        auto at = [] (int si, int wi) { return static_cast<double> (table[static_cast<std::size_t> (si)][static_cast<std::size_t> (wi)]); };
        return (1.0 - sf) * ((1.0 - wf) * at (s0, w0) + wf * at (s0, w0 + 1))
             + sf * ((1.0 - wf) * at (s0 + 1, w0) + wf * at (s0 + 1, w0 + 1));
    }

    static constexpr int kGrid = 11;

    // L(q) and L'(q) together, from a single tanh. Series near zero, where the closed
    // forms lose precision.
    static void langevin (double q, double& l, double& lPrime) noexcept
    {
        if (std::abs (q) < 0.05)
        {
            const double q2 = q * q;
            l = q * (1.0 / 3.0 - q2 / 45.0);
            lPrime = 1.0 / 3.0 - q2 / 15.0;
            return;
        }
        const double coth = 1.0 / std::tanh (q);
        const double invQ = 1.0 / q;
        l = coth - invQ;
        lPrime = invQ * invQ - coth * coth + 1.0; // 1/q^2 - 1/sinh^2(q)
    }

    double dMdt (double m, double h, double hd) const noexcept
    {
        const double q = (h + alpha * m) / a;
        double l, lPrime;
        langevin (q, l, lPrime);
        const double diff = Ms * l - m;
        const double delta = hd >= 0.0 ? 1.0 : -1.0;
        const double deltaM = (delta > 0.0) == (diff > 0.0) ? 1.0 : 0.0;

        double denom = (1.0 - c) * delta * k - alpha * diff;
        if (std::abs (denom) < 1.0e-9)
            denom = denom < 0.0 ? -1.0e-9 : 1.0e-9;
        const double irreversible = deltaM * (1.0 - c) * diff / denom;
        const double reversible = c * (Ms / a) * lPrime;
        double bottom = 1.0 - c * alpha * (Ms / a) * lPrime;
        if (std::abs (bottom) < 1.0e-9)
            bottom = 1.0e-9;
        return hd * (irreversible + reversible) / bottom;
    }

    // Fixed shape: the Drive knob sets the level into the model instead.
    static constexpr double kShape = 0.5;
    static constexpr double alpha = 1.6e-3;
    static constexpr double k = 0.47875;

    double fs = 192000.0, T = 1.0 / 192000.0;
    double Ms = 1.25, a = 1.25 / 3.01, c = 0.7, makeup = 1.0;
    double M = 0.0, H1 = 0.0, Hd1 = 0.0;
};

} // namespace contrail::dsp
