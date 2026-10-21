#include "CloudSky.h"
#include "Theme.h"

#include <cmath>

namespace contrail::ui
{

namespace
{
    constexpr float kPi = juce::MathConstants<float>::pi;
    constexpr double kChange = 1.6;     // every change in the mockup is a 1.6 s transition
    constexpr float kSoftRes = 0.5f;    // blurred contrails are cached at half panel resolution
    constexpr float kBypassSunTop = 236.0f;
    constexpr float kSunLeft = 820.0f, kSunSize = 100.0f;
    constexpr int kWorn = 3;

    juce::Colour hex (const char* s) { return juce::Colour::fromString (juce::String ("ff") + (s + 1)); }
    juce::Colour rgba (int r, int g, int b, float a)
    {
        return juce::Colour (static_cast<juce::uint8> (r), static_cast<juce::uint8> (g), static_cast<juce::uint8> (b), a);
    }

    float easeInOut (float t) { return 0.5f - 0.5f * std::cos (kPi * juce::jlimit (0.0f, 1.0f, t)); }

    // Where a looping CSS animation of `period` seconds is at time t, as 0..1.
    // `delay` is the CSS animation-delay (negative starts part way through).
    float cycle (double t, float period, float delay)
    {
        const double u = std::fmod (t - static_cast<double> (delay), static_cast<double> (period)) / period;
        return static_cast<float> (u < 0.0 ? u + 1.0 : u);
    }

    // A CSS "ease-in-out infinite alternate" animation: there and back again.
    float alternate (double t, float period)
    {
        const double u = std::fmod (t, 2.0 * period) / period;
        return easeInOut (static_cast<float> (u < 1.0 ? u : 2.0 - u));
    }

    // Keyframes that go a -> b -> a over one period (the mockup's glow, twinkle and plane).
    float pulse (double t, float period, float delay, float a, float b)
    {
        const float u = cycle (t, period, delay);
        return a + (b - a) * easeInOut (1.0f - std::abs (2.0f * u - 1.0f));
    }

    // Fills `area` with an elliptical CSS radial-gradient (radii rx, ry at centre).
    void ellipseGradient (juce::Graphics& g, juce::Rectangle<float> area, juce::Point<float> centre, float rx, float ry,
                          std::initializer_list<std::pair<float, juce::Colour>> stops)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (area.getSmallestIntegerContainer());
        g.addTransform (juce::AffineTransform::scale (1.0f, ry / rx, centre.x, centre.y));
        juce::ColourGradient grad (stops.begin()->second, centre, (stops.end() - 1)->second, centre.translated (rx, 0.0f), true);
        for (const auto& [pos, colour] : stops)
            grad.addColour (juce::jlimit (0.0, 1.0, static_cast<double> (pos)), colour);
        g.setGradientFill (grad);
        const float span = (std::max (area.getWidth(), area.getHeight()) + std::abs (centre.y - area.getCentreY()) * 2.0f)
                           * rx / std::min (rx, ry) + 4000.0f;
        g.fillRect (juce::Rectangle<float> (span, span).withCentre (centre));
    }

    // radial-gradient(closest-side, ...) filling a box.
    void closestSide (juce::Graphics& g, juce::Rectangle<float> box, std::initializer_list<std::pair<float, juce::Colour>> stops)
    {
        ellipseGradient (g, box, box.getCentre(), box.getWidth() * 0.5f, box.getHeight() * 0.5f, stops);
    }

    // A vertical CSS linear-gradient over `area`, stops measured from the top.
    void verticalGradient (juce::Graphics& g, juce::Rectangle<float> area, std::initializer_list<std::pair<float, juce::Colour>> stops)
    {
        juce::ColourGradient grad (stops.begin()->second, area.getX(), area.getY(),
                                   (stops.end() - 1)->second, area.getX(), area.getBottom(), false);
        for (const auto& [pos, colour] : stops)
            grad.addColour (static_cast<double> (pos), colour);
        g.setGradientFill (grad);
        g.fillRect (area);
    }

    //==========================================================================
    // The mockup's six skies (kept in step with docs/mockup/AboveClouds.dc.html).
    struct Sky
    {
        float sun, sunOpacity, grain, trail, blur;
        bool stars, leak, scan, ghost, veil;
        const char* sky[5];   // linear-gradient stops at 0, 40, 64-66, 88-90 and 100 %
        float skyStops[5];
        juce::Colour glow[2];
        const char* far[2];
        const char* near[2];
        const char* sea;
    };

