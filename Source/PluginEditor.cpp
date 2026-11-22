#include "PluginEditor.h"

using namespace xg::ui;

XoxicoaGlassEditor::XoxicoaGlassEditor (XoxicoaGlassProcessor& p)
    : AudioProcessorEditor (&p), proc (p), spectrum (p),
      depth (prm ("depth"), "Depth"), sharp (prm ("sharp"), "Sharpness"), speed (prm ("speed"), "Speed"),
      lo (prm ("focus_lo"), "Focus Low"), hi (prm ("focus_hi"), "Focus High"), mix (prm ("mix"), "Mix"), out (prm ("output"), "Output"),
      mode (prm ("mode"), "Mode"), delta (prm ("delta"), "Delta", "Listen"), target (prm ("target"))
{
    setLookAndFeel (&lnf);
    setOpaque (true);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &spectrum, &depth, &sharp, &speed, &lo, &hi, &mix, &out, &mode, &delta, &target, &autoBtn, &prevBtn, &nextBtn })
        addAndMakeVisible (c);
    autoBtn.getProperties().set ("primary", true);
    autoBtn.setTooltip ("AUTO listens to the playing audio for 4 seconds, then sets Depth, Sharpness, Speed and the Focus range for it. Click again to cancel.");
    autoBtn.onClick = [this] { if (proc.autoState() == 1) proc.cancelAuto(); else proc.startAuto(); };
    prevBtn.onClick = [this] { proc.stepPreset (-1); };
    nextBtn.onClick = [this] { proc.stepPreset (1); };
    target.onChange = [this] { spectrum.repaint(); };
    depth.setTooltip ("How hard harsh, ringing frequencies are pulled down");
    sharp.setTooltip ("Low = broad, smooth cuts (whole harsh regions). High = narrow cuts on single ringing tones");
    speed.setTooltip ("How fast the smoothing reacts and lets go (attack / release)");
    lo.setTooltip ("Lowest frequency it is allowed to touch (also the LOW handle on the spectrum)");
    hi.setTooltip ("Highest frequency it is allowed to touch (also the HIGH handle on the spectrum)");
    mode.setTooltip ("Soft = gentle, musical. Hard = stronger and more exact on peaks");
    mix.setTooltip ("Blend of processed and original sound");
    out.setTooltip ("Output level");
    target.setTooltip ("Tonal balance target for the display (approximate genre curves). Does not change the sound");
    proc.onPresetChanged = [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<XoxicoaGlassEditor> (this)] { if (sp) sp->repaint(); }); };
    setSize (kW, kH);
    startTimerHz (15);
}

XoxicoaGlassEditor::~XoxicoaGlassEditor()
{
    proc.onPresetChanged = nullptr;
    setLookAndFeel (nullptr);
}

void XoxicoaGlassEditor::resized()
{
    spectrum.setBounds (14, 70, 872, 300);
    autoBtn.setBounds (300, 13, 150, 36);
    target.setBounds (470, 16, 150, 30);
    prevBtn.setBounds (640, 16, 30, 30);
    presetBox = { 674, 16, 178, 30 };
    nextBtn.setBounds (856, 16, 30, 30);

    panels.clear();
    panels.push_back ({ "SMOOTHING", { 14, 382, 404, 164 } });
    panels.push_back ({ "FOCUS", { 430, 382, 196, 164 } });
    panels.push_back ({ "OUTPUT", { 638, 382, 248, 164 } });
    const int ky = 382 + 30, kh = 124;
    depth.setBounds (26, ky, 88, kh);
    sharp.setBounds (114, ky, 88, kh);
    speed.setBounds (202, ky, 88, kh);
    mode.setBounds (300, ky + 8, 104, kh - 8);
    lo.setBounds (440, ky, 88, kh);
    hi.setBounds (530, ky, 88, kh);
    delta.setBounds (646, ky + 8, 78, kh - 8);
    mix.setBounds (726, ky, 78, kh);
    out.setBounds (806, ky, 78, kh);
    buildStaticLayer();
}

