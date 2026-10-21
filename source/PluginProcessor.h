#pragma once

#include "FormatMemory.h"
#include "Parameters.h"
#include "PresetManager.h"
#include "dsp/TapeEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace contrail
{

class ContrailProcessor final : public juce::AudioProcessor,
                                private juce::AudioProcessorValueTreeState::Listener,
                                private juce::AsyncUpdater
{
public:
    ContrailProcessor();
    ~ContrailProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    // Hosts see the factory presets as programs.
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
    PresetManager& getPresets() noexcept { return presets; }

    // The panel's size, remembered with the session.
    float getUiScale() const noexcept { return uiScale.load(); }
    void setUiScale (float s) noexcept { uiScale = s; }

    // Tape speed for the panel (1 = running, 0 = stopped).
    float getTransportSpeed() const noexcept { return transportSpeed.load(); }

private:
    dsp::EngineParams readParams() const noexcept;

    // Per-format knob memory (see FormatMemory.h). Format changes are handled on the
    // message thread; preset loads and session restores set the memory directly.
    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override;
    void beginBulkChange();
    void endBulkChange();
    int currentFormat() const noexcept;
    FormatMemory::Knobs readKnobs() const noexcept;
    void applyKnobs (const FormatMemory::Knobs&);
    void writeMemory (juce::ValueTree&) const;
    void readMemory (const juce::ValueTree&);

    juce::AudioProcessorValueTreeState state;
    PresetManager presets { state };
    std::atomic<float> uiScale { 1.0f };
    std::atomic<float> transportSpeed { 1.0f };
    dsp::TapeEngine engine;

    FormatMemory memory { static_cast<std::size_t> (formatNames().size()) };
    mutable juce::CriticalSection memoryLock;
    std::atomic<bool> holdMemory { false };
    std::array<std::atomic<float>*, FormatMemory::kNumKnobs> knobs {};

    // Cached raw parameter pointers: lock-free reads on the audio thread.
    std::atomic<float>* format = nullptr;
    std::atomic<float>* wow = nullptr;
    std::atomic<float>* flutter = nullptr;
    std::atomic<float>* drift = nullptr;
    std::atomic<float>* drive = nullptr;
    std::atomic<float>* age = nullptr;
    std::atomic<float>* tone = nullptr;
    std::atomic<float>* mix = nullptr;
    std::atomic<float>* crunch = nullptr;
    std::atomic<float>* chorus = nullptr;
    std::atomic<float>* tapeStop = nullptr;
    std::atomic<float>* sync = nullptr;
    std::atomic<float>* noise = nullptr;
    std::atomic<float>* rate = nullptr;
    std::atomic<float>* stereo = nullptr;
    std::atomic<float>* output = nullptr;
    std::atomic<float>* bias = nullptr;
    std::atomic<float>* bypass = nullptr;
    juce::AudioProcessorParameter* bypassParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContrailProcessor)
};

} // namespace contrail
