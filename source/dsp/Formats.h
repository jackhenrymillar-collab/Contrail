#pragma once

// The six tape formats. Each one sets its own wow and flutter rates, tone, noise,
// saturation and transport feel. The knobs (Wow, Flutter, Drift, Drive, Age, Tone, Mix)
// work on top of these.

#include <array>
#include <cstddef>

namespace contrail::dsp
{

enum class FormatId
{
    studioReel = 0,
    homeReel,
    cassette,
    wornCassette,
    vhs,
    dictaphone
};

constexpr int kNumFormats = 6;

// Every field is a float so two profiles can be blended while the format changes.
// X(name) lists them once for the blend and for tests.
#define CONTRAIL_FORMAT_FIELDS(X)                                                                  \
    /* Tape and heads */                                                                           \
    X (speedIps)        /* tape speed, inches per second */                                        \
    X (gapUm)           /* playback head gap, micrometres (gap loss) */                            \
    X (ageSpacingUm)    /* head-to-tape spacing added at Age 100% (spacing loss) */               \
    X (bandwidthHz)     /* top end of the format's electronics and EQ */                           \
    X (bandwidthOrder)  /* steepness of that top end (Butterworth order) */                        \
    X (lowCutHz)        /* bottom end */                                                           \
    X (bumpHz)          /* head bump centre */                                                     \
    X (bumpDb)          /* head bump height */                                                     \
    X (bumpQ)                                                                                      \
    X (stereoWidth)     /* 1 = full stereo, 0 = mono (a dictaphone is mono) */                    \
    /* Recording */                                                                                \
    X (emphasisDb)      /* record pre-emphasis: highs saturate first, like real tape */           \
    X (saturation)      /* hysteresis saturation, 0 to 1 */                                        \
    X (hysteresisWidth) /* hysteresis loop width at nominal bias, 0 to 1 */                       \
    X (asymmetry)       /* even-harmonic warmth from a slightly lopsided record path */          \
    /* Noise and wear */                                                                           \
    X (hissDb)          /* hiss level at Age 0, dBFS RMS */                                        \
    X (hissAgeDb)       /* extra hiss at Age 100% */                                               \
    X (dropoutRate)     /* dropouts per second at Age 100% */                                      \
    X (dropoutDepthDb)  /* deepest dropout at Age 100% */                                          \
    /* Transport */                                                                                \
    X (reelHz)          /* main wow rate (reel or capstan eccentricity) */                         \
    X (reelHarmonic)    /* second harmonic of that wow, 0 to 1 */                                  \
    X (randomWowHz)     /* average rate of the random wow */                                       \
    X (flutterHz)       /* capstan flutter */                                                      \
    X (flutterHz2)      /* pinch roller and guide flutter */                                       \
    X (scrape)          /* scrape flutter amount, relative to Flutter */                           \
    X (stopSeconds)     /* tape stop: time to a halt */                                            \
    X (startSeconds)    /* tape start: time back to speed */                                       \
    /* Level */                                                                                    \
    X (trimDb)          /* loudness match between formats */

struct FormatProfile
{
#define CONTRAIL_DECLARE_FIELD(name) float name = 0.0f;
    CONTRAIL_FORMAT_FIELDS (CONTRAIL_DECLARE_FIELD)
#undef CONTRAIL_DECLARE_FIELD
};

// Field-by-field blend: t = 0 gives a, t = 1 gives b.
inline FormatProfile blend (const FormatProfile& a, const FormatProfile& b, float t) noexcept
{
    FormatProfile out;
#define CONTRAIL_BLEND_FIELD(name) out.name = a.name + (b.name - a.name) * t;
    CONTRAIL_FORMAT_FIELDS (CONTRAIL_BLEND_FIELD)
#undef CONTRAIL_BLEND_FIELD
    return out;
}

inline const FormatProfile& formatProfile (int index) noexcept
{
    static const std::array<FormatProfile, kNumFormats> profiles = [] {
        std::array<FormatProfile, kNumFormats> p {};

        // 0. Studio Reel: 15 ips two-track. Steady and warm, soft head bump.
        p[0].speedIps = 15.0f;   p[0].gapUm = 2.5f;  p[0].ageSpacingUm = 0.6f;
        p[0].bandwidthHz = 21000.0f; p[0].bandwidthOrder = 2.0f; p[0].lowCutHz = 18.0f;
        p[0].bumpHz = 55.0f;  p[0].bumpDb = 2.2f;  p[0].bumpQ = 1.0f; p[0].stereoWidth = 1.0f;
        p[0].emphasisDb = 3.0f; p[0].saturation = 0.40f; p[0].hysteresisWidth = 0.12f; p[0].asymmetry = 0.03f;
        p[0].hissDb = -84.0f; p[0].hissAgeDb = 8.0f; p[0].dropoutRate = 0.08f; p[0].dropoutDepthDb = 6.0f;
        p[0].reelHz = 0.55f; p[0].reelHarmonic = 0.20f; p[0].randomWowHz = 0.45f;
        p[0].flutterHz = 9.0f; p[0].flutterHz2 = 23.0f; p[0].scrape = 0.15f;
        p[0].stopSeconds = 1.6f; p[0].startSeconds = 0.9f;

        // 1. Home Reel: 7.5 ips. Looser, darker highs, a touch of hiss.
        p[1].speedIps = 7.5f;    p[1].gapUm = 3.0f;  p[1].ageSpacingUm = 0.8f;
        p[1].bandwidthHz = 15500.0f; p[1].bandwidthOrder = 2.0f; p[1].lowCutHz = 25.0f;
        p[1].bumpHz = 42.0f;  p[1].bumpDb = 2.0f;  p[1].bumpQ = 1.1f; p[1].stereoWidth = 1.0f;
        p[1].emphasisDb = 4.0f; p[1].saturation = 0.50f; p[1].hysteresisWidth = 0.18f; p[1].asymmetry = 0.05f;
        p[1].hissDb = -74.0f; p[1].hissAgeDb = 8.0f; p[1].dropoutRate = 0.15f; p[1].dropoutDepthDb = 8.0f;
        p[1].reelHz = 0.85f; p[1].reelHarmonic = 0.30f; p[1].randomWowHz = 0.65f;
        p[1].flutterHz = 7.0f; p[1].flutterHz2 = 17.0f; p[1].scrape = 0.20f;
        p[1].stopSeconds = 1.3f; p[1].startSeconds = 0.7f;

        // 2. Cassette: gentle wow, audible flutter, rolled-off top.
        p[2].speedIps = 1.875f;  p[2].gapUm = 1.2f;  p[2].ageSpacingUm = 0.6f;
        p[2].bandwidthHz = 12500.0f; p[2].bandwidthOrder = 3.0f; p[2].lowCutHz = 35.0f;
        p[2].bumpHz = 75.0f;  p[2].bumpDb = 1.5f;  p[2].bumpQ = 1.0f; p[2].stereoWidth = 1.0f;
        p[2].emphasisDb = 6.0f; p[2].saturation = 0.60f; p[2].hysteresisWidth = 0.25f; p[2].asymmetry = 0.06f;
        p[2].hissDb = -64.0f; p[2].hissAgeDb = 7.0f; p[2].dropoutRate = 0.30f; p[2].dropoutDepthDb = 10.0f;
        p[2].reelHz = 1.3f;  p[2].reelHarmonic = 0.35f; p[2].randomWowHz = 0.9f;
        p[2].flutterHz = 12.0f; p[2].flutterHz2 = 28.0f; p[2].scrape = 0.30f;
        p[2].stopSeconds = 0.9f; p[2].startSeconds = 0.45f;

        // 3. Worn Cassette: a stretched, well-loved tape. Deep wow, dropouts, grit.
        p[3].speedIps = 1.875f;  p[3].gapUm = 1.6f;  p[3].ageSpacingUm = 1.1f;
        p[3].bandwidthHz = 9500.0f; p[3].bandwidthOrder = 3.0f; p[3].lowCutHz = 50.0f;
        p[3].bumpHz = 75.0f;  p[3].bumpDb = 1.0f;  p[3].bumpQ = 0.9f; p[3].stereoWidth = 0.9f;
        p[3].emphasisDb = 7.0f; p[3].saturation = 0.72f; p[3].hysteresisWidth = 0.40f; p[3].asymmetry = 0.10f;
        p[3].hissDb = -60.0f; p[3].hissAgeDb = 6.0f; p[3].dropoutRate = 1.2f; p[3].dropoutDepthDb = 16.0f;
        p[3].reelHz = 1.05f; p[3].reelHarmonic = 0.55f; p[3].randomWowHz = 0.75f;
        p[3].flutterHz = 10.0f; p[3].flutterHz2 = 25.0f; p[3].scrape = 0.50f;
        p[3].stopSeconds = 1.0f; p[3].startSeconds = 0.55f;

        // 4. VHS: linear-track VCR audio. Woozy pitch, soft narrow top.
        p[4].speedIps = 1.31f;   p[4].gapUm = 1.5f;  p[4].ageSpacingUm = 0.7f;
        p[4].bandwidthHz = 9000.0f; p[4].bandwidthOrder = 3.0f; p[4].lowCutHz = 60.0f;
        p[4].bumpHz = 95.0f;  p[4].bumpDb = 1.0f;  p[4].bumpQ = 0.9f; p[4].stereoWidth = 0.75f;
        p[4].emphasisDb = 5.0f; p[4].saturation = 0.55f; p[4].hysteresisWidth = 0.25f; p[4].asymmetry = 0.06f;
        p[4].hissDb = -63.0f; p[4].hissAgeDb = 6.0f; p[4].dropoutRate = 0.40f; p[4].dropoutDepthDb = 10.0f;
        p[4].reelHz = 0.45f; p[4].reelHarmonic = 0.35f; p[4].randomWowHz = 0.55f;
        p[4].flutterHz = 8.0f; p[4].flutterHz2 = 20.0f; p[4].scrape = 0.25f;
        p[4].stopSeconds = 1.1f; p[4].startSeconds = 0.6f;

        // 5. Dictaphone: micro-cassette voice recorder. Fast warble, thin and lo-fi.
        p[5].speedIps = 0.94f;   p[5].gapUm = 2.0f;  p[5].ageSpacingUm = 0.6f;
        p[5].bandwidthHz = 5200.0f; p[5].bandwidthOrder = 4.0f; p[5].lowCutHz = 220.0f;
        p[5].bumpHz = 320.0f; p[5].bumpDb = 3.0f;  p[5].bumpQ = 0.8f; p[5].stereoWidth = 0.3f;
        p[5].emphasisDb = 6.0f; p[5].saturation = 0.78f; p[5].hysteresisWidth = 0.35f; p[5].asymmetry = 0.10f;
        p[5].hissDb = -54.0f; p[5].hissAgeDb = 5.0f; p[5].dropoutRate = 0.30f; p[5].dropoutDepthDb = 10.0f;
        p[5].reelHz = 2.8f;  p[5].reelHarmonic = 0.40f; p[5].randomWowHz = 2.0f;
        p[5].flutterHz = 18.0f; p[5].flutterHz2 = 33.0f; p[5].scrape = 0.40f;
        p[5].stopSeconds = 0.55f; p[5].startSeconds = 0.3f;

        // Loudness match (pink noise at -18 dBFS through each format's factory settings).
        // Measured by the "formats are loudness matched" test.
        const float trims[kNumFormats] = { 0.5f, 0.95f, 1.4f, 2.7f, 3.3f, 6.7f };
        for (std::size_t i = 0; i < p.size(); ++i)
            p[i].trimDb = trims[i];
        return p;
    }();

    const int i = index < 0 ? 0 : (index >= kNumFormats ? kNumFormats - 1 : index);
    return profiles[static_cast<std::size_t> (i)];
}

// Each format's starting knob settings (its factory preset). Knob memory starts here.
struct FormatKnobs
{
    float wowPercent, flutterPercent, drift, driveDb, age, tone, mix; // drift, age, mix 0..100; tone -100..100
};

inline constexpr std::array<FormatKnobs, kNumFormats> kFormatDefaults { {
    //  wow    flutter drift drive  age   tone    mix
    { 0.04f, 0.02f, 25.0f, 3.0f,  5.0f,   0.0f, 100.0f }, // Studio Reel
    { 0.08f, 0.05f, 30.0f, 4.0f, 25.0f,   0.0f, 100.0f }, // Home Reel
    { 0.15f, 0.10f, 50.0f, 5.0f, 30.0f, -10.0f, 100.0f }, // Cassette
    { 0.45f, 0.25f, 85.0f, 6.0f, 85.0f, -25.0f, 100.0f }, // Worn Cassette
    { 0.75f, 0.15f, 55.0f, 2.0f, 55.0f, -30.0f, 100.0f }, // VHS
    { 0.25f, 0.40f, 70.0f, 8.0f, 60.0f, -40.0f, 100.0f }, // Dictaphone
} };

// Words shown under the Drift and Age knobs (0 to 100).
inline const char* driftWord (float drift) noexcept
{
    return drift < 15.0f ? "Steady" : drift < 40.0f ? "Slow" : drift < 60.0f ? "Medium" : drift < 80.0f ? "Fast" : "Wander";
}

inline const char* ageWord (float age) noexcept
{
    return age < 10.0f ? "New" : age < 40.0f ? "Light" : age < 70.0f ? "Medium" : "Heavy";
}

// Musical names for the wow rate when it follows the host tempo.
constexpr int kNumSyncRates = 7;
inline constexpr std::array<float, kNumSyncRates> kSyncBeats { 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f };

} // namespace contrail::dsp
