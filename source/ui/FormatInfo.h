#pragma once

namespace contrail::ui
{

// What the panel says about each format. The order matches dsp::FormatId.
struct FormatInfo
{
    const char* name;
    const char* look;        // the sky's name, shown as "FORMAT · <LOOK>"
    const char* description;
};

inline constexpr FormatInfo kFormatInfo[] = {
    { "Studio Reel",   "First light",      "15 ips two-track: steady and warm, with a soft head bump" },
    { "Home Reel",     "Golden hour",      "7.5 ips: a little looser, darker highs and a touch of hiss" },
    { "Cassette",      "Pastel morning",   "Compact cassette: gentle wow, audible flutter, a rolled-off top" },
    { "Worn Cassette", "Faded photograph", "A stretched, well-loved tape: deep wow, dropouts and grit" },
    { "VHS",           "Tracking lines",   "Linear-track VCR audio: woozy pitch and a soft, narrow top" },
    { "Dictaphone",    "Overcast dawn",    "Micro-cassette voice recorder: fast warble, thin and lo-fi" },
};

inline constexpr int kNumFormatInfo = static_cast<int> (sizeof (kFormatInfo) / sizeof (kFormatInfo[0]));

} // namespace contrail::ui
