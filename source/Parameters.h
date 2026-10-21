#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace contrail
{

// Parameter IDs are part of saved sessions and presets: never rename one.
namespace ParamID
{
    inline constexpr auto format   = "format";
    inline constexpr auto wow      = "wow";
    inline constexpr auto flutter  = "flutter";
    inline constexpr auto drift    = "drift";
    inline constexpr auto drive    = "drive";
    inline constexpr auto age      = "age";
    inline constexpr auto tone     = "tone";
    inline constexpr auto mix      = "mix";
    inline constexpr auto crunch   = "crunch";
    inline constexpr auto chorus   = "chorus";
    inline constexpr auto tapeStop = "tapestop";
    inline constexpr auto sync     = "sync";
    inline constexpr auto noise    = "noise";
    inline constexpr auto rate     = "rate";
    inline constexpr auto stereo   = "stereo";
    inline constexpr auto output   = "output";
    inline constexpr auto bias     = "bias";
    inline constexpr auto bypass   = "bypass";
}

// The order must match dsp::FormatId. Saved sessions store the index.
juce::StringArray formatNames();

// Note values for Sync, in the order of dsp::kSyncBeats.
juce::StringArray syncRateNames();

// Stereo choices. Linked and Wide keep their old indices so saved sessions still load.
juce::StringArray stereoNames();

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace contrail
