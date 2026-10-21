#pragma once

#include "Analog.h"
#include "Blocks.h"
#include "Chorus.h"
#include "Crunch.h"
#include "Formats.h"
#include "Hysteresis.h"
#include "Oversampler.h"
#include "PlaybackLosses.h"
#include "TapeTransport.h"
#include "Wear.h"
#include "WowFlutter.h"

#include <array>
#include <vector>

namespace contrail::dsp
{

struct EngineParams
{
    int format = static_cast<int> (FormatId::studioReel);
    float wowPercent = 0.04f;      // 0 to 2 %
    float flutterPercent = 0.02f;  // 0 to 1 %
    float drift = 0.25f;           // 0 to 1
    float driveDb = 3.0f;          // -6 to +18 dB into the tape
    float age = 0.05f;             // 0 to 1
    float tone = 0.0f;             // -1 (dark) to +1 (bright)
    float mix = 1.0f;              // 0 to 1
    float bias = 0.5f;             // 0 (under-biased) to 1 (over-biased); 0.5 is nominal
    float outputDb = 0.0f;         // -18 to +18 dB on the wet signal
    bool tapeStop = false;         // true: stop the tape; false: run (or start it again)
    bool sync = false;             // periodic wow follows the host tempo
    int syncRate = 2;              // index into kSyncBeats
    bool noise = true;             // tape hiss on or off
    bool wide = true;              // Stereo: Wide (true) or Linked (false)
    bool mono = false;             // Stereo: Mono (overrides wide)
    float crunch = 0.0f;           // Sampler Crunch, 0 to 1
    float chorus = 0.0f;           // Vintage Chorus, 0 to 1
    bool bypass = false;
};

struct HostTransport
{
    double bpm = 120.0;
    double ppqPosition = 0.0;
    bool playing = false;
    bool valid = false;
};

// The whole tape chain:
//
//   Crunch -> Drive -> input transformer -> valve record amp -> pre-emphasis -> tape hysteresis
//   -> de-emphasis (all oversampled)
//   -> transport (wow, flutter, drift, tape stop) -> hiss -> dropouts
//   -> playback losses (Age) -> head bump and low cut -> Tone -> Chorus -> width -> Output
//   -> Mix with the dry signal -> bypass
//
// The dry signal is delayed to line up with the wet one, so the plugin's latency is a
// fixed 25 ms at every sample rate and Mix never combs.
class TapeEngine
{
public:
    static constexpr float kLatencyMs = 25.0f;
    static constexpr int kControlBlock = 32;
    static constexpr float kFormatMorphMs = 150.0f;
    static constexpr float kBypassFadeMs = 20.0f;
    static constexpr float kTapeHeadroom = 0.5f;

    TapeEngine();

    // All allocation happens here, never on the audio thread.
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setParams (const EngineParams& params) noexcept;
    void setHostTransport (const HostTransport& t) noexcept { host = t; }

    // In-place stereo processing on two distinct buffers. Any block size is accepted.
    void process (float* left, float* right, int numSamples) noexcept;

    int getLatencySamples() const noexcept { return latency; }
    float transportSpeed() const noexcept { return transport.speed(); }
    int oversamplingFactor() const noexcept { return oversamplers[0].getFactor(); }

    // The format being heard right now (mid-morph it's a blend).
    const FormatProfile& currentProfile() const noexcept { return profile; }

private:
    void updateControls() noexcept;

    // Input transformer -> valve amp -> pre-emphasis -> tape -> de-emphasis, for one
    // channel at the oversampled rate.
    float recordChain (std::size_t channel, float x) noexcept;

    double fs = 48000.0, internalFs = 192000.0;
    int latency = 1200;
    bool prepared = false;

    EngineParams params;
    HostTransport host;
    double samplesSinceHost = 0.0;

    // Format morph.
    FormatProfile profile, morphFrom, morphTo;
    int targetFormat = -1;
    float morph = 1.0f, morphStep = 0.01f;

    // Before and after the tape.
    Crunch crunch;
    Chorus chorus;

    // Record side.
    std::array<Oversampler, 2> oversamplers;
    std::array<Hysteresis, 2> hysteresis;
    std::array<Transformer, 2> transformers;
    std::array<Triode, 2> triodes;
    std::array<DcBlocker, 2> tubeDc;               // these four run at the oversampled rate
    std::array<Biquad, 2> preEmphasis, deEmphasis;
    std::array<DcBlocker, 2> recordDc;
    float appliedEmphasis = -100.0f;

    // Transport.
    WowFlutter wowFlutter;
    TapeTransport transport;
    std::array<OnePoleLowpass, 4> speedLp;

    // Playback side.
    Hiss hiss;
    Dropouts dropouts;
    PlaybackLosses losses;
    std::array<Biquad, 2> bump, lowCut, toneLow, toneHigh;
    float appliedBumpHz = -1.0f, appliedBumpDb = -100.0f, appliedBumpQ = -1.0f, appliedLowCut = -1.0f;
    float appliedToneLow = -100.0f, appliedToneHigh = -100.0f;

    // Smoothed values.
    Smoother driveGain, wetGain, mixAmount, hissGain, width;
    Smoother wowSmooth, flutterSmooth, driftSmooth, ageSmooth, toneSmooth, biasSmooth;
    float bypassMix = 1.0f, bypassStep = 0.001f; // 1 = effect on

    // Dry path, delayed by the latency.
    std::array<std::vector<float>, 2> dryLine;
    int dryPos = 0;

    int controlCountdown = 0;
};

} // namespace contrail::dsp
