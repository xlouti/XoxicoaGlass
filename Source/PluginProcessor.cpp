#include "PluginProcessor.h"
#include "PluginEditor.h"

XoxicoaGlassProcessor::XoxicoaGlassProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", xg::createLayout())
{
    const auto& defs = xg::paramDefs();
    jassert ((int) defs.size() == xg::kNumParams);
    for (int i = 0; i < xg::kNumParams; ++i) raw[(size_t) i] = apvts.getRawParameterValue (defs[(size_t) i].id);
    loadPreset (0);
    deharsh.prepare (48000.0);
    tonal.prepare (48000.0);
    startTimerHz (20);
}

bool XoxicoaGlassProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void XoxicoaGlassProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    deharsh.prepare (sampleRate);
    tonal.prepare (sampleRate);
    setLatencySamples (deharsh.latency());
    const int maxBlock = juce::jmax (256, samplesPerBlock);
    dryBuf.setSize (2, maxBlock, false, true, true);
    inCopy.setSize (2, maxBlock, false, true, true);
    for (auto* s : { &mixSm, &outSm, &deltaSm }) s->reset (sampleRate, 0.03);
    mixSm.setCurrentAndTargetValue (raw[xg::mix]->load());
    outSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw[xg::output]->load()));
    deltaSm.setCurrentAndTargetValue (raw[xg::delta]->load() > 0.5f ? 1.0f : 0.0f);
    listening = false;
    if (autoPhase.load() == 1) autoPhase = 0;
}

void XoxicoaGlassProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    const int numCh = juce::jmin (2, buffer.getNumChannels(), getTotalNumInputChannels());
    const int n = buffer.getNumSamples();
    for (int c = numCh; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    if (numCh == 0 || n == 0) return;

    if (autoCancel.exchange (false)) { listening = false; autoPhase = 0; }
    if (autoRequest.exchange (false))
    {
        stats.reset (deharsh.numBins());
        tonal.startAccumulate();
        listening = true; listenTotal = 0; autoPhase = 1; autoProg = 0.0f;
    }

    xg::DeHarshSettings s;
    s.depth = raw[xg::depth]->load(); s.sharp = raw[xg::sharp]->load(); s.speed = raw[xg::speed]->load();
    s.lo = raw[xg::focus_lo]->load(); s.hi = raw[xg::focus_hi]->load(); s.mode = (int) raw[xg::mode]->load();
    mixSm.setTargetValue (raw[xg::mix]->load());
    outSm.setTargetValue (juce::Decibels::decibelsToGain (raw[xg::output]->load()));
    deltaSm.setTargetValue (raw[xg::delta]->load() > 0.5f ? 1.0f : 0.0f);

    float inPk = 0.0f, outPk = 0.0f;
    for (int start = 0; start < n; start += dryBuf.getNumSamples())
    {
        const int len = juce::jmin (dryBuf.getNumSamples(), n - start);
        float* ch[2] {}; float* dry[2] {}; const float* in[2] {}; const float* out[2] {};
        for (int c = 0; c < numCh; ++c)
        {
            ch[c] = buffer.getWritePointer (c, start);
            dry[c] = dryBuf.getWritePointer (c);
            inCopy.copyFrom (c, 0, ch[c], len);
            in[c] = inCopy.getReadPointer (c); out[c] = ch[c];
            inPk = juce::jmax (inPk, inCopy.getMagnitude (c, 0, len));
        }
        deharsh.process (ch, numCh, len, s, dry, listening ? &stats : nullptr);
        for (int j = 0; j < len; ++j)
        {
            const float m = mixSm.getNextValue(), d = deltaSm.getNextValue(), g = outSm.getNextValue();
            for (int c = 0; c < numCh; ++c)
            {
                const float dr = dry[c][j];
                const float mixed = dr + (ch[c][j] - dr) * m;
                const float removed = dr - mixed;
                ch[c][j] = (mixed + (removed - mixed) * d) * g;
            }
        }
        for (int c = 0; c < numCh; ++c) outPk = juce::jmax (outPk, buffer.getMagnitude (c, start, len));
        tonal.push (in, out, numCh, len);
    }

    if (listening)
    {
        listenTotal += n;
        const double heard = stats.frames * (deharsh.latency() / 4.0) / sr;
        autoProg = (float) juce::jmin (1.0, heard / kAutoSeconds);
        if (heard >= kAutoSeconds || (listenTotal > 15.0 * sr && heard >= 1.0))
        {
            listening = false;
            autoReady = true;      // the message thread reads the stats now (the audio thread no longer touches them)
        }
        else if (listenTotal > 15.0 * sr)
        {
            listening = false; autoPhase = 3;   // nothing to listen to
        }
    }
    meterIn = juce::Decibels::gainToDecibels (inPk, -120.0f);
    meterOut = juce::Decibels::gainToDecibels (outPk, -120.0f);
    meterGr = deharsh.lastReductionDb();
}

void XoxicoaGlassProcessor::startAuto() { autoReady = false; autoRequest = true; autoPhase = 1; autoProg = 0.0f; }
void XoxicoaGlassProcessor::cancelAuto() { autoRequest = false; autoCancel = true; autoPhase = 0; }

bool XoxicoaGlassProcessor::applyAutoIfReady()
{
    if (! autoReady.exchange (false)) return false;
    const int target = (int) raw[xg::target]->load();
    const auto tonalAcc = tonal.accumulated();
    auto r = xg::decideAuto (stats, deharsh.binHz(), &tonalAcc, target);
    if (! r.valid) { autoPhase = 3; return false; }
    auto set = [this] (const char* id, float v)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (v));
            p->endChangeGesture();
        }
    };
    set ("depth", r.depth); set ("sharp", r.sharp); set ("speed", r.speed);
    set ("focus_lo", r.lo); set ("focus_hi", r.hi);
    autoResult = r;
    autoPhase = 2;
    presetIndex = -1;
    presetName = "Auto Set";
    if (onPresetChanged) onPresetChanged();
    return true;
}

const juce::String XoxicoaGlassProcessor::getProgramName (int index)
{
    const auto& p = xg::factoryPresets();
    return juce::isPositiveAndBelow (index, (int) p.size()) ? p[(size_t) index].name : juce::String();
}

void XoxicoaGlassProcessor::applyPatch (const xg::Patch& patch)
{
    for (auto& d : xg::paramDefs())
        if (auto* prm = apvts.getParameter (d.id))
        {
            auto it = patch.find (d.id);
            const float v = it != patch.end() ? it->second : d.def;
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
        }
}

void XoxicoaGlassProcessor::loadPreset (int index)
{
    const auto& list = xg::factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) list.size())) return;
    presetIndex = index;
    presetName = list[(size_t) index].name;
    applyPatch (list[(size_t) index].values);
    if (onPresetChanged) onPresetChanged();
}

void XoxicoaGlassProcessor::stepPreset (int delta)
{
    const int n = (int) xg::factoryPresets().size();
    loadPreset (((juce::jmax (0, presetIndex) + delta) % n + n) % n);
}

void XoxicoaGlassProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("presetIndex", presetIndex, nullptr);
    state.setProperty ("presetName", presetName, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void XoxicoaGlassProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            presetIndex = state.getProperty ("presetIndex", 0);
            presetName = state.getProperty ("presetName", "Init").toString();
            apvts.replaceState (state);
            if (onPresetChanged) onPresetChanged();
        }
}

juce::AudioProcessorEditor* XoxicoaGlassProcessor::createEditor() { return new XoxicoaGlassEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new XoxicoaGlassProcessor(); }
