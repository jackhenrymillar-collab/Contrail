#include "Controls.h"
#include "Theme.h"

#include <cmath>

namespace contrail::ui
{

//==============================================================================
Knob::Knob()
{
    setSliderStyle (juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity (220);
    // Holding Shift switches to velocity mode with a low sensitivity, for fine moves.
    setVelocityModeParameters (0.25, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
    setScrollWheelEnabled (true);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setRepaintsOnMouseActivity (true);
    onValueChange = [this] { repaint(); };
}

void Knob::setLabel (const juce::String& newLabel)
{
    if (label != newLabel)
    {
        label = newLabel;
        setTitle (newLabel);
        repaint();
    }
}

juce::Rectangle<float> Knob::dialBounds() const
{
    return { (static_cast<float> (getWidth()) - dialSize) * 0.5f, topMargin, dialSize, dialSize };
}

bool Knob::hitTest (int x, int y)
{
    // The dial and its text both respond, but not the empty space beside them.
    const auto d = dialBounds();
    const auto p = juce::Point<float> (static_cast<float> (x), static_cast<float> (y));
    return p.getDistanceFrom (d.getCentre()) <= dialSize * 0.55f
        || (p.y > d.getBottom() && std::abs (p.x - d.getCentreX()) < dialSize * 0.7f);
}

void Knob::rebuildDial (float pixelScale)
{
    dialScale = pixelScale;
    dial = juce::Image (juce::Image::ARGB,
                        std::max (1, juce::roundToInt (static_cast<float> (getWidth()) * pixelScale)),
                        std::max (1, juce::roundToInt ((topMargin + dialSize + 40.0f) * pixelScale)), true);
    juce::Graphics g (dial);
    g.addTransform (juce::AffineTransform::scale (pixelScale));

    const auto d = dialBounds();
    const auto c = d.getCentre();
    const float r = dialSize * 0.5f;

    // box-shadow: 0 10px 24px rgba(0,0,0,0.45)
    fillBlurredDisc (g, c.translated (0.0f, 10.0f), r, 24.0f, juce::Colours::black.withAlpha (0.45f));

    // radial-gradient(circle at 38% 30%, #5a5690, #2f2d5c 55%, #1b1a38), sized to the farthest corner.
    const juce::Point<float> light (d.getX() + 0.38f * dialSize, d.getY() + 0.30f * dialSize);
    const float reach = light.getDistanceFrom (d.getBottomRight());
    juce::ColourGradient body (juce::Colour (0xff5a5690), light, juce::Colour (0xff1b1a38), light.translated (reach, 0.0f), true);
    body.addColour (0.55, juce::Colour (0xff2f2d5c));
    g.setGradientFill (body);
    g.fillEllipse (d);

    // inset 0 1px 0 rgba(255,230,200,0.18): a thin highlight along the top edge.
    {
        juce::Path crescent;
        crescent.addEllipse (d);
        crescent.addEllipse (d.translated (0.0f, 1.0f));
        crescent.setUsingNonZeroWinding (false);
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addEllipse (d);
        g.reduceClipRegion (clip);
        g.setColour (juce::Colour (0xffffe6c8).withAlpha (0.18f));
        g.fillPath (crescent);
    }
}

void Knob::paint (juce::Graphics& g)
{
    const float pixelScale = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (std::abs (pixelScale - dialScale) > 0.01f || ! dial.isValid())
        rebuildDial (pixelScale);
    g.drawImageTransformed (dial, juce::AffineTransform::scale (1.0f / dialScale));

    const auto d = dialBounds();
    const auto c = d.getCentre();

    // border: 1px solid rgba(242,194,123,0.22), brighter while you're touching the knob.
    g.setColour (Colours::gold.withAlpha (isMouseOverOrDragging() ? 0.6f : 0.22f));
    g.drawEllipse (d.reduced (0.5f), 1.0f);

    // The glowing dot sweeps 270 degrees, from -135 to +135, 32 units from the centre.
    const float angle = juce::degreesToRadians (-135.0f + 270.0f * static_cast<float> (valueToProportionOfLength (getValue())));
    const juce::Point<float> dot (c.x + 32.0f * std::sin (angle), c.y - 32.0f * std::cos (angle));
    fillBlurredDisc (g, dot, 4.0f, 8.0f, juce::Colour (0xffffc878).withAlpha (0.9f));
    g.setColour (Colours::dot);
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (dot));

    const float w = static_cast<float> (getWidth());
    drawTrackedText (g, label.toUpperCase(), { 0.0f, d.getBottom() + 12.0f, w, 17.0f },
                     Fonts::sans (12.0f, false, 0.2f), juce::Justification::centred, Colours::label);
    const auto value = formatValue ? formatValue (getValue()) : getTextFromValue (getValue());
    drawTrackedText (g, value, { 0.0f, d.getBottom() + 36.0f, w, 22.0f },
                     Fonts::sans (15.0f), juce::Justification::centred, Colours::cream);
}

//==============================================================================
FormatButton::FormatButton (const juce::String& name) : juce::Button (name)
{
    setTitle (name);
    setClickingTogglesState (false);
}

void FormatButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();

    if (on)
    {
        // linear-gradient(180deg, #ffe0a6, #f2b36e)
        juce::ColourGradient grad (juce::Colour (0xffffe0a6), 0.0f, b.getY(), juce::Colour (0xfff2b36e), 0.0f, b.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (b, 10.0f);
        if (down)
        {
            g.setColour (juce::Colours::black.withAlpha (0.08f));
            g.fillRoundedRectangle (b, 10.0f);
        }
        g.setColour (Colours::gold);
    }
    else
    {
        g.setColour (highlighted ? juce::Colour (0xff221f4a).withAlpha (0.75f) : Colours::glass);
        g.fillRoundedRectangle (b, 10.0f);
        g.setColour (Colours::hairline.withMultipliedAlpha (highlighted ? 1.6f : 0.92f));
    }
    g.drawRoundedRectangle (b, 10.0f, 1.0f);

    drawTrackedText (g, getName(), b, Fonts::sans (13.0f, false, 0.04f), juce::Justification::centred,
                     on ? Colours::ink : Colours::cream);
}

//==============================================================================
PillToggle::PillToggle (const juce::String& caption)
{
    setButtonText (caption);
    setTitle (caption);
}

float PillToggle::preferredWidth() const
{
    // CSS: 18 of padding and a 1 unit border each side; JUCE counts the spacing after the last letter.
    return 38.0f + juce::GlyphArrangement::getStringWidth (Fonts::sans (12.0f, false, 0.2f), getButtonText());
}

void PillToggle::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = b.getHeight() * 0.5f;
    const bool on = getToggleState();

    if (on)
        g.setColour (down ? Colours::gold.darker (0.1f) : Colours::gold);
    else
        g.setColour (highlighted ? juce::Colour (0xff221f4a).withAlpha (0.75f) : juce::Colour (0xff161430).withAlpha (0.6f));
    g.fillRoundedRectangle (b, radius);

    g.setColour (on ? Colours::gold : juce::Colour (0xfff6d6b4).withAlpha (highlighted ? 0.5f : 0.3f));
    g.drawRoundedRectangle (b, radius, 1.0f);

    drawTrackedText (g, getButtonText(), b, Fonts::sans (12.0f, false, 0.2f), juce::Justification::centred,
                     on ? Colours::ink : Colours::cream);
}

//==============================================================================
HeaderButton::HeaderButton (const juce::String& caption) : juce::Button (caption)
{
    setButtonText (caption);
    setTitle (caption);
}

void HeaderButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xff12122c).withAlpha (down ? 0.8f : (highlighted ? 0.7f : 0.55f)));
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (juce::Colour (0xfff6d6b4).withAlpha (highlighted ? 0.5f : 0.3f));
    g.drawRoundedRectangle (b, 10.0f, 1.0f);

    const auto font = Fonts::sans (12.0f, false, 0.18f);
    if (litSide < 0)
    {
        drawTrackedText (g, getButtonText(), b, font, juce::Justification::centred, Colours::cream);
        return;
    }

    // "A / B" with the active side lit in gold.
    const juce::String parts[] = { "A", " / ", "B" };
    float total = 0.0f;
    float widths[3];
    for (int i = 0; i < 3; ++i)
        total += widths[i] = juce::GlyphArrangement::getStringWidth (font, parts[i]);
    total -= font.getHeight() * font.getExtraKerningFactor();

    float x = b.getCentreX() - total * 0.5f;
    for (int i = 0; i < 3; ++i)
    {
        const bool lit = i != 1 && (i == 0) == (litSide == 0);
        drawTrackedText (g, parts[i], { x, b.getY(), widths[i] + 4.0f, b.getHeight() }, font,
                         juce::Justification::centredLeft, lit ? Colours::gold : Colours::cream);
        x += widths[i];
    }
}

