#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Glass.h"
#include "../Params.h"

namespace xg::ui
{
// Rotary glass knob bound straight to a host parameter.
//  - drag up/right to raise; speed-sensitive (fast drags travel further)
//  - Shift = fine, double-click / Alt-click = reset, mouse wheel supported
//  - shows the value while hovered or dragged
class Knob : public juce::Component, public juce::SettableTooltipClient
{
public:
    Knob (juce::RangedAudioParameter& p, const juce::String& label)
        : param (p), text (label), attach (p, [this] (float) { repaint(); }, nullptr)
    {
        def = findDef (p.paramID);
        bipolar = def && def->min < 0.0f && def->max > 0.0f;
        attach.sendInitialUpdate();
        setTooltip (p.getName (64));
        setRepaintsOnMouseActivity (true);
    }
    float norm() const { return param.getValue(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float labelH = 18.0f;
        auto knobArea = b.withTrimmedBottom (labelH);
        const float d = juce::jmin (knobArea.getWidth(), knobArea.getHeight()) - 8.0f;
        auto c = knobArea.getCentre();
        const float rad = d * 0.5f;
        const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
        const float v = norm();
        const float av = a0 + v * (a1 - a0);
        const bool active = dragging || isMouseOverOrDragging();

        juce::Path track;
        track.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, a1, true);
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float from = bipolar ? a0 + (a1 - a0) * param.convertTo0to1 (0.0f) : a0;
        juce::Path val;
        val.addCentredArc (c.x, c.y, rad, rad, 0.0f, juce::jmin (from, av), juce::jmax (from, av), true);
        g.setColour (col::cyan.withAlpha (active ? 0.35f : 0.22f));                 // glow
        g.strokePath (val, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setGradientFill (juce::ColourGradient (col::cyanDeep, c.x - rad, c.y + rad, col::ice, c.x + rad, c.y - rad, false));
        g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // glass bead
        const float br = rad * 0.70f;
        auto bead = juce::Rectangle<float> (br * 2, br * 2).withCentre (c);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (active ? 0.30f : 0.22f), bead.getX(), bead.getY(),
                                                 juce::Colours::white.withAlpha (0.03f), bead.getRight(), bead.getBottom(), false));
        g.fillEllipse (bead);
        g.setColour (juce::Colour (0xff06202e).withAlpha (0.25f));
        g.fillEllipse (bead.reduced (br * 0.18f).translated (br * 0.08f, br * 0.12f));
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.75f), bead.getX(), bead.getY(),
                                                 juce::Colours::white.withAlpha (0.10f), bead.getRight(), bead.getBottom(), false));
        g.drawEllipse (bead, 1.1f);
        g.setColour (juce::Colours::white.withAlpha (0.38f));                        // specular glint
        g.fillEllipse (bead.getX() + br * 0.38f, bead.getY() + br * 0.20f, br * 0.62f, br * 0.30f);
        const juce::Point<float> p1 (c.x + std::sin (av) * br * 0.28f, c.y - std::cos (av) * br * 0.28f);
        const juce::Point<float> p2 (c.x + std::sin (av) * br * 0.86f, c.y - std::cos (av) * br * 0.86f);
        g.setColour (col::fg);
        g.drawLine ({ p1, p2 }, 2.2f);

        auto lr = b.removeFromBottom (labelH);
        if (active)
        {
            const auto s = param.getCurrentValueAsText();
            const float tw = juce::jmin (b.getWidth() + 30.0f, (float) juce::GlyphArrangement::getStringWidth (font (11.5f, true), s) + 14.0f);
            auto pill = juce::Rectangle<float> (tw, labelH - 2.0f).withCentre (lr.getCentre());
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, 0.8f);
            g.setColour (col::fg);
            g.setFont (font (11.5f, true));
            g.drawText (s, pill, juce::Justification::centred, false);
        }
        else
        {
            g.setColour (col::dim);
            g.setFont (font (11.5f, true));
            g.drawText (text.toUpperCase(), lr, juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isAltDown() || e.mods.isCommandDown()) { resetToDefault(); return; }
        dragging = true;
        accum = norm();
        lastPos = e.position;
        lastTime = juce::Time::getMillisecondCounterHiRes();
        attach.beginGesture();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto delta = e.position - lastPos;
        const float px = delta.x - delta.y;                                  // up or right = more
        const float dt = (float) juce::jmax (1.0, now - lastTime);
        const float speed = std::abs (px) / dt;                              // px per ms
        const bool fine = e.mods.isShiftDown();
        float sens = fine ? 1.0f / 900.0f : 1.0f / 220.0f;
        if (! fine) sens *= juce::jlimit (0.6f, 2.6f, 0.7f + speed * 0.9f); // velocity response
        accum = juce::jlimit (0.0f, 1.0f, accum + px * sens);
        setNorm (accum, true);
        lastPos = e.position;
        lastTime = now;
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging) attach.endGesture();
        dragging = false;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override { resetToDefault(); }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const float dir = (w.deltaY != 0.0f ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
        const float step = e.mods.isShiftDown() ? 0.004f : 0.025f;
        const float nv = juce::jlimit (0.0f, 1.0f, norm() + (dir > 0 ? step : -step) * juce::jmax (1.0f, std::abs (dir) * 4.0f));
        attach.setValueAsCompleteGesture (param.convertFrom0to1 (nv));
    }

