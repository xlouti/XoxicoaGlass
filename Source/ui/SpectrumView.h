#pragma once
#include "Glass.h"
#include "../PluginProcessor.h"

namespace xg::ui
{
// The glass spectrum: input (frosted fill), output (cyan line), gain reduction hanging from the top like
// icicles, the focus range with two draggable handles, and the tonal-balance strip underneath.
// Grid and labels are drawn once into the editor's static layer (paintStatic); paint() only draws live data.
class SpectrumView : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumView (XoxicoaGlassProcessor& p) : proc (p)
    {
        loP = proc.apvts.getParameter ("focus_lo");
        hiP = proc.apvts.getParameter ("focus_hi");
        startTimerHz (30);
        setTooltip();
    }
    void setTooltip() {}

    static constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f, kTopDb = 0.0f, kBotDb = -90.0f;

    juce::Rectangle<float> plot() const { return { 44.0f, 26.0f, (float) getWidth() - 56.0f, (float) getHeight() - 122.0f }; }
    juce::Rectangle<float> strip() const { auto p = plot(); return { p.getX(), p.getBottom() + 34.0f, p.getWidth(), (float) getHeight() - p.getBottom() - 42.0f }; }
    float xFor (float hz) const { auto p = plot(); return p.getX() + p.getWidth() * std::log (juce::jlimit (kMinHz, kMaxHz, hz) / kMinHz) / std::log (kMaxHz / kMinHz); }
    float hzFor (float x) const { auto p = plot(); return kMinHz * std::pow (kMaxHz / kMinHz, juce::jlimit (0.0f, 1.0f, (x - p.getX()) / p.getWidth())); }
    float yFor (float db) const { auto p = plot(); return p.getY() + p.getHeight() * (kTopDb - juce::jlimit (kBotDb, kTopDb, db)) / (kTopDb - kBotDb); }