//==============================================================================
ArrowButton::ArrowButton (const juce::String& name, bool pointsLeft)
    : juce::Button (name), left (pointsLeft)
{
    setTitle (name);
}

void ArrowButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto b = getLocalBounds().toFloat();
    if (highlighted || down)
    {
        g.setColour (Colours::gold.withAlpha (down ? 0.18f : 0.1f));
        g.fillRoundedRectangle (b, 8.0f);
    }

    const auto c = b.getCentre();
    const float s = left ? 1.0f : -1.0f;
    juce::Path chevron;
    chevron.startNewSubPath (c.x + 2.5f * s, c.y - 5.0f);
    chevron.lineTo (c.x - 2.5f * s, c.y);
    chevron.lineTo (c.x + 2.5f * s, c.y + 5.0f);
    g.setColour (Colours::gold);
    g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
namespace
{
    // The settings at the bottom right: "RATE" in tracked caps, then the value.
    const juce::Font captionFont() { return Fonts::sans (12.0f, false, 0.2f); }
    const juce::Font valueFont() { return Fonts::sans (12.0f, false, 0.05f); }
    constexpr float kCaptionGap = 6.0f;

    // The caption is followed by a space and a 6 unit margin, like the mockup's markup.
    float captionWidth (const juce::String& caption)
    {
        return juce::GlyphArrangement::getStringWidth (captionFont(), caption.toUpperCase() + " ") + kCaptionGap;
    }