    const Sky& skyFor (int format)
    {
        static const Sky skies[] = {
            { 214, 1.0f, 0.05f, 0.95f, 2.4f, true, false, false, false, false,
              { "#0f1640", "#23306a", "#4b4a86", "#c07a86", "#f2b08a" }, { 0, 0.40f, 0.66f, 0.90f, 1 },
              { rgba (255, 214, 150, 0.5f), rgba (250, 160, 120, 0.22f) }, { "#f6c7b0", "#a985a6" }, { "#8f78a8", "#3e3a6e" }, "#2a2650" },
            { 196, 1.0f, 0.06f, 0.9f, 2.6f, false, false, false, false, false,
              { "#1a1838", "#3b2f5e", "#6e4566", "#d9805e", "#ffc46e" }, { 0, 0.40f, 0.64f, 0.88f, 1 },
              { rgba (255, 200, 110, 0.7f), rgba (240, 140, 80, 0.32f) }, { "#ffd99a", "#d98f72" }, { "#a8789a", "#4a3560" }, "#2e2246" },
            { 176, 1.0f, 0.06f, 0.85f, 2.8f, false, false, false, false, false,
              { "#1e2552", "#3a4683", "#63639f", "#c99ac0", "#ffd3c4" }, { 0, 0.45f, 0.66f, 0.88f, 1 },
              { rgba (255, 226, 226, 0.55f), rgba (230, 170, 210, 0.3f) }, { "#ffe4ec", "#d6a8c8" }, { "#b39bcc", "#5a4f8a" }, "#2c2a5a" },
            { 160, 1.0f, 0.26f, 0.6f, 5.0f, false, true, false, false, false,
              { "#221c34", "#43345a", "#74526a", "#d0896a", "#f6c48a" }, { 0, 0.40f, 0.64f, 0.88f, 1 },
              { rgba (255, 206, 140, 0.6f), rgba (230, 150, 100, 0.3f) }, { "#f6d2a8", "#c9967a" }, { "#a08090", "#4a3a52" }, "#2e2436" },
            { 140, 1.0f, 0.1f, 0.8f, 2.4f, false, false, true, true, false,
              { "#120f30", "#2c1f5a", "#4e3484", "#c45a9a", "#ff9a8a" }, { 0, 0.40f, 0.64f, 0.88f, 1 },
              { rgba (255, 150, 170, 0.55f), rgba (200, 90, 200, 0.3f) }, { "#ffc2cf", "#b87ab8" }, { "#8a6ab8", "#3a2a6a" }, "#22184a" },
            { 118, 0.55f, 0.12f, 0.35f, 3.0f, false, false, false, false, true,
              { "#171b2a", "#2c3248", "#4c5268", "#9a9aa6", "#d8c8b8" }, { 0, 0.40f, 0.64f, 0.88f, 1 },
              { rgba (240, 226, 200, 0.4f), rgba (200, 190, 190, 0.18f) }, { "#d9d3d2", "#8f8b99" }, { "#6e6c84", "#2e2d42" }, "#262838" },
        };
        return skies[juce::jlimit (0, CloudSky::numFormats - 1, format)];
    }

    // The ten stars on Studio Reel: x, y, radius, delay.
    constexpr float kStars[][4] = { { 60, 18, 1.5f, 0 }, { 140, 52, 1, 1.2f }, { 230, 14, 1.5f, 2.6f }, { 318, 70, 1, 0.8f },
                                    { 420, 30, 1, 3.1f }, { 512, 12, 1.5f, 1.9f }, { 610, 48, 1, 0.4f }, { 700, 20, 1, 2.2f },
                                    { 960, 16, 1, 1.4f }, { 86, 96, 1, 4.1f } };