void XoxicoaGlassEditor::buildStaticLayer()
{
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) return;
    auto backdrop = makeBackdrop (w, h);
    auto frost = blurred (backdrop, 36);
    {
        juce::Graphics fg (frost);
        fg.setColour (juce::Colour (0xffcff6ff).withAlpha (0.06f));   // a touch of icy milk in the frost
        fg.fillAll();
    }
    staticLayer = juce::Image (juce::Image::RGB, w, h, false);
    juce::Graphics g (staticLayer);
    g.drawImageAt (backdrop, 0, 0);
    // header glass strip
    drawGlassPanel (g, frost, juce::Rectangle<float> (8.0f, 6.0f, (float) w - 16.0f, 52.0f), 16.0f);
    drawGlassPanel (g, frost, spectrum.getBounds().toFloat(), 16.0f);
    spectrum.paintStatic (g, spectrum.getPosition());
    for (auto& pn : panels)
    {
        drawGlassPanel (g, frost, pn.r.toFloat(), 14.0f);
        g.setColour (col::fg);
        g.setFont (font (12.0f, true));
        g.drawText (pn.title, pn.r.getX() + 14, pn.r.getY() + 8, 200, 16, juce::Justification::centredLeft);
    }
    // title
    g.setFont (font (24.0f, true));
    g.setColour (col::fg);
    g.drawText ("XOXICOA", 24, 12, 130, 28, juce::Justification::centredLeft);
    g.setColour (col::cyan);
    g.drawText ("GLASS", 140, 12, 120, 28, juce::Justification::centredLeft);
    g.setColour (col::dim);
    g.setFont (font (11.0f));
    g.drawText ("de-harsh + tonal balance", 25, 38, 220, 14, juce::Justification::centredLeft);
    g.setFont (font (9.5f, true));
    g.drawText ("TONAL TARGET", 470, 46, 150, 11, juce::Justification::centred);
    g.drawText ("PRESET", 674, 46, 178, 11, juce::Justification::centred);
}

void XoxicoaGlassEditor::paint (juce::Graphics& g)
{
    if (staticLayer.isValid()) g.drawImageAt (staticLayer, 0, 0);
    else g.fillAll (col::bgMid);
    auto pb = presetBox.toFloat();
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRoundedRectangle (pb, 15.0f);
    g.setColour (juce::Colours::white.withAlpha (0.4f));
    g.drawRoundedRectangle (pb, 15.0f, 1.0f);
    g.setColour (col::fg);
    g.setFont (font (13.0f, true));
    g.drawText (proc.presetName, presetBox.reduced (8, 0), juce::Justification::centred, true);
    // AUTO status under the button
    g.setFont (font (9.5f, true));
    const int st = proc.autoState();
    juce::String s = "LISTENS 4 S, THEN SETS IT UP";
    if (st == 1) s = "PLAY AUDIO, LISTENING...";
    else if (st == 3) s = "NO AUDIO HEARD";
    else if (st == 2 && autoFlash > 0) s = "SET FOR THIS SOUND";
    g.setColour (st == 3 ? col::warn : col::dim);
    g.drawText (s, 290, 50, 170, 11, juce::Justification::centred);
    if (st == 1)
    {
        auto bar = autoBtn.getBounds().toFloat().reduced (14.0f, 0).withY ((float) autoBtn.getBottom() - 6.0f).withHeight (2.5f);
        g.setColour (juce::Colours::white.withAlpha (0.2f));
        g.fillRoundedRectangle (bar, 1.2f);
        g.setColour (col::cyan);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * proc.autoProgress()), 1.2f);
    }
}

void XoxicoaGlassEditor::timerCallback()
{
    const int st = proc.autoState();
    if (st == 1) autoBtn.setButtonText ("LISTENING " + juce::String (juce::roundToInt (proc.autoProgress() * 100.0f)) + "%");
    else autoBtn.setButtonText ("AUTO");
    if (st != lastAutoPhase && st == 2) autoFlash = 45;
    if (autoFlash > 0) --autoFlash;
    if (st != lastAutoPhase || st == 1 || autoFlash > 0) repaint (280, 8, 190, 56);
    lastAutoPhase = st;
}