    // Static parts (into the editor's cached layer). origin = this component's top-left in the editor.
    void paintStatic (juce::Graphics& g, juce::Point<int> origin) const
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::translation ((float) origin.x, (float) origin.y));
        auto p = plot();
        g.setColour (juce::Colour (0xff04121f).withAlpha (0.22f));
        g.fillRoundedRectangle (p.expanded (2.0f), 6.0f);
        g.setFont (font (10.5f));
        for (float db = -12.0f; db > kBotDb; db -= 12.0f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.drawHorizontalLine ((int) yFor (db), p.getX(), p.getRight());
            g.setColour (col::faint);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (0, yFor (db) - 7, p.getX() - 6, 14), juce::Justification::centredRight);
        }
        for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            g.setColour (juce::Colours::white.withAlpha (hz == 100.0f || hz == 1000.0f || hz == 10000.0f ? 0.12f : 0.06f));
            g.drawVerticalLine ((int) xFor (hz), p.getY(), p.getBottom());
            g.setColour (col::faint);
            const auto t = hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz);
            g.drawText (t, juce::Rectangle<float> (xFor (hz) - 20, p.getBottom() + 2, 40, 13), juce::Justification::centred);
        }
        auto s = strip();
        g.setColour (juce::Colour (0xff04121f).withAlpha (0.18f));
        g.fillRoundedRectangle (s.expanded (2.0f), 6.0f);
        const float mid = s.getCentreY(), per = s.getHeight() * 0.5f / 8.0f;   // +-8 dB fills the strip
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect (s.getX(), mid - per * xg::kTargetTolerance, s.getWidth(), per * xg::kTargetTolerance * 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawHorizontalLine ((int) mid, s.getX(), s.getRight());
        g.setColour (col::faint);
        g.setFont (font (9.5f));
        g.drawText ("+8", juce::Rectangle<float> (0, s.getY() - 2, s.getX() - 6, 12), juce::Justification::centredRight);
        g.drawText ("0", juce::Rectangle<float> (0, mid - 6, s.getX() - 6, 12), juce::Justification::centredRight);
        g.drawText ("-8", juce::Rectangle<float> (0, s.getBottom() - 10, s.getX() - 6, 12), juce::Justification::centredRight);
    }

    void paint (juce::Graphics& g) override
    {
        auto p = plot();
        const float lo = loP->convertFrom0to1 (loP->getValue()), hi = hiP->convertFrom0to1 (hiP->getValue());
        const float xl = xFor (std::min (lo, hi)), xh = xFor (std::max (lo, hi));
        // outside the focus: darker glass
        g.setColour (juce::Colour (0xff031220).withAlpha (0.30f));
        g.fillRect (juce::Rectangle<float> (p.getX(), p.getY(), xl - p.getX(), p.getHeight()));
        g.fillRect (juce::Rectangle<float> (xh, p.getY(), p.getRight() - xh, p.getHeight()));
        g.setColour (col::cyan.withAlpha (0.04f));
        g.fillRect (juce::Rectangle<float> (xl, p.getY(), xh - xl, p.getHeight()));

        if (! inPath.isEmpty())
        {
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.20f), 0, p.getY(), juce::Colours::white.withAlpha (0.03f), 0, p.getBottom(), false));
            g.fillPath (inFill);
            g.setColour (juce::Colours::white.withAlpha (0.38f));
            g.strokePath (inPath, juce::PathStrokeType (1.0f));
            g.setColour (col::cyan.withAlpha (0.25f));
            g.strokePath (outPath, juce::PathStrokeType (4.0f));
            g.setColour (col::cyan);
            g.strokePath (outPath, juce::PathStrokeType (1.6f));
            g.setGradientFill (juce::ColourGradient (col::ice.withAlpha (0.85f), 0, p.getY(), col::cyan.withAlpha (0.35f), 0, p.getY() + 72.0f, false));
            g.fillPath (grPath);
        }
        // handles
        for (int i = 0; i < 2; ++i)
        {
            const float x = i == 0 ? xl : xh;
            const bool hot = dragHandle == i || hoverHandle == i;
            g.setColour (col::cyan.withAlpha (hot ? 0.9f : 0.6f));
            g.drawLine (x, p.getY(), x, p.getBottom(), hot ? 2.0f : 1.4f);
            auto grip = juce::Rectangle<float> (40.0f, 18.0f).withCentre ({ x, p.getBottom() - 12.0f });
            grip.setX (juce::jlimit (p.getX(), p.getRight() - grip.getWidth(), grip.getX()));
            g.setColour (juce::Colours::white.withAlpha (hot ? 0.32f : 0.2f));
            g.fillRoundedRectangle (grip, 9.0f);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawRoundedRectangle (grip, 9.0f, 1.0f);
            g.setColour (col::fg);
            g.setFont (font (10.5f, true));
            g.drawText (i == 0 ? "LOW" : "HIGH", grip, juce::Justification::centred);
            const float hz = i == 0 ? std::min (lo, hi) : std::max (lo, hi);
            const auto t = hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : juce::String ((int) hz) + " Hz";
            auto lab = juce::Rectangle<float> (70.0f, 14.0f).withCentre ({ x, p.getY() + 9.0f });
            lab.setX (juce::jlimit (p.getX(), p.getRight() - lab.getWidth(), lab.getX()));
            g.setColour (col::ice);
            g.drawText (t, lab, juce::Justification::centred);
        }
        // readouts
        g.setFont (font (11.5f, true));
        g.setColour (col::dim);
        auto ro = juce::Rectangle<float> (p.getRight() - 300.0f, 4.0f, 300.0f, 16.0f);
        const float latMs = (float) proc.getLatencySamples() / (float) juce::jmax (1.0, proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0) * 1000.0f;
        g.drawText ("REDUCTION " + juce::String (shownGr, 1) + " dB    LATENCY " + juce::String (latMs, 1) + " ms", ro, juce::Justification::centredRight);
        g.setColour (col::fg);
        g.setFont (font (12.0f, true));
        g.drawText ("GLASS SPECTRUM", juce::Rectangle<float> (12.0f, 4.0f, 200.0f, 16.0f), juce::Justification::centredLeft);

        // tonal balance strip
        auto s = strip();
        const int target = (int) proc.apvts.getRawParameterValue ("target")->load();
        g.setFont (font (11.0f, true));
        g.setColour (col::fg);
        auto title = juce::Rectangle<float> (12.0f, s.getY() - 17.0f, p.getRight() - 12.0f, 14.0f);
        if (target == 0) { g.setColour (col::dim); g.drawText ("TONAL BALANCE   (target off)", title, juce::Justification::centredLeft); return; }
        if (! tonalValid)
        {
            g.setColour (col::dim);
            g.drawText ("TONAL BALANCE vs " + xg::targetNames[target].toUpperCase() + " (approx.)   play audio to measure", title, juce::Justification::centredLeft);
            return;
        }
        const auto dev = xg::TonalBalance::deviation (tonalLv, target);
        const float mid = s.getCentreY(), per = s.getHeight() * 0.5f / 8.0f;
        for (int b = 0; b < xg::kBands; ++b)
        {
            const float x = xFor (xg::kBandHz[(size_t) b]);
            const float d = juce::jlimit (-8.0f, 8.0f, dev[(size_t) b]);
            const bool ok = std::abs (d) <= xg::kTargetTolerance;
            auto bar = juce::Rectangle<float> (x - 5.0f, d > 0 ? mid - d * per : mid, 10.0f, std::abs (d) * per + 1.0f);
            g.setColour ((ok ? col::cyan : col::warn).withAlpha (0.75f));
            g.fillRoundedRectangle (bar, 2.0f);
        }
        juce::String sum = "TONAL BALANCE vs " + xg::targetNames[target].toUpperCase() + " (approx.)   ";
        auto part = [&dev] (const char* n, float a, float b) { const float v = xg::TonalBalance::rangeDev (dev, a, b); return juce::String (n) + " " + (v >= 0 ? "+" : "") + juce::String (v, 1) + "   "; };
        sum << part ("LOW", 31.5f, 125.0f) << part ("LOW-MID", 160.0f, 500.0f) << part ("MID", 630.0f, 2500.0f) << part ("HIGH", 3150.0f, 8000.0f) << part ("AIR", 10000.0f, 16000.0f);
        g.drawText (sum, title, juce::Justification::centredLeft);
    }

    void mouseMove (const juce::MouseEvent& e) override { const int h = handleAt (e.position.x); if (h != hoverHandle) { hoverHandle = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hoverHandle = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragHandle = handleAt (e.position.x);
        if (dragHandle >= 0) param (dragHandle)->beginChangeGesture();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragHandle < 0) return;
        auto* prm = param (dragHandle);
        prm->setValueNotifyingHost (prm->convertTo0to1 (juce::jlimit (prm->getNormalisableRange().start, prm->getNormalisableRange().end, hzFor (e.position.x))));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { if (dragHandle >= 0) param (dragHandle)->endChangeGesture(); dragHandle = -1; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int h = handleAt (e.position.x);
        if (h >= 0) { auto* prm = param (h); prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->getDefaultValue()); prm->endChangeGesture(); }
    }

    void refresh() { timerCallback(); }

