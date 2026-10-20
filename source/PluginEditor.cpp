#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace contrail
{

ContrailEditor::ContrailEditor (ContrailProcessor& p)
    : AudioProcessorEditor (p), generic (p)
{
    addAndMakeVisible (generic);
    setSize (520, 620);
}

void ContrailEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff141428));
}

void ContrailEditor::resized()
{
    generic.setBounds (getLocalBounds());
}

} // namespace contrail
