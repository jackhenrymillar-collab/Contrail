#include "PluginEditor.h"
#include "ui/FormatInfo.h"
#include "ui/ReducedMotion.h"
#include "ui/Theme.h"

namespace contrail
{

using namespace ui;

namespace
{
    constexpr int kPanelWidth = 1040, kPanelHeight = 620;
    constexpr double kFrameInterval = 1.0 / 30.0; // 30 fps is plenty for slow clouds
    constexpr float kLeft = 40.0f, kRight = 1000.0f;
    constexpr float kDimmed = 0.45f;              // controls while bypassed

    float defaultValueOf (juce::AudioProcessorValueTreeState& state, const char* id)
    {
        auto* p = state.getParameter (id);
        return p->convertFrom0to1 (p->getDefaultValue());
    }

    void setParameter (juce::AudioProcessorValueTreeState& state, const char* id, float value)
    {
        auto* p = state.getParameter (id);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }

    // CSS widths: text with its letter spacing (including the gap after the last letter).
    float cssTextWidth (const juce::String& text, const juce::Font& font)
    {
        return juce::GlyphArrangement::getStringWidth (font, text);
    }
}

//==============================================================================
// The preset number and name in the middle of the header. Click it for the full list.
class CloudPanel::PresetName final : public juce::Button
{
public:
    PresetName() : juce::Button ("Preset") { setTitle ("Preset list"); }

