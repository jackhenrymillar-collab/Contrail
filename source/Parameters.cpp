#include "Parameters.h"
#include "dsp/Formats.h"

namespace contrail
{

juce::StringArray formatNames()
{
    return { "Studio Reel", "Home Reel", "Cassette", "Worn Cassette", "VHS", "Dictaphone" };
}

juce::StringArray syncRateNames()
{
    return { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16" };
}

namespace
{
    using Range = juce::NormalisableRange<float>;

    Range skewed (float min, float max, float centre)
    {
        Range r { min, max };
        r.setSkewForCentre (centre);
        return r;
    }

    juce::AudioParameterFloatAttributes withText (std::function<juce::String (float)> toText)
    {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([toText] (float v, int) { return toText (v); });
    }

    juce::String signedDb (float v)
    {
        return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB";
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    constexpr int v = 1; // parameter version hint for hosts
    const auto& d = dsp::kFormatDefaults[0];

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::format, v }, "Format", formatNames(), 0));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::wow, v }, "Wow",
        skewed (0.0f, 2.0f, 0.25f), d.wowPercent,
        withText ([] (float x) { return String (x, 2) + "%"; }).withLabel ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::flutter, v }, "Flutter",
        skewed (0.0f, 1.0f, 0.12f), d.flutterPercent,
        withText ([] (float x) { return String (x, 2) + "%"; }).withLabel ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::drift, v }, "Drift",
        Range { 0.0f, 100.0f, 0.1f }, d.drift,
        withText ([] (float x) { return String (dsp::driftWord (x)); })
            .withValueFromStringFunction ([] (const String& s) { return s.getFloatValue(); })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::drive, v }, "Drive",
        Range { -6.0f, 18.0f, 0.1f }, d.driveDb, withText (signedDb).withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::age, v }, "Age",
        Range { 0.0f, 100.0f, 0.1f }, d.age,
        withText ([] (float x) { return String (dsp::ageWord (x)); })
            .withValueFromStringFunction ([] (const String& s) { return s.getFloatValue(); })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::tone, v }, "Tone",
        Range { -100.0f, 100.0f, 0.1f }, d.tone,
        withText ([] (float x) { const int i = roundToInt (x); return (i > 0 ? "+" : "") + String (i); })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::mix, v }, "Mix",
        Range { 0.0f, 100.0f, 0.1f }, d.mix,
        withText ([] (float x) { return String (roundToInt (x)) + "%"; }).withLabel ("%")));

    // Tape Stop is a switch: on stops the tape, off starts it again.
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::tapeStop, v }, "Tape Stop", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::sync, v }, "Sync", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::noise, v }, "Noise", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::rate, v }, "Sync Rate", syncRateNames(), 2));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::stereo, v }, "Stereo", StringArray { "Linked", "Wide" }, 1));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::output, v }, "Output",
        Range { -18.0f, 18.0f, 0.1f }, 0.0f, withText (signedDb).withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::bias, v }, "Bias",
        Range { 0.0f, 100.0f, 0.1f }, 50.0f,
        withText ([] (float x) {
            const int off = roundToInt (x) - 50;
            return off == 0 ? String ("Nominal") : (off > 0 ? "Over " : "Under ") + String (std::abs (off));
        })));
    // Also the host's bypass switch (see ContrailProcessor::getBypassParameter). Clicking the sun toggles it.
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::bypass, v }, "Bypass", false));

    return layout;
}

} // namespace contrail
