#pragma once
#include "PluginProcessor.h"
#include "ui/Glass.h"
#include "ui/Controls.h"
#include "ui/SpectrumView.h"

class XoxicoaGlassEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit XoxicoaGlassEditor (XoxicoaGlassProcessor&);
    ~XoxicoaGlassEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int kW = 900, kH = 560;

private:
    void timerCallback() override;
    void buildStaticLayer();
    juce::RangedAudioParameter& prm (const char* id) { return *proc.apvts.getParameter (id); }

    XoxicoaGlassProcessor& proc;
    xg::ui::GlassLookAndFeel lnf;
    juce::Image staticLayer;
    xg::ui::SpectrumView spectrum;
    xg::ui::Knob depth, sharp, speed, lo, hi, mix, out;
    xg::ui::Segmented mode;
    xg::ui::PillToggle delta;
    xg::ui::Selector target;
    juce::TextButton autoBtn { "AUTO" }, prevBtn { "<" }, nextBtn { ">" };
    juce::Rectangle<int> presetBox;
    int lastAutoPhase = -1, autoFlash = 0;
    struct Panel { juce::String title; juce::Rectangle<int> r; };
    std::vector<Panel> panels;
    juce::TooltipWindow tips { this, 600 };
};
