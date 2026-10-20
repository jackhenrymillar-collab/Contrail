#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace contrail
{

class ContrailProcessor;

// Placeholder panel: every parameter as a plain control, so the plugin can be used and
// validated while the "Above the clouds" panel is built (milestone 2).
class ContrailEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ContrailEditor (ContrailProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::GenericAudioProcessorEditor generic;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContrailEditor)
};

} // namespace contrail
