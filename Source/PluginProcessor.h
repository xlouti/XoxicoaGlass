#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Params.h"
#include "Presets.h"
#include "dsp/DeHarsh.h"
#include "dsp/TonalBalance.h"
#include "dsp/AutoSet.h"

class XoxicoaGlassProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    XoxicoaGlassProcessor();
    ~XoxicoaGlassProcessor() override { stopTimer(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

    int getNumPrograms() override { return (int) xg::factoryPresets().size(); }
    int getCurrentProgram() override { return juce::jmax (0, presetIndex); }
    void setCurrentProgram (int index) override { loadPreset (index); }
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void applyPatch (const xg::Patch& patch);
    void loadPreset (int index);
    void stepPreset (int delta);

    // AUTO: listen for kAutoSeconds of signal, then set Depth, Sharpness, Speed and Focus.
    static constexpr double kAutoSeconds = 4.0;
    void startAuto();
    void cancelAuto();
    bool applyAutoIfReady();               // called by the timer (message thread) or directly by the tests
    int autoState() const { return autoPhase.load(); }   // 0 idle, 1 listening, 2 just applied, 3 no signal
    float autoProgress() const { return autoProg.load(); }
    xg::AutoResult lastAuto() const { return autoResult; }

    juce::AudioProcessorValueTreeState apvts;
    xg::DeHarsh deharsh;
    xg::TonalBalance tonal;
    int presetIndex = 0;
    juce::String presetName { "Init" };
    std::function<void()> onPresetChanged;
    std::atomic<float> meterIn { -120.0f }, meterOut { -120.0f }, meterGr { 0.0f };

private:
    void timerCallback() override { applyAutoIfReady(); }

    std::array<std::atomic<float>*, xg::kNumParams> raw {};
    juce::AudioBuffer<float> dryBuf, inCopy;
    juce::SmoothedValue<float> mixSm, outSm, deltaSm;
    xg::AutoStats stats;
    std::atomic<bool> autoRequest { false }, autoCancel { false }, autoReady { false };
    std::atomic<int> autoPhase { 0 };
    std::atomic<float> autoProg { 0.0f };
    bool listening = false;
    double listenSamples = 0, listenTotal = 0;
    xg::AutoResult autoResult, pending;
    double sr = 48000.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XoxicoaGlassProcessor)
};
