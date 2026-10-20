#pragma once

#include "PluginProcessor.h"
#include "ui/CloudSky.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>

namespace contrail
{

// The "Above the clouds" front panel, laid out at 1040 x 620 units to match the mockup
// (docs/mockup/AboveClouds.dc.html). The editor scales it as a whole when resized.
class CloudPanel final : public juce::Component
{
public:
    explicit CloudPanel (ContrailProcessor&);
    ~CloudPanel() override;

    // Called once per display frame. `seconds` is the animation clock.
    void frame (double seconds, bool animate);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class PresetName;
    class SunButton;

    void setFormat (int index);
    void setBypassed (bool);
    void showPresetMenu();
    void showRateMenu();
    void updatePresetDisplay();
    void updateRateText();

    ContrailProcessor& processor;
    PresetManager& presets;
    ui::CloudSky sky;
    int format = 0;
    bool bypassed = false;
    ui::CloudSky::Inputs lastInputs;

    // Header
    ui::ArrowButton previous { "Previous preset", true }, next { "Next preset", false };
    std::unique_ptr<PresetName> presetName;
    ui::HeaderButton abButton { "A / B" }, saveButton { "SAVE" };
    juce::Rectangle<float> presetFrame;

    // Formats and knobs
    std::vector<std::unique_ptr<ui::FormatButton>> formatButtons;
    std::unique_ptr<juce::ParameterAttachment> formatAttachment;

    enum KnobId { wow, flutter, drift, drive, age, tone, mix, numKnobs };
    std::array<ui::Knob, numKnobs> knobs;

    // Bottom row
    ui::PillToggle tapeStop { "TAPE STOP" }, sync { "SYNC" }, noise { "NOISE" };
    ui::TextChoice rate { "Rate" };
    ui::TextToggle stereo { "Stereo", "Linked", "Wide" };
    ui::TextDragger output { "Output" };

    ui::SaveOverlay saveOverlay;

    // The sun is the bypass switch.
    std::unique_ptr<SunButton> sunButton;
    std::unique_ptr<juce::ParameterAttachment> bypassAttachment;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

    int lastPresetChange = -1;
    juce::String lastPresetText;
    bool lastAB = false;
    double lastPresetCheck = 0.0;
    juce::String presetNote;
    double presetNoteUntil = 0.0;
    float settingsWidth = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CloudPanel)
};

class ContrailEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ContrailEditor (ContrailProcessor&);
    ~ContrailEditor() override;

    void resized() override;

    // Draws the panel at a given animation time. For the snapshot tool.
    void renderFrame (double seconds, bool animate) { panel.frame (seconds, animate); }

private:
    void onFrame (double timestamp);

    ContrailProcessor& processor;
    ui::CloudsLookAndFeel lookAndFeel;
    CloudPanel panel;
    juce::VBlankAttachment vblank;

    double lastFrame = -1.0, animationTime = 0.0, lastMotionCheck = -10.0;
    bool motion = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContrailEditor)
};

} // namespace contrail