    //==========================================================================
    // Worn Cassette's sky is a faded photograph: CSS sepia(0.5) saturate(0.7) contrast(0.92).
    void fadePhoto (float& r, float& g, float& b)
    {
        constexpr float a = 0.5f, k = 1.0f - a;
        const float r1 = (0.393f + 0.607f * k) * r + (0.769f - 0.769f * k) * g + (0.189f - 0.189f * k) * b;
        const float g1 = (0.349f - 0.349f * k) * r + (0.686f + 0.314f * k) * g + (0.168f - 0.168f * k) * b;
        const float b1 = (0.272f - 0.272f * k) * r + (0.534f - 0.534f * k) * g + (0.131f + 0.869f * k) * b;
        constexpr float s = 0.7f;
        const float r2 = (0.213f + 0.787f * s) * r1 + (0.715f - 0.715f * s) * g1 + (0.072f - 0.072f * s) * b1;
        const float g2 = (0.213f - 0.213f * s) * r1 + (0.715f + 0.285f * s) * g1 + (0.072f - 0.072f * s) * b1;
        const float b2 = (0.213f - 0.213f * s) * r1 + (0.715f - 0.715f * s) * g1 + (0.072f + 0.928f * s) * b1;
        constexpr float c = 0.92f;
        r = juce::jlimit (0.0f, 1.0f, (r2 - 0.5f) * c + 0.5f);
        g = juce::jlimit (0.0f, 1.0f, (g2 - 0.5f) * c + 0.5f);
        b = juce::jlimit (0.0f, 1.0f, (b2 - 0.5f) * c + 0.5f);
    }

    juce::Colour fadePhoto (juce::Colour colour)
    {
        float r = colour.getFloatRed(), g = colour.getFloatGreen(), b = colour.getFloatBlue();
        fadePhoto (r, g, b);
        return juce::Colour::fromFloatRGBA (r, g, b, colour.getFloatAlpha());
    }

    void fadePhoto (juce::Image& image)
    {
        const juce::Image::BitmapData bd (image, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < bd.height; ++y)
            for (int x = 0; x < bd.width; ++x)
            {
                auto* p = reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y));
                const auto alpha = p->getAlpha();
                if (alpha == 0)
                    continue;
                p->unpremultiply();
                float r = p->getRed() / 255.0f, g = p->getGreen() / 255.0f, b = p->getBlue() / 255.0f;
                fadePhoto (r, g, b);
                auto byte = [] (float v) { return static_cast<juce::uint8> (juce::roundToInt (v * 255.0f)); };
                p->setARGB (alpha, byte (r), byte (g), byte (b));
                p->premultiply();
            }
    }

    // The contrail shape from the mockup: a line from (x0, y0) to (x1, y1) with a
    // sine wobble of `amp` that fades out towards the far end.
    juce::Path wave (float x0, float y0, float x1, float y1, float amp, float n)
    {
        juce::Path p;
        p.startNewSubPath (x0, y0);
        for (int i = 1; i <= 40; ++i)
        {
            const float t = static_cast<float> (i) / 40.0f;
            p.lineTo (x0 + (x1 - x0) * t, y0 + (y1 - y0) * t + std::sin (t * kPi * n) * amp * (1.0f - t));
        }
        return p;
    }

    // The contrail stroke: transparent at the far (left) end, bright at the plane.
    juce::ColourGradient trailGradient (float left, float right, bool faded)
    {
        auto c = [faded] (juce::Colour colour) { return faded ? fadePhoto (colour) : colour; };
        juce::ColourGradient grad (c (hex ("#ffe9dc").withAlpha (0.0f)), left, 0.0f, c (hex ("#fff6ee").withAlpha (0.95f)), right, 0.0f, false);
        grad.addColour (0.6, c (hex ("#ffe9dc").withAlpha (0.5f)));
        return grad;
    }

    // One cloud: an ellipse filled with radial-gradient(ellipse at 50% 30%, top, mid 60%, clear).
    void blob (juce::Graphics& g, float x, float y, float w, float h, juce::Colour top, juce::Colour mid, float alpha)
    {
        const juce::Rectangle<float> r (x, y, w, h);
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addEllipse (r);
        g.reduceClipRegion (clip);
        // CSS sizes this gradient to the farthest corner, keeping the closest-side aspect ratio.
        ellipseGradient (g, r, { x + 0.5f * w, y + 0.3f * h }, 1.269f * w, 0.7616f * h,
                         { { 0.0f, top.withMultipliedAlpha (alpha) }, { 0.6f, mid.withMultipliedAlpha (alpha) },
                           { 1.0f, rgba (40, 36, 70, 0.0f) } });
    }
}

//==============================================================================
void CloudSky::Fade::set (float target, double t, bool instant)
{
    if (instant)
    {
        from = to = target;
        start = t - 1000.0;
        return;
    }
    if (juce::exactlyEqual (target, to))
        return;
    from = at (t);
    to = target;
    start = t;
}

float CloudSky::Fade::at (double t) const
{
    return from + (to - from) * easeInOut (static_cast<float> ((t - start) / length));
}