    void set (const juce::String& number, const juce::String& name, bool edited)
    {
        num = number;
        text = name;
        modified = edited;
        setDescription (name);
        repaint();
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto b = getLocalBounds().toFloat();
        const auto font = Fonts::sans (15.0f);
        const auto shown = text + (modified ? "*" : "");
        const float numW = num.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (font, num) + 10.0f;
        const float total = numW + juce::GlyphArrangement::getStringWidth (font, shown);
        const float x = b.getCentreX() - total * 0.5f;

        if (num.isNotEmpty())
            drawTrackedText (g, num, b.withX (x).withWidth (numW + 2.0f), font, juce::Justification::centredLeft, Colours::muted);
        drawTrackedText (g, shown, b.withX (x + numW).withRight (b.getRight()), font, juce::Justification::centredLeft,
                         highlighted ? Colours::ivory : Colours::cream);
    }

private:
    juce::String num, text;
    bool modified = false;
};

//==============================================================================
// A round, see-through button over the sun. The sky draws the sun itself; this only
// adds a faint ring on hover and catches clicks inside the disc.
class CloudPanel::SunButton final : public juce::Button
{
public:
    SunButton() : juce::Button ("Bypass")
    {
        setTitle ("Bypass");
        setClickingTogglesState (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    bool hitTest (int x, int y) override
    {
        const auto c = getLocalBounds().toFloat().getCentre();
        return c.getDistanceFrom ({ static_cast<float> (x), static_cast<float> (y) }) <= getWidth() * 0.5f;
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        if (! highlighted && ! hasKeyboardFocus (false))
            return;
        g.setColour (Colours::ivory.withAlpha (0.35f));
        g.drawEllipse (getLocalBounds().toFloat().reduced (1.5f), 1.0f);
    }
};

//==============================================================================
CloudPanel::CloudPanel (ContrailProcessor& p)
    : processor (p), presets (p.getPresets()), presetName (std::make_unique<PresetName>()),
      sunButton (std::make_unique<SunButton>())
{
    setOpaque (true);
    auto& state = processor.getState();

    // Header
    for (auto* c : std::initializer_list<juce::Component*> { &previous, &next, presetName.get(), &abButton, &saveButton })
        addAndMakeVisible (c);
    previous.onClick = [this] { presets.loadPrevious(); updatePresetDisplay(); };
    next.onClick = [this] { presets.loadNext(); updatePresetDisplay(); };
    presetName->onClick = [this] { showPresetMenu(); };
    abButton.setTooltip ("Compare two settings");
    abButton.onClick = [this] { presets.toggleAB(); updatePresetDisplay(); };
    saveButton.onClick = [this]
    {
        const auto name = presets.getCurrentIndex() >= presets.getNumFactoryPresets() ? presets.getCurrentName() : juce::String();
        saveOverlay.show (name);
    };

    // Formats
    for (int i = 0; i < kNumFormatInfo; ++i)
    {
        auto button = std::make_unique<FormatButton> (kFormatInfo[i].name);
        button->setRadioGroupId (1);
        button->onClick = [this, i] { formatAttachment->setValueAsCompleteGesture (static_cast<float> (i)); };
        addAndMakeVisible (*button);
        formatButtons.push_back (std::move (button));
    }
    formatAttachment = std::make_unique<juce::ParameterAttachment> (
        *state.getParameter (ParamID::format), [this] (float value) { setFormat (juce::roundToInt (value)); });

    // Knobs. Their values are shown with the parameters' own text (Drift and Age as words).
    const char* ids[numKnobs] = { ParamID::wow, ParamID::flutter, ParamID::drift, ParamID::drive,
                                  ParamID::age, ParamID::tone, ParamID::mix };
    const char* labels[numKnobs] = { "Wow", "Flutter", "Drift", "Drive", "Age", "Tone", "Mix" };
    for (int i = 0; i < numKnobs; ++i)
    {
        auto& knob = knobs[static_cast<size_t> (i)];
        knob.setLabel (labels[i]);
        addAndMakeVisible (knob);
        sliderAttachments.push_back (std::make_unique<SliderAttachment> (state, ids[i], knob));
        knob.setDoubleClickReturnValue (true, defaultValueOf (state, ids[i]));
    }
    knobs[wow].setTooltip ("Slow pitch drift: the reels and capstan");
    knobs[flutter].setTooltip ("Fast pitch wobble: the motor and scrape");
    knobs[drift].setTooltip ("How quickly and unevenly the wow wanders");
    knobs[drive].setTooltip ("How hard the tape is hit");
    knobs[age].setTooltip ("Worn tape: duller highs, more hiss and dropouts");

    // Bottom row
    for (auto* c : std::initializer_list<juce::Component*> { &tapeStop, &sync, &noise, &rate, &stereo, &output })
        addAndMakeVisible (c);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, ParamID::tapeStop, tapeStop));
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, ParamID::sync, sync));
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, ParamID::noise, noise));
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, ParamID::stereo, stereo));
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (state, ParamID::output, output));
    output.setDoubleClickReturnValue (true, defaultValueOf (state, ParamID::output));
    tapeStop.setTooltip ("Stop the tape; click again to start it");
    sync.setTooltip ("Lock the wow to the song's tempo");
    noise.setTooltip ("Tape hiss");
    rate.setTooltip ("How often the wow repeats when Sync is on");
    rate.onClick = [this] { showRateMenu(); };
    stereo.setTooltip ("Wide: each side wobbles a little differently");
    sync.onStateChange = [this] { updateRateText(); };

    addAndMakeVisible (*sunButton);
    sunButton->onClick = [this] { bypassAttachment->setValueAsCompleteGesture (sunButton->getToggleState() ? 1.0f : 0.0f); };
    bypassAttachment = std::make_unique<juce::ParameterAttachment> (
        *state.getParameter (ParamID::bypass), [this] (float value) { setBypassed (value > 0.5f); });

    addChildComponent (saveOverlay);
    saveOverlay.onSave = [this] (const juce::String& name)
    {
        if (! presets.saveUserPreset (name))
        {
            presetNote = "Couldn't save the preset";
            presetNoteUntil = juce::Time::getMillisecondCounterHiRes() * 0.001 + 3.0;
        }
        updatePresetDisplay();
    };

    formatAttachment->sendInitialUpdate();
    bypassAttachment->sendInitialUpdate();
    updatePresetDisplay();
    updateRateText();
    setSize (kPanelWidth, kPanelHeight);
}

CloudPanel::~CloudPanel() = default;

void CloudPanel::setFormat (int index)
{
    format = juce::jlimit (0, kNumFormatInfo - 1, index);
    for (int i = 0; i < kNumFormatInfo; ++i)
        formatButtons[static_cast<size_t> (i)]->setToggleState (i == format, juce::dontSendNotification);
    repaint();
}

void CloudPanel::setBypassed (bool shouldBypass)
{
    bypassed = shouldBypass;
    sunButton->setToggleState (bypassed, juce::dontSendNotification);
    sunButton->setTooltip (bypassed ? "Click the sun to turn Contrail back on" : "Click the sun to bypass");

    // Dim the controls while bypassed; they still work.
    const float alpha = bypassed ? kDimmed : 1.0f;
    for (auto& k : knobs)
        k.setAlpha (alpha);
    for (auto* c : std::initializer_list<juce::Component*> { &tapeStop, &sync, &noise, &rate, &stereo, &output })
        c->setAlpha (alpha);
    repaint();
}