    void drawSetting (juce::Graphics& g, juce::Rectangle<float> b, const juce::String& caption, const juce::String& value, bool highlighted)
    {
        drawTrackedText (g, caption.toUpperCase(), b, captionFont(), juce::Justification::centredLeft,
                         highlighted ? Colours::cream : Colours::label);
        drawTrackedText (g, value, b.withTrimmedLeft (captionWidth (caption)), valueFont(), juce::Justification::centredLeft,
                         highlighted ? Colours::ivory : Colours::cream);
    }

    float settingWidth (const juce::String& caption, const juce::String& value)
    {
        return captionWidth (caption) + trackedTextWidth (value, valueFont()) + 2.0f;
    }
}

TextDragger::TextDragger (const juce::String& captionText) : caption (captionText)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity (200);
    setVelocityModeParameters (0.25, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setRepaintsOnMouseActivity (true);
    setTitle (caption);
    onValueChange = [this] { repaint(); };
}

float TextDragger::preferredWidth() const
{
    // Wide enough for the longest value, so the caption doesn't move while you drag.
    return settingWidth (caption, "-18.0 dB");
}

void TextDragger::paint (juce::Graphics& g)
{
    drawSetting (g, getLocalBounds().toFloat(), caption,
                 formatValue ? formatValue (getValue()) : getTextFromValue (getValue()), isMouseOverOrDragging());
}

//==============================================================================
TextToggle::TextToggle (const juce::String& captionText, const juce::String& off, const juce::String& on)
    : caption (captionText), offText (off), onText (on)
{
    setTitle (caption);
}

float TextToggle::preferredWidth() const
{
    return settingWidth (caption, getToggleState() ? onText : offText);
}

void TextToggle::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    drawSetting (g, getLocalBounds().toFloat(), caption, getToggleState() ? onText : offText, highlighted);
}

//==============================================================================
TextChoice::TextChoice (const juce::String& captionText) : juce::Button (captionText), caption (captionText)
{
    setTitle (caption);
}

void TextChoice::setValueText (const juce::String& newText)
{
    if (newText != valueText)
    {
        valueText = newText;
        setDescription (newText);
        repaint();
    }
}

float TextChoice::preferredWidth() const
{
    return settingWidth (caption, valueText);
}