//==============================================================================
CloudSky::CloudSky()
{
    for (auto& f : themeFade)
        f.length = kChange;
    sunTop.length = sunOpacity.length = grain.length = kChange;
}

void CloudSky::setInputs (const Inputs& in)
{
    const bool instant = ! started || ! motion;
    inputs = in;
    inputs.format = juce::jlimit (0, numFormats - 1, in.format);
    inputs.wow = juce::jlimit (0.0f, 1.0f, in.wow);

    const auto& k = skyFor (inputs.format);
    for (int i = 0; i < numFormats; ++i)
        themeFade[static_cast<size_t> (i)].set (i == inputs.format ? 1.0f : 0.0f, now, instant);
    sunTop.set (inputs.bypass ? kBypassSunTop : k.sun, now, instant);
    sunOpacity.set (inputs.bypass ? 0.7f : k.sunOpacity, now, instant);
    grain.set (k.grain, now, instant);
    started = true;
}

void CloudSky::advance (double seconds)
{
    now = seconds;
}

bool CloudSky::isChanging() const
{
    for (const auto& f : themeFade)
        if (! f.settled (now))
            return true;
    return ! sunTop.settled (now) || ! sunOpacity.settled (now) || ! grain.settled (now);
}

juce::Rectangle<float> CloudSky::sunBounds() const
{
    return { kSunLeft, sunTop.at (now), kSunSize, kSunSize };
}

//==============================================================================
CloudSky::Cached CloudSky::renderLayer (juce::Rectangle<float> area, float resolution, float blur, int format,
                                        const std::function<void (juce::Graphics&)>& paint) const
{
    Cached c;
    c.area = area;
    c.resolution = resolution;
    c.image = juce::Image (juce::Image::ARGB, std::max (1, juce::roundToInt (area.getWidth() * resolution)),
                           std::max (1, juce::roundToInt (area.getHeight() * resolution)), true);
    {
        juce::Graphics g (c.image);
        g.addTransform (juce::AffineTransform::translation (-area.getX(), -area.getY()).scaled (resolution));
        paint (g);
    }
    if (blur > 0.0f)
        gaussianBlur (c.image, blur * resolution); // SVG stdDeviation and CSS blur() are both sigma
    if (format == kWorn)
        fadePhoto (c.image);
    return c;
}

void CloudSky::drawCached (juce::Graphics& g, const Cached& c, float opacity, juce::Point<float> offset, bool atRest) const
{
    if (opacity <= 0.002f || ! c.image.isValid())
        return;

    g.setOpacity (juce::jmin (1.0f, opacity));
    if (! atRest)
    {
        g.drawImageTransformed (c.image, juce::AffineTransform::scale (1.0f / c.resolution)
                                             .translated (c.area.getX() + offset.x, c.area.getY() + offset.y));
        g.setOpacity (1.0f);
        return;
    }

    // Only the part inside the panel is kept.
    const auto visible = c.relative ? c.area : c.area.getIntersection ({ -80.0f, -80.0f, width + 160.0f, height + 160.0f });
    if (! juce::exactlyEqual (c.restScale, cachedScale) || ! c.rest.isValid())
    {
        const auto size = (visible * cachedScale).getSmallestIntegerContainer();
        c.rest = juce::Image (juce::Image::ARGB, std::max (1, size.getWidth()), std::max (1, size.getHeight()), true);
        juce::Graphics rg (c.rest);
        rg.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        rg.addTransform (juce::AffineTransform::translation (-visible.getX(), -visible.getY()).scaled (cachedScale));
        rg.drawImageTransformed (c.image, juce::AffineTransform::scale (1.0f / c.resolution).translated (c.area.getX(), c.area.getY()));
        c.restScale = cachedScale;
    }

    // Snap to whole pixels so the copy is blitted rather than resampled.
    const float x = std::round ((visible.getX() + offset.x) * cachedScale) / cachedScale;
    const float y = std::round ((visible.getY() + offset.y) * cachedScale) / cachedScale;
    g.drawImageTransformed (c.rest, juce::AffineTransform::scale (1.0f / cachedScale).translated (x, y));
    g.setOpacity (1.0f);
}

void CloudSky::dropRestCopies (int format)
{
    auto& l = layers[static_cast<size_t> (format)];
    for (auto* c : { &l.sky, &l.glow, &l.farClouds, &l.sea, &l.nearClouds, &l.leak, &l.veil, &l.softTrails })
        if (c->rest.isValid())
        {
            c->rest = {};
            c->restScale = 0.0f;
        }
}