void CloudPanel::updateRateText()
{
    auto& state = processor.getState();
    const bool synced = state.getRawParameterValue (ParamID::sync)->load() > 0.5f;
    const int index = juce::roundToInt (state.getRawParameterValue (ParamID::rate)->load());
    const auto names = syncRateNames();
    rate.setValueText (synced ? names[juce::jlimit (0, names.size() - 1, index)] : juce::String ("Free"));
}

void CloudPanel::showRateMenu()
{
    auto& state = processor.getState();
    const bool synced = state.getRawParameterValue (ParamID::sync)->load() > 0.5f;
    const int current = juce::roundToInt (state.getRawParameterValue (ParamID::rate)->load());

    juce::PopupMenu menu;
    menu.addItem (1, "Free", true, ! synced);
    menu.addSeparator();
    const auto names = syncRateNames();
    for (int i = 0; i < names.size(); ++i)
        menu.addItem (i + 2, names[i], true, synced && i == current);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&rate).withMinimumWidth (120),
                        [safe = juce::Component::SafePointer<CloudPanel> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            auto& s = safe->processor.getState();
                            // Picking a note value turns Sync on; Free turns it off.
                            if (result > 1)
                                setParameter (s, ParamID::rate, static_cast<float> (result - 2));
                            setParameter (s, ParamID::sync, result > 1 ? 1.0f : 0.0f);
                            safe->updateRateText();
                        });
}

void CloudPanel::updatePresetDisplay()
{
    const bool edited = presets.isModified();
    const int index = presets.getCurrentIndex();
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    juce::String number, name;
    if (presetNote.isNotEmpty() && now < presetNoteUntil)
        name = presetNote;
    else
    {
        presetNote.clear();
        number = index >= 0 ? juce::String (index + 1).paddedLeft ('0', 3) : juce::String();
        name = presets.getCurrentName();
    }

    const auto key = number + "|" + name + "|" + (edited ? "*" : "");
    if (key != lastPresetText || presets.isOnB() != lastAB || presets.getChangeCount() != lastPresetChange)
    {
        lastPresetText = key;
        lastAB = presets.isOnB();
        lastPresetChange = presets.getChangeCount();
        presetName->set (number, name, edited && presetNote.isEmpty());
        abButton.setLitSide (presets.isOnB() ? 1 : 0);
    }
}

void CloudPanel::showPresetMenu()
{
    presets.refreshUserPresets();

    juce::PopupMenu menu;
    const int factoryCount = presets.getNumFactoryPresets();
    const int total = presets.getNumPresets();
    const int current = presets.getCurrentIndex();

    menu.addSectionHeader ("FACTORY");
    for (int i = 0; i < factoryCount; ++i)
        menu.addItem (i + 1, juce::String (i + 1).paddedLeft ('0', 3) + "   " + presets.getPresetName (i), true, i == current);

    menu.addSectionHeader ("YOURS");
    if (total == factoryCount)
        menu.addItem (-1, "Save a preset and it shows up here", false);
    for (int i = factoryCount; i < total; ++i)
        menu.addItem (i + 1, juce::String (i + 1).paddedLeft ('0', 3) + "   " + presets.getPresetName (i), true, i == current);

    menu.addSeparator();
    constexpr int kShowFolder = 10000;
    menu.addItem (kShowFolder, "Show presets folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetName.get()).withMinimumWidth (260),
                        [safe = juce::Component::SafePointer<CloudPanel> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result == kShowFolder)
                            {
                                auto folder = PresetManager::getUserPresetFolder();
                                folder.createDirectory();
                                folder.revealToUser();
                                return;
                            }
                            safe->presets.loadPreset (result - 1);
                            safe->updatePresetDisplay();
                        });
}

