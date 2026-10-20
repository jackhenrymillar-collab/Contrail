#include "PresetManager.h"
#include "Parameters.h"
#include "dsp/Formats.h"

namespace contrail
{

namespace
{
    constexpr auto kFileExtension = ".ctpreset";
    constexpr auto kRootTag = "ContrailPreset";

    // One starting point per format, in format order. The knobs come from
    // dsp::kFormatDefaults, so knob memory and the factory presets agree.
    constexpr const char* kFactoryNames[dsp::kNumFormats] = {
        "Golden Hour", "Sunday Reel", "Mixtape", "Attic Find", "Late Broadcast", "Field Notes" };
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s)
    : state (s)
{
    for (int f = 0; f < dsp::kNumFormats; ++f)
    {
        const auto& k = dsp::kFormatDefaults[static_cast<std::size_t> (f)];
        Preset p;
        p.name = kFactoryNames[f];
        p.values = { { ParamID::format,  static_cast<float> (f) },
                     { ParamID::wow,     k.wowPercent },
                     { ParamID::flutter, k.flutterPercent },
                     { ParamID::drift,   k.drift },
                     { ParamID::drive,   k.driveDb },
                     { ParamID::age,     k.age },
                     { ParamID::tone,    k.tone },
                     { ParamID::mix,     k.mix } };
        factory.push_back (std::move (p));
    }

    refreshUserPresets();
    setLoaded (captureValues());
}

void PresetManager::setLoaded (Values values)
{
    const juce::ScopedLock sl (listLock);
    loaded = std::move (values);
}

int PresetManager::getNumPresets() const
{
    const juce::ScopedLock sl (listLock);
    return static_cast<int> (factory.size() + user.size());
}

int PresetManager::getNumFactoryPresets() const noexcept { return static_cast<int> (factory.size()); }

juce::String PresetManager::getPresetName (int index) const
{
    const juce::ScopedLock sl (listLock);
    if (index < 0)
        return "Init";
    if (index < static_cast<int> (factory.size()))
        return factory[static_cast<std::size_t> (index)].name;
    index -= static_cast<int> (factory.size());
    if (index < static_cast<int> (user.size()))
        return user[static_cast<std::size_t> (index)].name;
    return {};
}

juce::String PresetManager::getCurrentName() const { return getPresetName (current.load()); }

bool PresetManager::isPerformanceSwitch (const juce::String& id)
{
    // Bypass and Tape Stop are things you play, not part of a sound: presets and A/B
    // leave them where they are.
    return id == ParamID::bypass || id == ParamID::tapeStop;
}

PresetManager::Values PresetManager::captureValues() const
{
    Values v;
    for (auto* param : state.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            if (! isPerformanceSwitch (ranged->getParameterID()))
                v[ranged->getParameterID()] = ranged->convertFrom0to1 (ranged->getValue());
    return v;
}

std::optional<FormatMemory::Knobs> PresetManager::getFactoryKnobs (int format) const
{
    if (format < 0 || format >= dsp::kNumFormats)
        return std::nullopt;
    const auto& k = dsp::kFormatDefaults[static_cast<std::size_t> (format)];
    return FormatMemory::Knobs { k.wowPercent, k.flutterPercent, k.drift, k.driveDb, k.age, k.tone, k.mix };
}

void PresetManager::applyValues (const Values& values)
{
    if (onBeforeApply)
        onBeforeApply();
    for (auto* param : state.processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (ranged == nullptr || isPerformanceSwitch (ranged->getParameterID()))
            continue;

        // Anything the preset leaves out goes back to its default.
        const auto it = values.find (ranged->getParameterID());
        const float normalised = it != values.end() ? ranged->convertTo0to1 (it->second)
                                                    : ranged->getDefaultValue();
        ranged->beginChangeGesture();
        ranged->setValueNotifyingHost (normalised);
        ranged->endChangeGesture();
    }

    if (onAfterApply)
        onAfterApply();
}

bool PresetManager::isModified() const
{
    Values loadedCopy;
    {
        const juce::ScopedLock sl (listLock);
        loadedCopy = loaded;
    }
    for (const auto& [id, value] : captureValues())
    {
        const auto it = loadedCopy.find (id);
        if (it == loadedCopy.end() || std::abs (it->second - value) > 1.0e-3f * std::max (1.0f, std::abs (value)))
            return true;
    }
    return false;
}

void PresetManager::loadPreset (int index)
{
    Values values;
    {
        const juce::ScopedLock sl (listLock);
        const int total = static_cast<int> (factory.size() + user.size());
        if (index < 0 || index >= total)
            return;
        values = index < static_cast<int> (factory.size())
                     ? factory[static_cast<std::size_t> (index)].values
                     : user[static_cast<std::size_t> (index) - factory.size()].values;
    }

    applyValues (values);
    setLoaded (captureValues());
    current = index;
    ++changeCount;
}

void PresetManager::loadNext()
{
    const int n = getNumPresets();
    if (n > 0)
        loadPreset ((current.load() + 1 + n) % n);
}

void PresetManager::loadPrevious()
{
    const int n = getNumPresets();
    if (n > 0)
        loadPreset (current.load() <= 0 ? n - 1 : current.load() - 1);
}

juce::File PresetManager::getUserPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile ("Audio/Presets")
#endif
        .getChildFile ("Overcast Audio/Contrail");
}

bool PresetManager::readPresetFile (const juce::File& file, Preset& out)
{
    auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName (kRootTag))
        return false;