void CloudSky::rebuildCaches (float pixelScale)
{
    cachedScale = pixelScale;
    for (auto& l : layers)
        l = {};

    // Smooth gradients don't need full retina resolution.
    const float res = juce::jlimit (0.5f, 1.0f, pixelScale * 0.5f);
    const float sharp = juce::jlimit (1.0f, 2.0f, pixelScale);

    // The sun, its glow and the lens flare, relative to the sun's top-left corner.
    sunDisc = renderLayer ({ -72.0f, -72.0f, kSunSize + 144.0f, kSunSize + 144.0f }, sharp, 0.0f, -1, [] (juce::Graphics& g)
    {
        const juce::Rectangle<float> d (0.0f, 0.0f, kSunSize, kSunSize);
        fillBlurredDisc (g, d.getCentre(), kSunSize * 0.5f, 46.0f, rgba (255, 200, 120, 0.75f));
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addEllipse (d);
        g.reduceClipRegion (clip);
        const juce::Point<float> c (50.0f, 45.0f);
        ellipseGradient (g, d, c, 74.33f, 74.33f, { { 0.0f, hex ("#fffbe8") }, { 0.45f, hex ("#ffe7a8") },
                                                    { 0.8f, hex ("#ffbf73") }, { 1.0f, hex ("#f79a5c") } });
    });
    sunGlow = renderLayer ({ -50.0f, -50.0f, 200.0f, 200.0f }, res, 0.0f, -1, [] (juce::Graphics& g)
    {
        closestSide (g, { -50.0f, -50.0f, 200.0f, 200.0f }, { { 0.0f, rgba (255, 226, 160, 0.9f) }, { 0.55f, rgba (255, 180, 100, 0.32f) },
                                                             { 1.0f, rgba (255, 150, 80, 0.0f) } });
    });
    sunFlare = renderLayer ({ -190.0f, 40.0f, 420.0f, 20.0f }, sharp, 0.0f, -1, [] (juce::Graphics& g)
    {
        closestSide (g, { -190.0f, 40.0f, 420.0f, 20.0f }, { { 0.0f, rgba (255, 240, 200, 0.8f) }, { 1.0f, rgba (255, 200, 140, 0.0f) } });
    });

    sunDisc.relative = sunGlow.relative = sunFlare.relative = true;

    bottomShade = renderLayer ({ 0.0f, 300.0f, width, 320.0f }, res, 0.0f, -1, [] (juce::Graphics& g)
    {
        verticalGradient (g, { 0.0f, 300.0f, width, 320.0f }, { { 0.0f, rgba (20, 18, 42, 0.0f) }, { 0.3f, rgba (20, 18, 42, 0.55f) },
                                                               { 1.0f, rgba (16, 15, 34, 0.85f) } });
    });

    // Film grain: fine grey noise, drawn lightly over everything (the mockup's
    // feTurbulence in overlay mode, which mostly adds speckle to the mid tones).
    {
        Cached c;
        c.area = { 0.0f, 0.0f, width, height };
        c.resolution = 1.0f;
        c.image = juce::Image (juce::Image::ARGB, static_cast<int> (width), static_cast<int> (height), true);
        juce::Random rng (19);
        const juce::Image::BitmapData bd (c.image, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < bd.height; ++y)
            for (int x = 0; x < bd.width; ++x)
            {
                const float n = (rng.nextFloat() + rng.nextFloat() + rng.nextFloat()) / 3.0f - 0.5f; // -0.5..0.5, peaked
                const auto a = static_cast<juce::uint8> (juce::jlimit (0, 255, juce::roundToInt (std::abs (n) * 2.0f * 255.0f)));
                const auto v = static_cast<juce::uint8> (n > 0.0f ? a : 0);
                reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y))->setARGB (a, v, v, v);
            }
        grainImage = std::move (c);
    }
}

