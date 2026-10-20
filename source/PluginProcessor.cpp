#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace contrail
{

namespace
{
    // Parameter IDs of the knobs each format remembers, in FormatMemory order.
    constexpr std::array<const char*, FormatMemory::kNumKnobs> kKnobIds {
        ParamID::wow, ParamID::flutter, ParamID::drift, ParamID::drive,
        ParamID::age, ParamID::tone, ParamID::mix };
    constexpr auto kMemoryTag = "FORMAT_KNOBS";
}

ContrailProcessor::ContrailProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "Contrail", createParameterLayout())
{
    format   = state.getRawParameterValue (ParamID::format);
    wow      = state.getRawParameterValue (ParamID::wow);
    flutter  = state.getRawParameterValue (ParamID::flutter);
    drift    = state.getRawParameterValue (ParamID::drift);
    drive    = state.getRawParameterValue (ParamID::drive);
    age      = state.getRawParameterValue (ParamID::age);
    tone     = state.getRawParameterValue (ParamID::tone);
    mix      = state.getRawParameterValue (ParamID::mix);
    tapeStop = state.getRawParameterValue (ParamID::tapeStop);
    sync     = state.getRawParameterValue (ParamID::sync);
    noise    = state.getRawParameterValue (ParamID::noise);
    rate     = state.getRawParameterValue (ParamID::rate);
    stereo   = state.getRawParameterValue (ParamID::stereo);
    output   = state.getRawParameterValue (ParamID::output);
    bias     = state.getRawParameterValue (ParamID::bias);
    bypass   = state.getRawParameterValue (ParamID::bypass);
    bypassParam = state.getParameter (ParamID::bypass);
    knobs = { wow, flutter, drift, drive, age, tone, mix };

    // Formats start from their factory preset's knobs until you change them.
    for (int f = 0; f < static_cast<int> (memory.numFormats()); ++f)
        if (const auto k = presets.getFactoryKnobs (f))
            memory.store (f, *k);
    memory.setCurrent (currentFormat());
    memory.store (currentFormat(), readKnobs());

    presets.onBeforeApply = [this] { beginBulkChange(); };
    presets.onAfterApply = [this] { endBulkChange(); };
    state.addParameterListener (ParamID::format, this);
}

ContrailProcessor::~ContrailProcessor()
{
    state.removeParameterListener (ParamID::format, this);
    cancelPendingUpdate();
}

int ContrailProcessor::currentFormat() const noexcept
{
    return juce::roundToInt (format->load());
}

FormatMemory::Knobs ContrailProcessor::readKnobs() const noexcept
{
    FormatMemory::Knobs k {};
    for (std::size_t i = 0; i < k.size(); ++i)
        k[i] = knobs[i]->load();
    return k;
}

void ContrailProcessor::applyKnobs (const FormatMemory::Knobs& k)
{
    for (std::size_t i = 0; i < k.size(); ++i)
        if (auto* param = state.getParameter (kKnobIds[i]))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (k[i]));
            param->endChangeGesture();
        }
}

void ContrailProcessor::parameterChanged (const juce::String&, float)
{
    // May arrive on the audio thread (host automation), so the work is deferred.
    if (! holdMemory.load())
        triggerAsyncUpdate();
}

void ContrailProcessor::handleAsyncUpdate()
{
    const juce::ScopedLock sl (memoryLock);
    if (holdMemory.load())
        return;
    if (const auto restored = memory.switchTo (currentFormat(), readKnobs()))
    {
        holdMemory = true;
        applyKnobs (*restored);
        holdMemory = false;
    }
}

void ContrailProcessor::beginBulkChange()
{
    // Finish any format change still waiting, then keep the knobs of the format being
    // left before the preset overwrites them.
    if (juce::MessageManager::existsAndIsCurrentThread())
        handleUpdateNowIfNeeded();
    const juce::ScopedLock sl (memoryLock);
    holdMemory = true;
    memory.store (memory.current(), readKnobs());
}

void ContrailProcessor::endBulkChange()
{
    // The preset's knobs become the memory of the format it selected.
    cancelPendingUpdate();
    const juce::ScopedLock sl (memoryLock);
    memory.setCurrent (currentFormat());
    memory.store (currentFormat(), readKnobs());
    holdMemory = false;
}

void ContrailProcessor::writeMemory (juce::ValueTree& tree) const
{
    juce::ValueTree node (kMemoryTag);
    const juce::ScopedLock sl (memoryLock);
    for (int f = 0; f < static_cast<int> (memory.numFormats()); ++f)
        if (const auto k = memory.recall (f))
        {
            juce::ValueTree slot ("FORMAT");
            slot.setProperty ("index", f, nullptr);
            for (std::size_t i = 0; i < k->size(); ++i)
                slot.setProperty (kKnobIds[i], (*k)[i], nullptr);
            node.appendChild (slot, nullptr);
        }
    tree.appendChild (node, nullptr);
}

