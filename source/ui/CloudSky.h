#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <vector>

namespace contrail::ui
{

// The animated sky behind the controls, one look per format: a sky gradient and
// glow, contrails that bend with the Wow knob, the sun, two rows of clouds rolling
// slowly to and fro, and each format's extras (stars, a light leak, scanlines and a
// rolling tracking band, a ghosted sun, haze and drizzle), with film grain on top.
//
// Changing format crossfades the looks over 1.6 s and moves the sun to that format's
// height. Bypass sinks the sun into the clouds. Everything follows the approved mockup
// (docs/mockup/AboveClouds.dc.html) and is in panel units (1040 x 620).
//
// It's drawn by the panel rather than being a component, so the panel's text sits on
// top. Gradients and clouds are cached as images and rebuilt only when the screen's
// pixel density changes; the soft contrails are rebuilt when Wow moves.
class CloudSky
{
public:
    static constexpr float width = 1040.0f, height = 620.0f;
    static constexpr int numFormats = 6;

    struct Inputs
    {
        int format = 0;
        bool bypass = false;
        float wow = 0.15f; // the Wow knob's position, 0..1
    };

    CloudSky();

    void setInputs (const Inputs&);
    void advance (double seconds);

    // With motion off the sky holds still and changes are instant.
    void setMotion (bool shouldMove) noexcept { motion = shouldMove; }

    void draw (juce::Graphics&);

    // Where the sun is right now (it glides when the format changes), in panel units.
    juce::Rectangle<float> sunBounds() const;

    // True while something is still fading, so the panel knows to keep repainting.
    bool isChanging() const;

private:
    struct Fade
    {
        float from = 0.0f, to = 0.0f;
        double start = -100.0, length = 1.6;
        void set (float target, double now, bool instant);
        float at (double now) const;
        bool settled (double now) const { return juce::exactlyEqual (from, to) || now >= start + length; }
    };

    struct Cached
    {
        juce::Image image;
        juce::Rectangle<float> area;
        float resolution = 1.0f;

        // A copy at screen resolution, made once the layer is at rest. Blitting it is
        // much cheaper than scaling up the small image every frame.
        mutable juce::Image rest;
        mutable float restScale = 0.0f;
    };

    struct Layers
    {
        Cached sky, glow, farClouds, sea, nearClouds, leak, veil, softTrails;
        bool built = false;
        float trailAmp = -1.0f;
    };

    void rebuildCaches (float pixelScale);
    void buildFormat (int format);
    void rebuildTrails (int format, float amp);
    Cached renderLayer (juce::Rectangle<float> area, float resolution, float blur, int format,
                        const std::function<void (juce::Graphics&)>&) const;
    void drawCached (juce::Graphics&, const Cached&, float opacity, juce::Point<float> offset = {}, bool atRest = false) const;
    void dropRestCopies (int format);

    void drawSkyGroup (juce::Graphics&, int format, float alpha, float sunTop);
    void drawCloudGroup (juce::Graphics&, int format, float alpha, float sunTop);
    void drawSun (juce::Graphics&, float sunTop, float opacity);
    void drawCrispTrails (juce::Graphics&, int format, float alpha) const;

    float amplitude() const noexcept { return 2.0f + inputs.wow * 10.0f; }

    Inputs inputs;
    bool motion = true, started = false;
    double now = 0.0;

    std::array<Fade, numFormats> themeFade;
    Fade sunTop, sunOpacity, grain;

    float cachedScale = 0.0f;
    std::array<Layers, numFormats> layers;
    Cached sunDisc, sunGlow, sunFlare, grainImage, bottomShade;
};

} // namespace contrail::ui