private:
    juce::RangedAudioParameter& param;
    juce::String text;
    bool bipolar = false, dragging = false;
    const ParamDef* def = nullptr;
    juce::ParameterAttachment attach;
    float accum = 0.0f;
    juce::Point<float> lastPos;
    double lastTime = 0.0;

    void setNorm (float n, bool inGesture)
    {
        const float v = param.convertFrom0to1 (n);
        if (inGesture) attach.setValueAsPartOfGesture (v);
        else attach.setValueAsCompleteGesture (v);
    }
    void resetToDefault() { attach.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue())); }
};


// Two-way glass switch for a choice param (Soft | Hard).
class Segmented : public juce::Component, public juce::SettableTooltipClient
{
public:
    Segmented (juce::RangedAudioParameter& p, const juce::String& label)
        : param (p), text (label), attach (p, [this] (float) { repaint(); }, nullptr)
    {
        attach.sendInitialUpdate();
        setTooltip (p.getName (64));
        setRepaintsOnMouseActivity (true);
    }
    int index() const { return juce::roundToInt (param.getValue() * (float) (juce::jmax (2, param.getNumSteps()) - 1)); }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        auto lr = b.removeFromBottom (18.0f);
        const int n = juce::jmax (2, param.getNumSteps());
        auto r = b.withSizeKeepingCentre (b.getWidth() - 4.0f, juce::jmin (b.getHeight(), 30.0f) * (float) n * 0.9f).reduced (0, 2);
        const float rh = r.getHeight() / (float) n;
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (r, 9.0f);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawRoundedRectangle (r, 9.0f, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            auto cell = juce::Rectangle<float> (r.getX(), r.getY() + rh * (float) i, r.getWidth(), rh).reduced (3.0f);
            const bool on = i == index();
            if (on)
            {
                g.setColour (col::cyan.withAlpha (0.30f)); g.fillRoundedRectangle (cell, 7.0f);
                g.setColour (col::ice.withAlpha (0.8f)); g.drawRoundedRectangle (cell, 7.0f, 1.0f);
            }
            g.setColour (on ? col::fg : col::dim);
            g.setFont (font (12.5f, true));
            g.drawText (param.getText ((float) i / (float) (n - 1), 16).toUpperCase(), cell, juce::Justification::centred);
        }
        g.setColour (col::dim);
        g.setFont (font (11.5f, true));
        g.drawText (text.toUpperCase(), lr, juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int n = juce::jmax (2, param.getNumSteps());
        auto b = getLocalBounds().toFloat().withTrimmedBottom (18.0f);
        const int i = juce::jlimit (0, n - 1, (int) ((e.position.y - b.getY()) / b.getHeight() * (float) n));
        attach.setValueAsCompleteGesture (param.convertFrom0to1 ((float) i / (float) (n - 1)));
    }
private:
    juce::RangedAudioParameter& param;
    juce::String text;
    juce::ParameterAttachment attach;
};

