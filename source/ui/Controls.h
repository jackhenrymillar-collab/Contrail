#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace contrail::ui
{

// A knob from the mockup: the 86-unit dial with its glowing dot, plus its label and
// value underneath. Drag up/down (hold Shift for fine moves), scroll, or
// double-click to reset.
class Knob final : public juce::Slider
{
public:
    static constexpr float dialSize = 86.0f;
    static constexpr float topMargin = 12.0f; // room for the dial's shadow

    Knob();

    void setLabel (const juce::String&);
    std::function<juce::String (double)> formatValue;

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;

private:
    juce::Rectangle<float> dialBounds() const;
    void rebuildDial (float pixelScale);

    juce::String label;
    juce::Image dial;
    float dialScale = 0.0f;
};

// One of the six format buttons across the middle of the panel.
class FormatButton final : public juce::Button
{
public:
    explicit FormatButton (const juce::String& name);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

// The rounded switches along the bottom: Tape Stop, Sync, Noise. Gold when on.
class PillToggle final : public juce::ToggleButton
{
public:
    explicit PillToggle (const juce::String& text);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    float preferredWidth() const;
};

// Small framed buttons in the header (A / B and Save).
class HeaderButton final : public juce::Button
{
public:
    explicit HeaderButton (const juce::String& text);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    // For A / B: which letter is lit (0 = A, 1 = B, -1 = neither).
    void setLitSide (int side) { litSide = side; repaint(); }

private:
    int litSide = -1;
};

// The preset arrows: a gold chevron.
class ArrowButton final : public juce::Button
{
public:
    ArrowButton (const juce::String& name, bool pointsLeft);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool left;
};

// Text that you drag up and down to change, like "OUTPUT  0.0 dB".
class TextDragger final : public juce::Slider
{
public:
    explicit TextDragger (const juce::String& caption);
    std::function<juce::String (double)> formatValue;
    void paint (juce::Graphics&) override;
    float preferredWidth() const;

private:
    juce::String caption;
};

// Text that flips between two choices when clicked, like "STEREO  Wide".
class TextToggle final : public juce::ToggleButton
{
public:
    TextToggle (const juce::String& caption, const juce::String& offText, const juce::String& onText);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    float preferredWidth() const;

private:
    juce::String caption, offText, onText;
};

// Text that opens a menu of choices when clicked, like "RATE  1 bar".
class TextChoice final : public juce::Button
{
public:
    explicit TextChoice (const juce::String& caption);
    void setValueText (const juce::String&);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    float preferredWidth() const;

private:
    juce::String caption, valueText;
};

// "Name this preset" box shown over the panel when Save is clicked.
class SaveOverlay final : public juce::Component
{
public:
    SaveOverlay();
    void show (const juce::String& suggestedName);
    std::function<void (const juce::String&)> onSave;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void commit();
    juce::Rectangle<float> boxBounds() const;

    juce::TextEditor name;
    HeaderButton save { "SAVE" }, cancel { "CANCEL" };
};

// Look and feel for the bits JUCE draws itself: popup menus, text editor, resizer.
class CloudsLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    CloudsLookAndFeel();
    juce::Font getPopupMenuFont() override;
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
};

} // namespace contrail::ui