private:
    juce::RangedAudioParameter* param (int i) const
    {
        // the handle on the left is whichever of the two values is lower
        const float lo = loP->convertFrom0to1 (loP->getValue()), hi = hiP->convertFrom0to1 (hiP->getValue());
        const bool swapped = lo > hi;
        return (i == 0) != swapped ? loP : hiP;
    }
    int handleAt (float x) const
    {
        const float lo = loP->convertFrom0to1 (loP->getValue()), hi = hiP->convertFrom0to1 (hiP->getValue());
        const float dl = std::abs (x - xFor (std::min (lo, hi))), dh = std::abs (x - xFor (std::max (lo, hi)));
        if (std::min (dl, dh) > 10.0f) return -1;
        return dl <= dh ? 0 : 1;
    }

    void timerCallback() override
    {
        proc.deharsh.copyDisplay (din, dout, dgr);
        const float binHz = proc.deharsh.binHz();
        auto p = plot();
        inPath.clear(); outPath.clear(); grPath.clear(); inFill.clear();
        if (din.empty() || getWidth() <= 0) { repaint(); return; }
        const int nb = (int) din.size();
        constexpr float step = 3.0f;
        grPath.startNewSubPath (p.getX(), p.getY());
        bool first = true;
        float maxGr = 0.0f;
        for (float x = p.getX(); x <= p.getRight() + 0.1f; x += step)
        {
            const float f0 = hzFor (x - step * 0.5f), f1 = hzFor (x + step * 0.5f);
            int k0 = juce::jlimit (1, nb - 1, (int) std::floor (f0 / binHz)), k1 = juce::jlimit (1, nb - 1, (int) std::ceil (f1 / binHz));
            float a = -200.0f, b = -200.0f, gr = 0.0f;
            if (f1 - f0 < binHz * 2.0f)
            {   // fewer than ~2 bins per column (the low end): interpolate between bins instead of stair-stepping
                const float fk = juce::jlimit (1.0f, (float) (nb - 2), (f0 + f1) * 0.5f / binHz);
                const int k = (int) fk; const float t = fk - (float) k;
                a = din[(size_t) k] + (din[(size_t) k + 1] - din[(size_t) k]) * t;
                b = dout[(size_t) k] + (dout[(size_t) k + 1] - dout[(size_t) k]) * t;
                gr = dgr[(size_t) k] + (dgr[(size_t) k + 1] - dgr[(size_t) k]) * t;
            }
            else
                for (int k = k0; k <= k1; ++k) { a = std::max (a, din[(size_t) k]); b = std::max (b, dout[(size_t) k]); gr = std::max (gr, dgr[(size_t) k]); }
            const float ya = yFor (a), yb = yFor (b), yg = p.getY() + juce::jmin (p.getHeight() * 0.5f, gr * 4.0f);
            maxGr = std::max (maxGr, gr);
            if (first) { inPath.startNewSubPath (x, ya); outPath.startNewSubPath (x, yb); first = false; }
            else { inPath.lineTo (x, ya); outPath.lineTo (x, yb); }
            grPath.lineTo (x, yg);
        }
        grPath.lineTo (p.getRight(), p.getY());
        grPath.closeSubPath();
        inFill = inPath;
        inFill.lineTo (p.getRight(), p.getBottom());
        inFill.lineTo (p.getX(), p.getBottom());
        inFill.closeSubPath();
        shownGr += (maxGr - shownGr) * 0.3f;
        tonalLv = proc.tonal.levels (0);
        tonalValid = tonalLv[15] > -110.0f;
        repaint();
    }

    XoxicoaGlassProcessor& proc;
    juce::RangedAudioParameter* loP = nullptr;
    juce::RangedAudioParameter* hiP = nullptr;
    std::vector<float> din, dout, dgr;
    juce::Path inPath, inFill, outPath, grPath;
    std::array<float, xg::kBands> tonalLv {};
    bool tonalValid = false;
    float shownGr = 0.0f;
    int dragHandle = -1, hoverHandle = -1;
};
} // namespace xg::ui