    out.name = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
    out.file = file;
    out.values.clear();
    for (auto* p : xml->getChildWithTagNameIterator ("PARAM"))
        out.values[p->getStringAttribute ("id")] = static_cast<float> (p->getDoubleAttribute ("value"));
    return true;
}

void PresetManager::refreshUserPresets()
{
    std::vector<Preset> found;
    for (const auto& file : getUserPresetFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + kFileExtension))
    {
        Preset p;
        if (readPresetFile (file, p))
            found.push_back (std::move (p));
    }
    std::sort (found.begin(), found.end(), [] (const Preset& a, const Preset& b) { return a.name.compareNatural (b.name) < 0; });

    const auto currentName = getCurrentName();
    const bool wasUser = current.load() >= static_cast<int> (factory.size());
    {
        const juce::ScopedLock sl (listLock);
        user = std::move (found);
    }

    // Keep pointing at the same user preset after the list is re-sorted.
    if (wasUser)
    {
        int index = -1;
        for (int i = 0; i < static_cast<int> (user.size()); ++i)
            if (user[static_cast<std::size_t> (i)].name == currentName)
                index = static_cast<int> (factory.size()) + i;
        current = index;
    }
    ++changeCount;
}

bool PresetManager::saveUserPreset (const juce::String& rawName)
{
    const auto name = rawName.trim();
    if (name.isEmpty())
        return false;

    auto folder = getUserPresetFolder();
    if (! folder.createDirectory())
        return false;

    juce::XmlElement xml (kRootTag);
    xml.setAttribute ("name", name);
    xml.setAttribute ("version", 1);
    for (const auto& [id, value] : captureValues())
    {
        auto* p = xml.createNewChildElement ("PARAM");
        p->setAttribute ("id", id);
        p->setAttribute ("value", value);
    }

    const auto file = folder.getChildFile (juce::File::createLegalFileName (name) + kFileExtension);
    if (! xml.writeTo (file))
        return false;

    refreshUserPresets();
    {
        const juce::ScopedLock sl (listLock);
        for (int i = 0; i < static_cast<int> (user.size()); ++i)
            if (user[static_cast<std::size_t> (i)].file == file)
                current = static_cast<int> (factory.size()) + i;
    }
    setLoaded (captureValues());
    ++changeCount;
    return true;
}

void PresetManager::toggleAB()
{
    auto& from = onB ? slotB : slotA;
    auto& to = onB ? slotA : slotB;
    from = captureValues();
    if (to.empty())
        to = from;
    else
        applyValues (to);
    onB = ! onB;
    ++changeCount;
}

void PresetManager::writeToState (juce::ValueTree& stateCopy) const
{
    stateCopy.setProperty ("preset", current.load() >= 0 ? getCurrentName() : juce::String(), nullptr);
}

void PresetManager::readFromState (const juce::ValueTree& restored)
{
    // May be called off the message thread, so it only works out which preset is
    // current. The comparison values are filled in from the preset list.
    const auto name = restored.getProperty ("preset").toString();
    int index = -1;
    Values values;
    if (name.isNotEmpty())
    {
        const juce::ScopedLock sl (listLock);
        for (int i = 0; index < 0 && i < static_cast<int> (factory.size()); ++i)
            if (factory[static_cast<std::size_t> (i)].name == name)
                index = i;
        for (int i = 0; index < 0 && i < static_cast<int> (user.size()); ++i)
            if (user[static_cast<std::size_t> (i)].name == name)
                index = static_cast<int> (factory.size()) + i;
    }

    // Fill in defaults for anything a preset leaves out, the same way applyValues does.
    if (index >= 0)
    {
        const juce::ScopedLock sl (listLock);
        const auto& preset = index < static_cast<int> (factory.size())
                                 ? factory[static_cast<std::size_t> (index)]
                                 : user[static_cast<std::size_t> (index) - factory.size()];
        for (auto* param : state.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param); ranged != nullptr && ! isPerformanceSwitch (ranged->getParameterID()))
            {
                const auto it = preset.values.find (ranged->getParameterID());
                values[ranged->getParameterID()] = it != preset.values.end()
                    ? ranged->convertFrom0to1 (ranged->convertTo0to1 (it->second))
                    : ranged->convertFrom0to1 (ranged->getDefaultValue());
            }
    }
    else
    {
        values = captureValues();
    }

    setLoaded (std::move (values));
    current = index;
    ++changeCount;
}

} // namespace contrail