void CloudSky::buildFormat (int format)
{
    auto& l = layers[static_cast<size_t> (format)];
    const auto& k = skyFor (format);
    const float res = juce::jlimit (0.5f, 1.0f, cachedScale * 0.5f);

    l.sky = renderLayer ({ 0.0f, 0.0f, width, 330.0f }, res, 0.0f, format, [&k] (juce::Graphics& g)
    {
        juce::ColourGradient grad (hex (k.sky[0]), 0.0f, 0.0f, hex (k.sky[4]), 0.0f, 330.0f, false);
        for (int i = 1; i < 4; ++i)
            grad.addColour (static_cast<double> (k.skyStops[i]), hex (k.sky[i]));
        g.setGradientFill (grad);
        g.fillRect (0.0f, 0.0f, width, 330.0f);
    });

    l.glow = renderLayer ({ 380.0f, -40.0f, 1000.0f, 560.0f }, res * 0.5f, 0.0f, format, [&k] (juce::Graphics& g)
    {
        closestSide (g, { 380.0f, -40.0f, 1000.0f, 560.0f }, { { 0.0f, k.glow[0] }, { 0.4f, k.glow[1] },
                                                              { 0.72f, rgba (220, 120, 140, 0.06f) }, { 1.0f, rgba (220, 120, 140, 0.0f) } });
    });

    const auto far0 = hex (k.far[0]), far1 = hex (k.far[1]), near0 = hex (k.near[0]), near1 = hex (k.near[1]), sea = hex (k.sea);

    // The far row sits at (-60, 238) and drifts 40 to the right and back.
    l.farClouds = renderLayer ({ -60.0f, 238.0f, 1320.0f, 160.0f }, res, 0.0f, format, [=] (juce::Graphics& g)
    {
        g.addTransform (juce::AffineTransform::translation (-60.0f, 238.0f));
        blob (g, 0, 30, 260, 70, far0, far1, 0.9f);
        blob (g, 180, 18, 300, 90, far0, far1, 0.95f);
        blob (g, 420, 26, 280, 80, far0, far1, 0.95f);
        blob (g, 620, 8, 340, 100, far0, far1, 1.0f);
        blob (g, 880, 20, 300, 90, far0, far1, 1.0f);
        blob (g, 1060, 34, 200, 70, far0, far1, 0.9f);
        blob (g, 60, 56, 380, 70, near0, near1, 0.8f);
        blob (g, 400, 60, 420, 70, near0, near1, 0.8f);
        blob (g, 800, 54, 400, 70, near0, near1, 0.85f);
    });

    l.sea = renderLayer ({ 0.0f, 262.0f, width, 358.0f }, res, 0.0f, format, [=] (juce::Graphics& g)
    {
        verticalGradient (g, { 0.0f, 262.0f, width, 358.0f }, { { 0.0f, rgba (40, 36, 70, 0.0f) }, { 0.18f, sea.withAlpha (0.85f) },
                                                               { 0.45f, sea }, { 0.75f, hex ("#191734") }, { 1.0f, hex ("#121127") } });
    });

    // The near row sits at (-40, 268) and drifts 60 to the left and back.
    l.nearClouds = renderLayer ({ -100.0f, 268.0f, 1400.0f, 352.0f }, res, 0.0f, format, [=] (juce::Graphics& g)
    {
        g.addTransform (juce::AffineTransform::translation (-40.0f, 268.0f));
        const auto deep = hex ("#16142e");
        blob (g, -40, 10, 380, 120, near0, near1, 0.85f);
        blob (g, 260, 0, 420, 130, near0, near1, 0.85f);
        blob (g, 620, 6, 420, 120, near0, near1, 0.85f);
        blob (g, 940, 12, 300, 110, near0, near1, 0.85f);
        blob (g, 80, 70, 480, 120, near1, sea, 0.9f);
        blob (g, 520, 64, 520, 130, near1, sea, 0.9f);
        blob (g, -60, 120, 600, 110, sea, deep, 0.9f);
        blob (g, 480, 120, 700, 110, sea, deep, 0.9f);
    });

    if (k.leak)
        l.leak = renderLayer ({ 820.0f, -60.0f, 220.0f, 680.0f }, res * 0.5f, 0.0f, format, [] (juce::Graphics& g)
        {
            closestSide (g, { 820.0f, -60.0f, 300.0f, 760.0f }, { { 0.0f, rgba (255, 110, 60, 0.4f) }, { 1.0f, rgba (255, 110, 60, 0.0f) } });
        });

    // Dictaphone's haze over the sun, relative to the sun's top: box (640, +10, 460 x 90), blur 6.
    if (k.veil)
        l.veil = renderLayer ({ 620.0f, -10.0f, 500.0f, 130.0f }, res, 6.0f, format, [] (juce::Graphics& g)
        {
            closestSide (g, { 640.0f, 10.0f, 460.0f, 90.0f }, { { 0.0f, rgba (190, 192, 204, 0.6f) }, { 1.0f, rgba (190, 192, 204, 0.0f) } });
        });

    l.built = true;
}

