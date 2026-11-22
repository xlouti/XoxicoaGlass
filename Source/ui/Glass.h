#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Xoxicoa Glass look: frosted, clear "ice" panels over a deep teal-to-navy backdrop with cyan light.
// Everything expensive (gradients, blur, panel frosting, edges) is drawn ONCE into images when the
// editor is created / resized. Per frame only the spectrum lines are drawn.
namespace xg::ui
{
namespace col
{
    const juce::Colour bgTop    { 0xff0e4a5c };   // deep teal
    const juce::Colour bgMid    { 0xff0c2f4f };
    const juce::Colour bgBottom { 0xff0a1834 };   // navy
    const juce::Colour cyan     { 0xff6ff3ff };
    const juce::Colour cyanDeep { 0xff2cc8f0 };
    const juce::Colour ice      { 0xffd9fbff };
    const juce::Colour fg       { 0xfff2fdff };
    const juce::Colour dim      { 0xff9cc9d6 };
    const juce::Colour faint    { 0xff6f9db0 };
    const juce::Colour warn     { 0xffffc7d6 };   // over-target bars (soft frost pink)
}

inline juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

// Separable box blur, 3 passes (looks Gaussian). Runs on an ARGB image in place. Used only at build time.
inline void boxBlur (juce::Image& img, int radius)
{
    if (radius < 1) return;
    const int w = img.getWidth(), h = img.getHeight();
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);
    std::vector<int> acc (4), line ((size_t) juce::jmax (w, h) * 4);
    auto pass = [&] (bool horizontal)
    {
        const int len = horizontal ? w : h, count = horizontal ? h : w;
        for (int L = 0; L < count; ++L)
        {
            auto px = [&] (int i) -> juce::uint8* { return horizontal ? bd.getPixelPointer (i, L) : bd.getPixelPointer (L, i); };
            for (int i = 0; i < len; ++i) { auto* p = px (i); for (int c = 0; c < 4; ++c) line[(size_t) (i * 4 + c)] = p[c]; }
            std::fill (acc.begin(), acc.end(), 0);
            for (int i = -radius; i <= radius; ++i) { const int j = juce::jlimit (0, len - 1, i); for (int c = 0; c < 4; ++c) acc[(size_t) c] += line[(size_t) (j * 4 + c)]; }
            const int div = 2 * radius + 1;
            for (int i = 0; i < len; ++i)
            {
                auto* p = px (i);
                for (int c = 0; c < 4; ++c) p[c] = (juce::uint8) (acc[(size_t) c] / div);
                const int out = juce::jlimit (0, len - 1, i - radius), in = juce::jlimit (0, len - 1, i + radius + 1);
                for (int c = 0; c < 4; ++c) acc[(size_t) c] += line[(size_t) (in * 4 + c)] - line[(size_t) (out * 4 + c)];
            }
        }
    };
    for (int k = 0; k < 3; ++k) { pass (true); pass (false); }
}

// Blur at a quarter size, then scale back up: big soft blur for very little work.
inline juce::Image blurred (const juce::Image& src, int radiusFull)
{
    const int sw = juce::jmax (1, src.getWidth() / 4), sh = juce::jmax (1, src.getHeight() / 4);
    juce::Image small (juce::Image::ARGB, sw, sh, true);
    {
        juce::Graphics g (small);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (src, 0, 0, sw, sh, 0, 0, src.getWidth(), src.getHeight());
    }
    boxBlur (small, juce::jmax (1, radiusFull / 4));
    juce::Image out (juce::Image::ARGB, src.getWidth(), src.getHeight(), true);
    juce::Graphics g (out);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (small, 0, 0, src.getWidth(), src.getHeight(), 0, 0, sw, sh);
    return out;
}

// The scene behind the glass: gradient + soft light blobs + a couple of shapes for the glass to refract.
inline juce::Image makeBackdrop (int w, int h)
{
    juce::Image img (juce::Image::ARGB, w, h, true);
    juce::Graphics g (img);
    juce::ColourGradient grad (col::bgTop, 0, 0, col::bgBottom, (float) w, (float) h, false);
    grad.addColour (0.5, col::bgMid);
    g.setGradientFill (grad);
    g.fillAll();
    auto blob = [&g] (float x, float y, float r, juce::Colour c)
    {
        juce::ColourGradient rg (c, x, y, c.withAlpha (0.0f), x + r, y, true);
        g.setGradientFill (rg);
        g.fillEllipse (x - r, y - r, r * 2, r * 2);
    };
    blob (110, 30, 260, juce::Colour (0xff46e6ff).withAlpha (0.50f));
    blob (860, 70, 230, juce::Colour (0xff3d7bff).withAlpha (0.38f));
    blob (760, 540, 300, juce::Colour (0xff2bc4b8).withAlpha (0.36f));
    blob (420, 330, 180, juce::Colour (0xffbff8ff).withAlpha (0.14f));
    blob (60, 520, 200, juce::Colour (0xff1f6fd0).withAlpha (0.30f));
    // shapes behind the glass (they read as soft refractions through the frost)
    g.setColour (juce::Colour (0xff8ff6ff).withAlpha (0.20f));
    g.drawEllipse (520.0f, 90.0f, 330.0f, 330.0f, 16.0f);
    g.setColour (juce::Colour (0xffffffff).withAlpha (0.08f));
    juce::Path streak;
    streak.startNewSubPath (-40, (float) h * 0.78f);
    streak.lineTo ((float) w * 0.55f, -20);
    streak.lineTo ((float) w * 0.62f, -20);
    streak.lineTo (40, (float) h * 0.86f);
    streak.closeSubPath();
    g.fillPath (streak);
    return blurred (img, 12);
}

