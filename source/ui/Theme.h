#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Colours, fonts and drawing helpers for the "Above the clouds" panel.
// Sizes are in panel units: the panel is laid out at 1040 x 620 and scaled as a whole.
namespace contrail::ui
{

namespace Colours
{
    inline const juce::Colour gold      { 0xfff2c27b }; // buttons, arrows, lit pills
    inline const juce::Colour dot       { 0xffffd99a }; // the knob's glowing dot
    inline const juce::Colour cream     { 0xfff6ead8 }; // body text and values
    inline const juce::Colour ivory     { 0xfffbf3e8 }; // the format name
    inline const juce::Colour label     { 0xffddd4e8 }; // knob and setting captions
    inline const juce::Colour soft      { 0xffe6dcee }; // the "FORMAT" line
    inline const juce::Colour muted     { 0xffc9bfd6 }; // the preset number
    inline const juce::Colour ink       { 0xff1d1730 }; // text on gold
    inline const juce::Colour night     { 0xff141428 }; // panel background
    inline const juce::Colour glass     = juce::Colour (0xff161430).withAlpha (0.62f);
    inline const juce::Colour hairline  = juce::Colour (0xfff6d6b4).withAlpha (0.26f);
}

// The two typefaces from the mockup, embedded so the panel looks the same on every Mac.
// Both are under the SIL Open Font License (see resources/fonts).
class Fonts
{
public:
    Fonts();

    // letterSpacing is in ems, like CSS letter-spacing.
    static juce::Font serif (float px, bool semiBold = false, float letterSpacing = 0.0f);
    static juce::Font sans (float px, bool medium = false, float letterSpacing = 0.0f);

private:
    static juce::Font make (juce::Typeface::Ptr, float px, float letterSpacing);

    juce::Typeface::Ptr serifMedium, serifSemiBold, sansRegular, sansMedium;
};

// A soft disc like a CSS box-shadow: a circle of `radius` blurred with `blur`
// (the CSS blur length, so the edge softens over about that distance).
void fillBlurredDisc (juce::Graphics&, juce::Point<float> centre, float radius, float blur, juce::Colour);

// Same idea for a rounded rectangle glow, drawn as a few soft strokes. Cheap enough per frame.
void drawGlow (juce::Graphics&, juce::Rectangle<float> bounds, float cornerRadius, float blur, juce::Colour);

// In-place Gaussian blur of an ARGB image. sigma is in pixels of that image.
void gaussianBlur (juce::Image&, float sigma);

// Draws text with CSS-style letter spacing, without the trailing gap the spacing adds.
void drawTrackedText (juce::Graphics&, const juce::String&, juce::Rectangle<float>, const juce::Font&,
                      juce::Justification, juce::Colour);
float trackedTextWidth (const juce::String&, const juce::Font&);

} // namespace contrail::ui