void ContrailProcessor::readMemory (const juce::ValueTree& node)
{
    for (const auto& slot : node)
    {
        const int f = slot.getProperty ("index", -1);
        if (f < 0 || f >= static_cast<int> (memory.numFormats()))
            continue;
        // Anything missing keeps what the format already had.
        auto k = memory.recall (f).value_or (readKnobs());
        for (std::size_t i = 0; i < k.size(); ++i)
            k[i] = static_cast<float> (slot.getProperty (kKnobIds[i], k[i]));
        memory.store (f, k);
    }
}

bool ContrailProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono-in/stereo-out and stereo/stereo.
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void ContrailProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setParams (readParams());
    engine.prepare (sampleRate, juce::jmax (1, samplesPerBlock));
    setLatencySamples (engine.getLatencySamples());
}

void ContrailProcessor::reset()
{
    engine.reset();
}

dsp::EngineParams ContrailProcessor::readParams() const noexcept
{
    dsp::EngineParams p;
    p.format = juce::roundToInt (format->load());
    p.wowPercent = wow->load();
    p.flutterPercent = flutter->load();
    p.drift = drift->load() * 0.01f;
    p.driveDb = drive->load();
    p.age = age->load() * 0.01f;
    p.tone = tone->load() * 0.01f;
    p.mix = mix->load() * 0.01f;
    p.tapeStop = tapeStop->load() > 0.5f;
    p.sync = sync->load() > 0.5f;
    p.noise = noise->load() > 0.5f;
    p.syncRate = juce::roundToInt (rate->load());
    p.wide = stereo->load() > 0.5f;
    p.outputDb = output->load();
    p.bias = bias->load() * 0.01f;
    p.bypass = bypass->load() > 0.5f;
    return p;
}

void ContrailProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0)
        return;

    // Mono input: duplicate into the right channel so the dry signal is centred.
    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    dsp::HostTransport host;
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
        {
            host.valid = pos->getBpm().hasValue();
            host.bpm = pos->getBpm().orFallback (120.0);
            host.ppqPosition = pos->getPpqPosition().orFallback (0.0);
            host.playing = pos->getIsPlaying() && pos->getPpqPosition().hasValue();
        }
    engine.setHostTransport (host);
    engine.setParams (readParams());
    engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
    transportSpeed = engine.transportSpeed();
}

juce::AudioProcessorEditor* ContrailProcessor::createEditor()
{
    return new ContrailEditor (*this);
}

int ContrailProcessor::getNumPrograms()
{
    return presets.getNumFactoryPresets();
}

int ContrailProcessor::getCurrentProgram()
{
    const int index = presets.getCurrentIndex();
    return index >= 0 && index < presets.getNumFactoryPresets() ? index : 0;
}

void ContrailProcessor::setCurrentProgram (int index)
{
    if (index >= 0 && index < presets.getNumFactoryPresets())
        presets.loadPreset (index);
}

const juce::String ContrailProcessor::getProgramName (int index)
{
    return index >= 0 && index < presets.getNumFactoryPresets() ? presets.getPresetName (index) : juce::String();
}

void ContrailProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto copy = state.copyState();
    presets.writeToState (copy);
    copy.setProperty ("uiScale", uiScale.load(), nullptr);
    writeMemory (copy);
    if (auto xml = copy.createXml())
        copyXmlToBinary (*xml, destData);
}

void ContrailProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            auto memoryNode = tree.getChildWithName (kMemoryTag);
            tree.removeChild (memoryNode, nullptr);

            holdMemory = true;
            state.replaceState (tree);

            // replaceState skips a parameter whose stored value already matches, judged
            // after snapping. A switch left at a raw 0.3 (reads as off) would then never
            // be set back to exactly 0, so set every parameter explicitly.
            for (const auto& child : tree)
                if (child.hasType ("PARAM"))
                    if (auto* param = state.getParameter (child.getProperty ("id").toString()))
                        param->setValueNotifyingHost (param->convertTo0to1 (static_cast<float> (child.getProperty ("value"))));

            presets.readFromState (tree);
            uiScale = static_cast<float> (tree.getProperty ("uiScale", 1.0));

            // Sessions saved without knob memory keep the factory starting points.
            cancelPendingUpdate();
            const juce::ScopedLock sl (memoryLock);
            readMemory (memoryNode);
            memory.setCurrent (currentFormat());
            memory.store (currentFormat(), readKnobs());
            holdMemory = false;
        }
}

} // namespace contrail

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new contrail::ContrailProcessor();
}