void CloudPanel::frame (double seconds, bool animate)
{
    CloudSky::Inputs in;
    in.format = format;
    in.bypass = bypassed;
    in.wow = static_cast<float> (knobs[wow].valueToProportionOfLength (knobs[wow].getValue()));

    const bool changed = in.format != lastInputs.format || in.bypass != lastInputs.bypass
                      || std::abs (in.wow - lastInputs.wow) > 1.0e-4f;
    lastInputs = in;

    sky.setMotion (animate);
    sky.advance (seconds);
    sky.setInputs (in);

    const bool moving = sky.isChanging();
    sunButton->setBounds (sky.sunBounds().toNearestInt());
    if (animate || changed || moving)
        repaint();

    // The preset name changes when knobs move or the host loads a session.
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (now - lastPresetCheck > 0.2)
    {
        lastPresetCheck = now;
        updatePresetDisplay();
        updateRateText();

        // The settings sit flush right like the mockup, so their spacing follows their text.
        const float widths = rate.preferredWidth() + stereo.preferredWidth() + output.preferredWidth();
        if (std::abs (widths - settingsWidth) > 0.5f)
            resized();
    }
}

void CloudPanel::resized()
{
    // Header buttons: 40 tall, 24 from the top, flush with the right margin.
    const auto buttonFont = Fonts::sans (12.0f, false, 0.18f);
    auto buttonWidth = [&] (const juce::String& t) { return 34.0f + cssTextWidth (t, buttonFont); };
    const float saveW = buttonWidth ("SAVE"), abW = buttonWidth ("A / B");
    saveButton.setBounds (juce::Rectangle<float> (kRight - saveW, 24.0f, saveW, 40.0f).toNearestInt());
    abButton.setBounds (juce::Rectangle<float> (kRight - saveW - 8.0f - abW, 24.0f, abW, 40.0f).toNearestInt());

    // The preset box at (372, 22), 296 x 46, with its arrows inside a 1 + 6 unit inset.
    presetFrame = { 372.0f, 22.0f, 296.0f, 46.0f };
    const auto inner = presetFrame.reduced (7.0f, 5.0f);
    previous.setBounds (juce::Rectangle<float> (inner.getX(), inner.getY(), 44.0f, 36.0f).toNearestInt());
    next.setBounds (juce::Rectangle<float> (inner.getRight() - 44.0f, inner.getY(), 44.0f, 36.0f).toNearestInt());
    presetName->setBounds (juce::Rectangle<float> (inner.getX() + 44.0f, inner.getY(), inner.getWidth() - 88.0f, 36.0f).toNearestInt());

    // Format buttons: six across, 8 apart, at y = 262.
    constexpr float contentW = kRight - kLeft;
    const float buttons = static_cast<float> (formatButtons.size());
    const float colW = (contentW - (buttons - 1.0f) * 8.0f) / buttons;
    for (size_t i = 0; i < formatButtons.size(); ++i)
        formatButtons[i]->setBounds (
            juce::Rectangle<float> (kLeft + static_cast<float> (i) * (colW + 8.0f), 262.0f, colW, 44.0f).toNearestInt());

    // Knobs: seven across, 8 apart, dials starting at y = 334.
    const float knobW = (contentW - 6.0f * 8.0f) / 7.0f;
    for (int i = 0; i < numKnobs; ++i)
        knobs[static_cast<size_t> (i)].setBounds (
            juce::Rectangle<float> (kLeft + static_cast<float> (i) * (knobW + 8.0f), 334.0f - Knob::topMargin,
                                    knobW, Knob::topMargin + Knob::dialSize + 64.0f).toNearestInt());

    // Pills at y = 552, 10 apart.
    float x = kLeft;
    for (auto* pill : { &tapeStop, &sync, &noise })
    {
        const float w = pill->preferredWidth();
        pill->setBounds (juce::Rectangle<float> (x, 552.0f, w, 40.0f).toNearestInt());
        x += w + 10.0f;
    }

    // Settings on the right, 28 apart, text centred on y = 570.5.
    settingsWidth = rate.preferredWidth() + stereo.preferredWidth() + output.preferredWidth();
    float rx = kRight;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &output, &stereo, &rate })
    {
        const float w = c == &output ? output.preferredWidth() : (c == &stereo ? stereo.preferredWidth() : rate.preferredWidth());
        c->setBounds (juce::Rectangle<float> (rx - w, 556.0f, w, 29.0f).toNearestInt());
        rx -= w + 28.0f;
    }

    sunButton->setBounds (sky.sunBounds().toNearestInt());
    saveOverlay.setBounds (getLocalBounds());
}