// Glass pill toggle for a bool param (Delta).
class PillToggle : public juce::Component, public juce::SettableTooltipClient
{
public:
    PillToggle (juce::RangedAudioParameter& p, const juce::String& label, const juce::String& caption)
        : param (p), text (label), cap (caption), attach (p, [this] (float) { repaint(); }, nullptr)
    {
        attach.sendInitialUpdate();
        setTooltip (p.getName (64) + ": hear only what is being removed");
        setRepaintsOnMouseActivity (true);
    }
    bool isOn() const { return param.getValue() > 0.5f; }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        auto lr = b.removeFromBottom (18.0f);
        auto r = b.withSizeKeepingCentre (b.getWidth() - 6.0f, 34.0f);
        const float rad = r.getHeight() * 0.5f;
        g.setColour (isOn() ? col::cyan.withAlpha (0.38f) : juce::Colours::white.withAlpha (isMouseOver() ? 0.14f : 0.08f));
        g.fillRoundedRectangle (r, rad);
        g.setColour (isOn() ? col::ice : juce::Colours::white.withAlpha (0.4f));
        g.drawRoundedRectangle (r, rad, 1.0f);
        g.setColour (isOn() ? col::fg : col::dim);
        g.setFont (font (13.0f, true));
        g.drawText (text.toUpperCase(), r, juce::Justification::centred);
        g.setColour (col::dim);
        g.setFont (font (11.5f, true));
        g.drawText (cap.toUpperCase(), lr, juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent&) override { attach.setValueAsCompleteGesture (isOn() ? 0.0f : 1.0f); }
private:
    juce::RangedAudioParameter& param;
    juce::String text, cap;
    juce::ParameterAttachment attach;
};

// Stepper for a choice param:  < value >  ; click the middle for a menu.
class Selector : public juce::Component, public juce::SettableTooltipClient
{
public:
    Selector (juce::RangedAudioParameter& p) : param (p), attach (p, [this] (float) { repaint(); if (onChange) onChange(); }, nullptr)
    {
        attach.sendInitialUpdate();
        setTooltip (p.getName (64));
        setRepaintsOnMouseActivity (true);
    }
    std::function<void()> onChange;
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        const float rad = b.getHeight() * 0.5f;
        g.setColour (juce::Colours::white.withAlpha (isMouseOver() ? 0.14f : 0.08f));
        g.fillRoundedRectangle (b, rad);
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.drawRoundedRectangle (b, rad, 1.0f);
        g.setColour (col::fg);
        g.setFont (font (13.0f, true));
        g.drawText (param.getCurrentValueAsText(), b.reduced (16, 0), juce::Justification::centred, true);
        g.setColour (col::cyan);
        auto arrow = [&g] (juce::Rectangle<float> r, bool left)
        {
            juce::Path t; const auto cc = r.getCentre();
            if (left) t.addTriangle (cc.x + 2, cc.y - 4, cc.x + 2, cc.y + 4, cc.x - 3, cc.y);
            else      t.addTriangle (cc.x - 2, cc.y - 4, cc.x - 2, cc.y + 4, cc.x + 3, cc.y);
            g.fillPath (t);
        };
        arrow (b.withWidth (18).translated (4, 0), true);
        arrow (b.withTrimmedLeft (b.getWidth() - 18).translated (-4, 0), false);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.x < 22) step (-1);
        else if (e.x > getWidth() - 22) step (1);
        else
        {
            juce::PopupMenu m;
            const int n = numSteps();
            for (int i = 0; i < n; ++i) m.addItem (i + 1, param.getText ((float) i / (float) (n - 1), 32), true, i == current());
            juce::Component::SafePointer<Selector> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                             [safe] (int r) { if (safe && r > 0) safe->setIndex (r - 1); });
        }
    }
private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attach;
    int numSteps() const { return juce::jmax (2, param.getNumSteps()); }
    int current() const { return juce::roundToInt (param.getValue() * (float) (numSteps() - 1)); }
    void setIndex (int i) { i = juce::jlimit (0, numSteps() - 1, i); attach.setValueAsCompleteGesture (param.convertFrom0to1 ((float) i / (float) (numSteps() - 1))); }
    void step (int d) { setIndex ((current() + d + numSteps()) % numSteps()); }
};
} // namespace xg::ui