void CloudSky::rebuildTrails (int format, float amp)
{
    auto& l = layers[static_cast<size_t> (format)];
    const auto& k = skyFor (format);
    l.trailAmp = amp;
    l.softTrails = renderLayer ({ -40.0f, -20.0f, 1120.0f, 280.0f }, kSoftRes, k.blur, format, [amp, &k] (juce::Graphics& g)
    {
        g.reduceClipRegion (0, 0, 1040, 260); // the mockup's SVG is 1040 x 260
        const auto old = wave (1060, 150, -40, 40, amp * 1.6f, 3);
        g.setColour (hex ("#f5c6c6").withAlpha (0.22f));
        g.strokePath (old, juce::PathStrokeType (10.0f + k.blur * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto a = wave (1010, 62, 160, 210, amp, 4);
        g.setGradientFill (trailGradient (160.0f, 1010.0f, false));
        g.setOpacity (0.55f);
        g.strokePath (a, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    });
}

//==============================================================================
void CloudSky::drawCrispTrails (juce::Graphics& g, int format, float alpha) const
{
    const float amp = amplitude();
    const bool faded = format == kWorn;
    const juce::PathStrokeType stroke (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (0, 0, 1040, 260);
    g.setGradientFill (trailGradient (160.0f, 1010.0f, faded));
    g.setOpacity (alpha);
    g.strokePath (wave (1010, 62, 160, 210, amp, 4), stroke);
    g.setGradientFill (trailGradient (200.0f, 1010.0f, faded));
    g.setOpacity (alpha);
    g.strokePath (wave (1010, 68, 200, 232, amp, 4), stroke);

    // The plane.
    const float blink = motion ? pulse (now, 2.4f, 0.0f, 0.6f, 1.0f) : 1.0f;
    const auto planeColour = hex ("#fffaf2");
    g.setColour ((faded ? fadePhoto (planeColour) : planeColour).withAlpha (alpha * blink));
    g.fillEllipse (juce::Rectangle<float> (5.2f, 5.2f).withCentre ({ 1012.0f, 64.0f }));
}

void CloudSky::drawSkyGroup (juce::Graphics& g, int format, float alpha, float sun)
{
    auto& l = layers[static_cast<size_t> (format)];
    const auto& k = skyFor (format);
    const double t = motion ? now : 0.0;
    const bool rest = themeFade[static_cast<size_t> (format)].settled (now);

    drawCached (g, l.sky, alpha, {}, rest);
    drawCached (g, l.glow, alpha * (motion ? pulse (t, 7.0f, 0.0f, 0.85f, 1.0f) : 1.0f), {}, rest);

    if (k.stars)
        for (const auto& s : kStars)
        {
            const float level = motion ? pulse (t, 5.0f, -s[3], 0.15f, 0.75f) : 1.0f;
            g.setColour (hex ("#fff6ea").withAlpha (alpha * level));
            g.fillEllipse (s[0], s[1], s[2] * 2.0f, s[2] * 2.0f);
        }

    // VHS's ghosted sun: red and cyan copies just off the real one. The mockup screens
    // them over the sun, which leaves only coloured fringes, so they're drawn behind it.
    if (k.ghost)
    {
        g.setColour (rgba (255, 60, 90, 0.28f * alpha));
        g.fillEllipse (815.0f, sun - 2.0f, 100.0f, 100.0f);
        g.setColour (rgba (60, 220, 255, 0.22f * alpha));
        g.fillEllipse (826.0f, sun + 2.0f, 100.0f, 100.0f);
    }

    const float amp = amplitude();
    if (std::abs (l.trailAmp - amp) > 0.04f)
        rebuildTrails (format, amp);
    drawCached (g, l.softTrails, alpha * k.trail, {}, rest);
    drawCrispTrails (g, format, alpha * k.trail);
}

void CloudSky::drawCloudGroup (juce::Graphics& g, int format, float alpha, float sun)
{
    auto& l = layers[static_cast<size_t> (format)];
    const auto& k = skyFor (format);
    const double t = motion ? now : 0.0;
    const bool rest = themeFade[static_cast<size_t> (format)].settled (now);

    // The clouds drift less than a unit a second, so at rest they move in whole pixels.
    drawCached (g, l.farClouds, alpha, { 40.0f * (motion ? alternate (t, 52.0f) : 0.0f), 0.0f }, rest);
    drawCached (g, l.sea, alpha, {}, rest);
    drawCached (g, l.nearClouds, alpha, { -60.0f * (motion ? alternate (t, 40.0f) : 0.0f), 0.0f }, rest);

    if (k.leak)
        drawCached (g, l.leak, alpha, {}, rest);

    if (k.scan)
    {
        // Scanlines: a dark line every 3 units, then the tracking band rolling down.
        g.setColour (juce::Colours::black.withAlpha (0.18f * alpha));
        for (float y = height - 1.0f; y >= 0.0f; y -= 3.0f)
            g.fillRect (0.0f, y, width, 1.0f);

        const float bandY = motion ? -30.0f + 670.0f * cycle (t, 7.0f, 0.0f) : 0.0f;
        verticalGradient (g, { 0.0f, bandY, width, 16.0f }, { { 0.0f, juce::Colours::white.withAlpha (0.0f) },
                                                             { 0.5f, rgba (255, 240, 250, 0.12f * alpha) },
                                                             { 1.0f, juce::Colours::white.withAlpha (0.0f) } });
    }

    if (k.veil)
    {
        drawCached (g, l.veil, alpha, { 0.0f, sun }, rest && sunTop.settled (now));

        // Drizzle: faint lines every 11 units, leaning 15 degrees.
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (0, 0, static_cast<int> (width), static_cast<int> (height));
        g.addTransform (juce::AffineTransform::rotation (juce::degreesToRadians (15.0f)));
        g.setColour (rgba (220, 226, 238, 0.07f * alpha));
        for (float x = -220.0f; x < 1200.0f; x += 11.0f)
            g.fillRect (x, -400.0f, 1.0f, 1400.0f);
    }
}

void CloudSky::drawSun (juce::Graphics& g, float sun, float opacity)
{
    const bool rest = sunTop.settled (now);
    drawCached (g, sunFlare, opacity, { kSunLeft, sun }, rest);
    drawCached (g, sunGlow, opacity, { kSunLeft, sun }, rest);
    drawCached (g, sunDisc, opacity, { kSunLeft, sun }, rest);
}

void CloudSky::draw (juce::Graphics& g)
{
    const float pixelScale = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (std::abs (pixelScale - cachedScale) > 0.01f)
        rebuildCaches (pixelScale);

    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    g.fillAll (Colours::night);

    const float sun = sunTop.at (now);
    std::array<float, numFormats> alphas {};
    for (int i = 0; i < numFormats; ++i)
    {
        alphas[static_cast<size_t> (i)] = themeFade[static_cast<size_t> (i)].at (now);
        if (alphas[static_cast<size_t> (i)] > 0.002f && ! layers[static_cast<size_t> (i)].built)
            buildFormat (i);
        else if (alphas[static_cast<size_t> (i)] <= 0.002f && themeFade[static_cast<size_t> (i)].settled (now))
            dropRestCopies (i); // only the format on show keeps its big screen copies
    }

    for (int i = 0; i < numFormats; ++i)
        if (const float a = alphas[static_cast<size_t> (i)]; a > 0.002f)
            drawSkyGroup (g, i, a, sun);

    drawSun (g, sun, sunOpacity.at (now));

    for (int i = 0; i < numFormats; ++i)
        if (const float a = alphas[static_cast<size_t> (i)]; a > 0.002f)
            drawCloudGroup (g, i, a, sun);

    drawCached (g, bottomShade, 1.0f, {}, true);

    // Grain in overlay mode hardly touches the bright sun, so leave it out there
    // (a stack of strips covering the disc keeps the clip cheap).
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::RectangleList<int> clip (juce::Rectangle<int> (0, 0, static_cast<int> (width), static_cast<int> (height)));
        const auto disc = sunBounds();
        constexpr int strips = 12;
        for (int i = 0; i < strips; ++i)
        {
            const float y0 = disc.getY() + disc.getHeight() * static_cast<float> (i) / strips;
            const float yMid = (static_cast<float> (i) + 0.5f) / strips * 2.0f - 1.0f;
            const float half = disc.getWidth() * 0.5f * std::sqrt (std::max (0.0f, 1.0f - yMid * yMid)) - 3.0f;
            if (half > 0.0f)
                clip.subtract (juce::Rectangle<float> (disc.getCentreX() - half, y0, half * 2.0f, disc.getHeight() / strips)
                                   .getSmallestIntegerContainer());
        }
        g.reduceClipRegion (clip);
        drawCached (g, grainImage, grain.at (now) * 0.4f, {}, true);
    }
}

} // namespace contrail::ui