// One frosted glass panel, drawn into the static layer.
inline void drawGlassPanel (juce::Graphics& g, const juce::Image& frost, juce::Rectangle<float> r, float radius = 14.0f)
{
    // soft shadow under the pane
    for (int i = 0; i < 6; ++i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.045f));
        g.fillRoundedRectangle (r.translated (0, 3.0f + (float) i).expanded ((float) i * 0.8f), radius + (float) i);
    }
    juce::Path shape;
    shape.addRoundedRectangle (r, radius);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (shape);
        g.drawImageAt (frost, 0, 0);                                   // the blurred scene seen through the glass
        g.setColour (juce::Colours::white.withAlpha (0.07f));          // milky frost
        g.fillRect (r);
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.16f), r.getX(), r.getY(),
                                    juce::Colours::white.withAlpha (0.0f), r.getX(), r.getY() + juce::jmin (90.0f, r.getHeight() * 0.5f), false);
        g.setGradientFill (sheen);
        g.fillRect (r);
        // refraction streak across the pane
        juce::Path band;
        const float x0 = r.getX() + r.getWidth() * 0.58f;
        band.startNewSubPath (x0, r.getY());
        band.lineTo (x0 + 70.0f, r.getY());
        band.lineTo (x0 + 70.0f - r.getHeight() * 0.6f, r.getBottom());
        band.lineTo (x0 - r.getHeight() * 0.6f, r.getBottom());
        band.closeSubPath();
        g.setColour (juce::Colours::white.withAlpha (0.035f));
        g.fillPath (band);
    }
    // bright top-left edge, dim bottom-right edge (light hitting the glass)
    juce::ColourGradient edge (juce::Colours::white.withAlpha (0.55f), r.getX(), r.getY(),
                               juce::Colours::white.withAlpha (0.08f), r.getRight(), r.getBottom(), false);
    g.setGradientFill (edge);
    g.strokePath (shape, juce::PathStrokeType (1.2f));
    juce::Path inner;
    inner.addRoundedRectangle (r.reduced (1.5f), radius - 1.5f);
    juce::ColourGradient innerEdge (col::cyan.withAlpha (0.22f), r.getX(), r.getY(), col::cyan.withAlpha (0.0f), r.getX(), r.getY() + 30.0f, false);
    g.setGradientFill (innerEdge);
    g.strokePath (inner, juce::PathStrokeType (1.0f));
}

class GlassLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GlassLookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xf0123a52));
        setColour (juce::PopupMenu::textColourId, col::fg);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, col::cyan.withAlpha (0.35f));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf0123a52));
        setColour (juce::TooltipWindow::textColourId, col::fg);
        setColour (juce::TooltipWindow::outlineColourId, col::cyan.withAlpha (0.4f));
    }
    juce::Font getPopupMenuFont() override { return font (14.0f); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const float rad = r.getHeight() * 0.5f;
        const bool primary = b.getProperties()["primary"];
        juce::ColourGradient fill (juce::Colours::white.withAlpha (primary ? 0.30f : 0.14f), r.getX(), r.getY(),
                                   juce::Colours::white.withAlpha (primary ? 0.08f : 0.03f), r.getX(), r.getBottom(), false);
        if (down) fill.multiplyOpacity (0.7f);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (r, rad);
        if (primary)
        {
            g.setColour (col::cyan.withAlpha (over ? 0.38f : 0.24f));
            g.fillRoundedRectangle (r, rad);
        }
        else if (over) { g.setColour (juce::Colours::white.withAlpha (0.08f)); g.fillRoundedRectangle (r, rad); }
        juce::ColourGradient edge (juce::Colours::white.withAlpha (0.7f), r.getX(), r.getY(), juce::Colours::white.withAlpha (0.15f), r.getX(), r.getBottom(), false);
        g.setGradientFill (edge);
        g.drawRoundedRectangle (r, rad, 1.0f);
    }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const bool primary = b.getProperties()["primary"];
        g.setColour (col::fg);
        g.setFont (font (primary ? 15.0f : 13.0f, true));
        g.drawText (b.getButtonText(), b.getLocalBounds().reduced (6, 0), juce::Justification::centred, true);
    }
};
} // namespace xg::ui
