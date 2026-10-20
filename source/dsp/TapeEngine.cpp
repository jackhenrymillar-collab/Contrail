#include "TapeEngine.h"

#include <algorithm>
#include <cmath>

namespace contrail::dsp
{

TapeEngine::TapeEngine()
{
    profile = morphFrom = morphTo = formatProfile (params.format);
}

void TapeEngine::prepare (double sampleRate, int maxBlockSize)
{
    (void) maxBlockSize;
    fs = sampleRate;

    const int factor = Oversampler::factorFor (sampleRate);
    for (auto& o : oversamplers)
        o.prepare (factor);
    for (auto& h : hysteresis)
        h.prepare (sampleRate * factor);

    losses.prepare (sampleRate);
    latency = static_cast<int> (std::lround (kLatencyMs * 0.001 * sampleRate));
    const int baseDelay = latency - oversamplers[0].latency() - losses.latency();
    transport.prepare (sampleRate, baseDelay);
    wowFlutter.prepare (sampleRate, static_cast<float> (baseDelay - InterpolatedDelay::kMinDelay - 4.0));

    hiss.prepare (sampleRate);
    dropouts.prepare (sampleRate);

    for (auto& line : dryLine)
        line.assign (static_cast<std::size_t> (latency + 1), 0.0f);

    driveGain.prepare (sampleRate, 30.0f);
    wetGain.prepare (sampleRate, 30.0f);
    mixAmount.prepare (sampleRate, 20.0f);
    hissGain.prepare (sampleRate, 60.0f);
    width.prepare (sampleRate, 60.0f);
    const double controlRate = sampleRate / kControlBlock;
    wowSmooth.prepare (controlRate, 60.0f);
    flutterSmooth.prepare (controlRate, 60.0f);
    driftSmooth.prepare (controlRate, 120.0f);
    ageSmooth.prepare (controlRate, 80.0f);
    toneSmooth.prepare (controlRate, 40.0f);
    biasSmooth.prepare (controlRate, 60.0f);

    morphStep = static_cast<float> (kControlBlock / (kFormatMorphMs * 0.001 * sampleRate));
    bypassStep = static_cast<float> (1.0 / (kBypassFadeMs * 0.001 * sampleRate));

    prepared = true;
    reset();
}

void TapeEngine::reset()
{
    for (auto& o : oversamplers) o.reset();
    for (auto& h : hysteresis) h.reset();
    for (auto& b : preEmphasis) b.reset();
    for (auto& b : deEmphasis) b.reset();
    for (auto& b : bump) b.reset();
    for (auto& b : lowCut) b.reset();
    for (auto& b : toneLow) b.reset();
    for (auto& b : toneHigh) b.reset();
    for (auto& l : speedLp) l.reset();
    wowFlutter.reset();
    transport.reset();
    transport.setStopped (params.tapeStop);
    hiss.reset();
    dropouts.reset();
    losses.reset();
    for (auto& line : dryLine)
        std::fill (line.begin(), line.end(), 0.0f);
    dryPos = 0;

    // Jump straight to the current settings: nothing glides in after a reset.
    targetFormat = params.format;
    profile = morphFrom = morphTo = formatProfile (params.format);
    morph = 1.0f;
    driveGain.snapTo (dbToGain (params.driveDb));
    wetGain.snapTo (dbToGain (params.outputDb + profile.trimDb));
    mixAmount.snapTo (params.mix);
    hissGain.snapTo (params.noise ? dbToGain (profile.hissDb + profile.hissAgeDb * params.age) : 0.0f);
    width.snapTo (profile.stereoWidth);
    wowSmooth.snapTo (params.wowPercent);
    flutterSmooth.snapTo (params.flutterPercent);
    driftSmooth.snapTo (params.drift);
    ageSmooth.snapTo (params.age);
    toneSmooth.snapTo (params.tone);
    biasSmooth.snapTo (params.bias);
    bypassMix = params.bypass ? 0.0f : 1.0f;
    appliedEmphasis = appliedBumpDb = appliedToneLow = appliedToneHigh = -100.0f;
    appliedBumpHz = appliedBumpQ = appliedLowCut = -1.0f;

    controlCountdown = 0;
    if (prepared)
        updateControls();
}

void TapeEngine::setParams (const EngineParams& p) noexcept
{
    params = p;
    params.format = std::clamp (p.format, 0, kNumFormats - 1);
    params.syncRate = std::clamp (p.syncRate, 0, kNumSyncRates - 1);
    transport.setStopped (params.tapeStop);
}

void TapeEngine::updateControls() noexcept
{
    // Format morph: every format value glides from where it was to the new format.
    if (params.format != targetFormat)
    {
        targetFormat = params.format;
        morphFrom = profile;
        morphTo = formatProfile (targetFormat);
        morph = 0.0f;
    }
    if (morph < 1.0f)
    {
        morph = std::min (1.0f, morph + morphStep);
        const float eased = morph * morph * (3.0f - 2.0f * morph);
        profile = blend (morphFrom, morphTo, eased);
    }
    const FormatProfile& f = profile;

    wowSmooth.setTarget (params.wowPercent);
    flutterSmooth.setTarget (params.flutterPercent);
    driftSmooth.setTarget (params.drift);
    ageSmooth.setTarget (params.age);
    toneSmooth.setTarget (params.tone);
    biasSmooth.setTarget (params.bias);
    const float age = ageSmooth.skip (kControlBlock);
    const float tone = toneSmooth.skip (kControlBlock);
    const float bias = biasSmooth.skip (kControlBlock);

    driveGain.setTarget (dbToGain (params.driveDb));
    wetGain.setTarget (dbToGain (params.outputDb + f.trimDb));
    mixAmount.setTarget (params.mix);
    hissGain.setTarget (params.noise ? dbToGain (f.hissDb + f.hissAgeDb * age) : 0.0f);
    width.setTarget (f.stereoWidth);

    // Wow and flutter, with the host tempo if Sync is on.
    WowFlutterParams wf;
    wf.wowPercent = wowSmooth.skip (kControlBlock);
    wf.flutterPercent = flutterSmooth.skip (kControlBlock);
    wf.drift = driftSmooth.skip (kControlBlock);
    wf.wide = params.wide;
    wf.synced = params.sync;
    if (params.sync)
    {
        const double beats = kSyncBeats[static_cast<std::size_t> (params.syncRate)];
        const double bpm = host.valid && host.bpm > 1.0 ? host.bpm : 120.0;
        wf.syncHz = static_cast<float> (bpm / 60.0 / beats);
        if (host.valid && host.playing)
        {
            const double ppq = host.ppqPosition + samplesSinceHost * bpm / 60.0 / fs;
            const double cycles = ppq / beats;
            wf.syncPhase = static_cast<float> (cycles - std::floor (cycles));
        }
    }
    wowFlutter.setParams (wf, f);

    transport.setTimes (f.stopSeconds, f.startSeconds);

    // Recording: the loop narrows as bias rises.
    const float hystWidth = std::clamp (f.hysteresisWidth + (0.5f - bias) * 0.6f, 0.0f, 0.98f);
    for (auto& h : hysteresis)
        h.setParams (f.saturation, hystWidth);
    if (std::abs (f.emphasisDb - appliedEmphasis) > 1.0e-3f)
    {
        appliedEmphasis = f.emphasisDb;
        for (auto& b : preEmphasis) b.setHighShelf (3000.0f, f.emphasisDb, fs);
        for (auto& b : deEmphasis) b.setHighShelf (3000.0f, -f.emphasisDb, fs);
    }

    // Playback.
    dropouts.setAmount (f.dropoutRate * age * age, f.dropoutDepthDb * std::sqrt (age));
    losses.update (f, age);
    if (std::abs (f.bumpHz - appliedBumpHz) > 1.0e-2f || std::abs (f.bumpDb - appliedBumpDb) > 1.0e-3f
        || std::abs (f.bumpQ - appliedBumpQ) > 1.0e-3f)
    {
        appliedBumpHz = f.bumpHz;
        appliedBumpDb = f.bumpDb;
        appliedBumpQ = f.bumpQ;
        for (auto& b : bump) b.setPeak (f.bumpHz, f.bumpQ, f.bumpDb, fs);
    }
    if (std::abs (f.lowCutHz - appliedLowCut) > 1.0e-2f)
    {
        appliedLowCut = f.lowCutHz;
        for (auto& b : lowCut) b.setHighpass (f.lowCutHz, 0.7071f, fs);
    }

    // Tone tilts the highs against the lows. Over-bias also dulls the highs a little.
    const float highDb = tone * 8.0f - (bias - 0.5f) * 3.0f;
    const float lowDb = -tone * 2.5f;
    if (std::abs (highDb - appliedToneHigh) > 1.0e-3f)
    {
        appliedToneHigh = highDb;
        for (auto& b : toneHigh) b.setHighShelf (3200.0f, highDb, fs);
    }
    if (std::abs (lowDb - appliedToneLow) > 1.0e-3f)
    {
        appliedToneLow = lowDb;
        for (auto& b : toneLow) b.setLowShelf (200.0f, lowDb, fs);
    }
}

void TapeEngine::process (float* left, float* right, int numSamples) noexcept
{
    if (! prepared)
        return;

    const auto dryLen = static_cast<int> (dryLine[0].size());

    for (int i = 0; i < numSamples; ++i)
    {
        if (--controlCountdown < 0)
        {
            updateControls();
            controlCountdown = kControlBlock - 1;
        }
        samplesSinceHost += 1.0;

        const float inL = left[i], inR = right[i];

        // Dry path, delayed by the latency.
        dryLine[0][static_cast<std::size_t> (dryPos)] = inL;
        dryLine[1][static_cast<std::size_t> (dryPos)] = inR;
        int readPos = dryPos - latency;
        if (readPos < 0)
            readPos += dryLen;
        const float dryL = dryLine[0][static_cast<std::size_t> (readPos)];
        const float dryR = dryLine[1][static_cast<std::size_t> (readPos)];
        dryPos = dryPos + 1 >= dryLen ? 0 : dryPos + 1;

        // Record: Drive pushes the level into the tape; the same amount comes off after,
        // so Drive changes the saturation, not the loudness of quiet passages.
        const float g = driveGain.next();
        const float makeup = 1.0f / g;
        auto& h0 = hysteresis[0];
        auto& h1 = hysteresis[1];
        float l = preEmphasis[0].process (inL * g);
        float r = preEmphasis[1].process (inR * g);
        l = oversamplers[0].process (l, [&h0] (float v) { return h0.process (v); });
        r = oversamplers[1].process (r, [&h1] (float v) { return h1.process (v); });
        l = deEmphasis[0].process (l) * makeup;
        r = deEmphasis[1].process (r) * makeup;

        // Transport.
        float offL, offR;
        wowFlutter.next (offL, offR);
        transport.process (l, r, offL, offR, l, r);
        const float speed = transport.speed();
        if (speed < 0.9999f)
        {
            // A slowing tape loses its highs as the wavelengths shrink.
            const float a = OnePoleLowpass::coefficientFor (150.0f + 19000.0f * speed * speed, fs);
            l = speedLp[1].process (speedLp[0].process (l, a), a);
            r = speedLp[3].process (speedLp[2].process (r, a), a);
        }
        else
        {
            speedLp[0].reset (l);
            speedLp[1].reset (l);
            speedLp[2].reset (r);
            speedLp[3].reset (r);
        }

        // Hiss comes off the moving tape, so it slows and stops with it.
        const float hg = hissGain.next() * speed;
        if (hg > 0.0f)
        {
            float nl, nr;
            hiss.next (nl, nr);
            l += hg * nl;
            r += hg * nr;
        }

        dropouts.process (l, r);
        losses.process (l, r);
        l = toneHigh[0].process (toneLow[0].process (lowCut[0].process (bump[0].process (l))));
        r = toneHigh[1].process (toneLow[1].process (lowCut[1].process (bump[1].process (r))));

        // Narrow formats fold toward mono.
        const float w = width.next();
        const float mid = 0.5f * (l + r);
        const float side = 0.5f * (l - r) * w;
        const float wg = wetGain.next();
        l = (mid + side) * wg;
        r = (mid - side) * wg;

        // Mix (linear: the wet signal is a copy of the dry one, so they add in phase).
        const float m = mixAmount.next();
        float outL = dryL + m * (l - dryL);
        float outR = dryR + m * (r - dryR);

        // Bypass fades to the (equally delayed) dry signal.
        const float target = params.bypass ? 0.0f : 1.0f;
        if (bypassMix != target)
            bypassMix = target > bypassMix ? std::min (target, bypassMix + bypassStep)
                                           : std::max (target, bypassMix - bypassStep);
        if (bypassMix < 1.0f)
        {
            outL = dryL + bypassMix * (outL - dryL);
            outR = dryR + bypassMix * (outR - dryR);
        }

        // Last-ditch guard: never send a non-finite value to the host.
        left[i] = std::isfinite (outL) ? outL : 0.0f;
        right[i] = std::isfinite (outR) ? outR : 0.0f;
    }

    // Host position for the next block arrives with setHostTransport.
    samplesSinceHost = 0.0;
}

} // namespace contrail::dsp
