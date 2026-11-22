// Renders the real editor to a PNG without opening a window.  usage: <tool> out.png [scale] [presetIndex]
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "AuditCommon.h"
#include "Material.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out (argc > 1 ? juce::String (argv[1]) : juce::String ("ui.png"));
    const float scale = argc > 2 ? (float) std::atof (argv[2]) : 1.0f;
    PROC_CLASS proc;
    proc.loadPreset (argc > 3 ? std::atoi (argv[3]) : 1);
    // feed some harsh material so the spectrum, reduction and tonal balance are live
    auto mat = material::fullMix (8.0);
    audit::process (proc, mat);
    {   // keep playing (no silent tail) so the live spectrum shows music, not the flush at the end
        juce::AudioBuffer<float> blk (2, 512); juce::MidiBuffer midi;
        for (int pos = 0; pos + 512 <= mat.getNumSamples() / 2; pos += 512)
        {
            for (int c = 0; c < 2; ++c) blk.copyFrom (c, 0, mat, c, pos, 512);
            proc.processBlock (blk, midi);
        }
    }
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setVisible (true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    // time the static layer build and one spectrum frame (UI cost on old machines)
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    ed->setSize (900, 560); ed->resized();
    const auto t1 = juce::Time::getMillisecondCounterHiRes();
    juce::Image probe (juce::Image::ARGB, 900, 560, true);
    const int frames = 60;
    for (int i = 0; i < frames; ++i) { juce::Graphics g (probe); ed->paintEntireComponent (g, false); }
    const auto t2 = juce::Time::getMillisecondCounterHiRes();
    std::printf ("static layer build %.1f ms, full window repaint %.2f ms each (spectrum-only repaints are smaller)\n", t1 - t0, (t2 - t1) / frames);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, os);
    std::printf ("wrote %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return 0;
}