void CloudPanel::paint (juce::Graphics& g)
{
    sky.draw (g);

    // Logo: the sun on the horizon with a contrail above, then the name.
    {
        static const juce::Path sun = juce::Drawable::parseSVGPath ("M5 17 A8 8 0 0 1 21 17 Z");
        static const juce::Path trail = juce::Drawable::parseSVGPath ("M3 9 C9 6 15 11 23 6");
        const auto at = juce::AffineTransform::translation (kLeft, 31.0f);
        g.setColour (juce::Colour (0xfff6c27e));
        g.fillPath (sun, at);
        g.setColour (Colours::cream);
        g.drawLine (kLeft + 2.0f, 48.5f, kLeft + 24.0f, 48.5f, 1.4f);
        g.setColour (Colours::cream.withAlpha (0.8f));
        g.strokePath (trail, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), at);
        drawTrackedText (g, "CONTRAIL", { kLeft + 38.0f, 26.0f, 260.0f, 36.0f }, Fonts::serif (30.0f, false, 0.32f),
                         juce::Justification::centredLeft, Colours::cream);
    }

    // Preset box frame.
    g.setColour (juce::Colour (0xff12122c).withAlpha (0.55f));
    g.fillRoundedRectangle (presetFrame, 12.0f);
    g.setColour (juce::Colour (0xfff6d6b4).withAlpha (0.28f));
    g.drawRoundedRectangle (presetFrame.reduced (0.5f), 12.0f, 1.0f);

    // Format name and description.
    const auto& info = kFormatInfo[format];
    const auto label = bypassed ? juce::String ("BYPASSED")
                                : juce::String::fromUTF8 ("FORMAT \xc2\xb7 ") + juce::String (info.look).toUpperCase();
    drawTrackedText (g, label, { kLeft, 112.0f, 600.0f, 17.0f },
                     Fonts::sans (12.0f, false, 0.3f), juce::Justification::centredLeft, Colours::soft);
    drawTrackedText (g, info.name, { kLeft, 133.0f, 640.0f, 55.0f }, Fonts::serif (52.0f),
                     juce::Justification::centredLeft, Colours::ivory);
    drawTrackedText (g, info.description, { kLeft, 192.0f, 700.0f, 22.0f }, Fonts::sans (15.0f),
                     juce::Justification::centredLeft, Colours::cream);

    // The selected format's glow sits under its button, like a CSS box-shadow.
    drawGlow (g, formatButtons[static_cast<size_t> (format)]->getBounds().toFloat(), 10.0f, 18.0f,
              juce::Colour (0xffffbe6e).withAlpha (0.35f));
}

//==============================================================================
ContrailEditor::ContrailEditor (ContrailProcessor& p)
    : AudioProcessorEditor (p), processor (p), panel (p),
      vblank (this, [this] (double t) { onFrame (t); })
{
    // Read the saved size first: setting the limits below calls resized(), which stores a new one.
    const float scale = juce::jlimit (0.7f, 1.5f, processor.getUiScale());

    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (panel);

    setResizable (true, true);
    setResizeLimits (kPanelWidth * 7 / 10, kPanelHeight * 7 / 10, kPanelWidth * 3 / 2, kPanelHeight * 3 / 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (static_cast<double> (kPanelWidth) / kPanelHeight);

    motion = ! prefersReducedMotion();
    panel.frame (0.0, motion);
    setSize (juce::roundToInt (kPanelWidth * scale), juce::roundToInt (kPanelHeight * scale));
}

ContrailEditor::~ContrailEditor()
{
    setLookAndFeel (nullptr);
}

void ContrailEditor::resized()
{
    const float scale = static_cast<float> (getWidth()) / kPanelWidth;
    panel.setTransform (juce::AffineTransform::scale (scale));
    processor.setUiScale (scale);
}

void ContrailEditor::onFrame (double timestamp)
{
    if (lastFrame < 0.0)
        lastFrame = timestamp;
    const double dt = timestamp - lastFrame;
    if (dt < kFrameInterval - 0.004)
        return;
    lastFrame = timestamp;

    // Check the accessibility setting about once a second; it's cheap but not free.
    if (timestamp - lastMotionCheck > 1.0)
    {
        lastMotionCheck = timestamp;
        const bool wanted = ! prefersReducedMotion();
        if (wanted != motion)
        {
            motion = wanted;
            panel.repaint();
        }
    }

    // Pause the clock rather than jump ahead after the window was hidden. The clock keeps
    // running with Reduce Motion on so format changes still finish; the sky just holds still.
    animationTime += std::min (dt, 0.1);
    panel.frame (animationTime, motion);
}

} // namespace contrail