void TextChoice::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    drawSetting (g, getLocalBounds().toFloat(), caption, valueText, highlighted);
}

//==============================================================================
SaveOverlay::SaveOverlay()
{
    setInterceptsMouseClicks (true, true);
    addAndMakeVisible (name);
    addAndMakeVisible (save);
    addAndMakeVisible (cancel);

    name.setFont (Fonts::sans (15.0f));
    name.setJustification (juce::Justification::centredLeft);
    name.setIndents (12, 0);
    name.setColour (juce::TextEditor::backgroundColourId, Colours::night.withAlpha (0.7f));
    name.setColour (juce::TextEditor::textColourId, Colours::ivory);
    name.setColour (juce::TextEditor::outlineColourId, Colours::gold.withAlpha (0.35f));
    name.setColour (juce::TextEditor::focusedOutlineColourId, Colours::gold.withAlpha (0.8f));
    name.setColour (juce::TextEditor::highlightColourId, Colours::gold.withAlpha (0.3f));
    name.setColour (juce::CaretComponent::caretColourId, Colours::gold);
    name.setInputRestrictions (60);
    name.setTitle ("Preset name");
    name.onReturnKey = [this] { commit(); };
    name.onEscapeKey = [this] { setVisible (false); };

    save.onClick = [this] { commit(); };
    cancel.onClick = [this] { setVisible (false); };
    setVisible (false);
}

void SaveOverlay::show (const juce::String& suggestedName)
{
    name.setText (suggestedName, false);
    name.selectAll();
    setVisible (true);
    toFront (true);
    name.grabKeyboardFocus();
}

void SaveOverlay::commit()
{
    const auto text = name.getText().trim();
    if (text.isEmpty())
        return;
    setVisible (false);
    if (onSave)
        onSave (text);
}

juce::Rectangle<float> SaveOverlay::boxBounds() const
{
    return getLocalBounds().toFloat().withSizeKeepingCentre (380.0f, 176.0f);
}

void SaveOverlay::paint (juce::Graphics& g)
{
    g.fillAll (Colours::night.withAlpha (0.6f));
    const auto box = boxBounds();
    g.setColour (juce::Colour (0xff1c1a3a).withAlpha (0.97f));
    g.fillRoundedRectangle (box, 14.0f);
    g.setColour (Colours::gold.withAlpha (0.35f));
    g.drawRoundedRectangle (box, 14.0f, 1.0f);
    drawTrackedText (g, "SAVE PRESET", box.reduced (24.0f).withHeight (16.0f),
                     Fonts::sans (12.0f, false, 0.28f), juce::Justification::centredLeft, Colours::muted);
}

void SaveOverlay::resized()
{
    const auto box = boxBounds().reduced (24.0f).toNearestInt();
    name.setBounds (box.getX(), box.getY() + 28, box.getWidth(), 40);
    save.setBounds (box.getRight() - 90, box.getBottom() - 36, 90, 36);
    cancel.setBounds (box.getRight() - 90 - 10 - 100, box.getBottom() - 36, 100, 36);
}

void SaveOverlay::mouseDown (const juce::MouseEvent& e)
{
    // Clicking outside the box cancels.
    if (! boxBounds().contains (e.position))
        setVisible (false);
}

//==============================================================================
CloudsLookAndFeel::CloudsLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1c1a3a));
    setColour (juce::PopupMenu::textColourId, Colours::cream);
    setColour (juce::PopupMenu::headerTextColourId, Colours::muted);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::gold.withAlpha (0.22f));
    setColour (juce::PopupMenu::highlightedTextColourId, Colours::ivory);
}

juce::Font CloudsLookAndFeel::getPopupMenuFont() { return Fonts::sans (15.0f); }

void CloudsLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setColour (Colours::gold.withAlpha (0.3f));
    g.drawRect (0, 0, width, height, 1);
}

void CloudsLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    const float alpha = (isMouseOver || isMouseDragging) ? 0.6f : 0.25f;
    g.setColour (Colours::cream.withAlpha (alpha));
    const auto fw = static_cast<float> (w), fh = static_cast<float> (h);
    for (float i = 0.3f; i < 1.0f; i += 0.3f)
        g.drawLine (fw * i, fh, fw, fh * i, 1.0f);
}

} // namespace contrail::ui
