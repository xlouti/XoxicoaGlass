// Shared offline test harness pieces (runs audio through the real processor).
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>

namespace audit
{
constexpr double kFs = 48000.0;
constexpr int kBlock = 256;

// Runs a stereo buffer through the processor in host-sized blocks and returns the output with the
// reported latency removed (so output sample i lines up with input sample i).
template <typename Proc>
juce::AudioBuffer<float> process (Proc& proc, const juce::AudioBuffer<float>& in, double* cpuSecs = nullptr, int block = kBlock, double fs = kFs)
{
    proc.setPlayConfigDetails (2, 2, fs, block);
    proc.prepareToPlay (fs, block);
    const int lat = proc.getLatencySamples();
    const int total = in.getNumSamples();
    juce::AudioBuffer<float> out (2, total), blk (2, block);
    juce::MidiBuffer midi;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int pos = 0; pos < total + lat; pos += block)
    {
        const int n = block;
        blk.clear();
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i) { const int s = pos + i; blk.setSample (c, i, s < total ? in.getSample (c, s) : 0.0f); }
        proc.processBlock (blk, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i) { const int s = pos + i - lat; if (s >= 0 && s < total) out.setSample (c, s, blk.getSample (c, i)); }
    }
    if (cpuSecs) *cpuSecs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    return out;
}

inline juce::AudioBuffer<float> slice (const juce::AudioBuffer<float>& b, double from, double to = -1, double fs = kFs)
{
    const int s0 = (int) (from * fs), s1 = to < 0 ? b.getNumSamples() : juce::jmin (b.getNumSamples(), (int) (to * fs));
    juce::AudioBuffer<float> r (2, juce::jmax (1, s1 - s0));
    for (int c = 0; c < 2; ++c) r.copyFrom (c, 0, b, juce::jmin (c, b.getNumChannels() - 1), s0, s1 - s0);
    return r;
}

struct Features
{
    float rmsDb = -200, peakDb = -200, sideDb = 0;
    std::vector<float> spec, env, bands;
    bool finite = true;
};

inline Features analyse (const juce::AudioBuffer<float>& b)
{
    Features f;
    const int n = b.getNumSamples();
    const float* L = b.getReadPointer (0); const float* R = b.getReadPointer (1);
    double s2 = 0, m2 = 0, d2 = 0; float pk = 0;
    for (int i = 0; i < n; ++i)
    {
        if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) { f.finite = false; continue; }
        const double m = 0.5 * (L[i] + R[i]), d = 0.5 * (L[i] - R[i]);
        s2 += 0.5 * (L[i] * (double) L[i] + R[i] * (double) R[i]); m2 += m * m; d2 += d * d;
        pk = juce::jmax (pk, std::abs (L[i]), std::abs (R[i]));
    }
    f.rmsDb = (float) (10.0 * std::log10 (s2 / n + 1e-20));
    f.peakDb = juce::Decibels::gainToDecibels (pk, -200.0f);
    f.sideDb = (float) juce::jlimit (-40.0, 6.0, 10.0 * std::log10 ((d2 + 1e-12) / (m2 + 1e-12)));
    const int fr = (int) (0.02 * kFs);
    for (int s = 0; s + fr <= n; s += fr)
    {
        double e = 0;
        for (int i = s; i < s + fr; ++i) e += 0.5 * (L[i] * (double) L[i] + R[i] * (double) R[i]);
        f.env.push_back ((float) juce::jmax (-90.0, 10.0 * std::log10 (e / fr + 1e-20)));
    }
    {
        juce::IIRFilter lp, hp, lp3;
        lp.setCoefficients (juce::IIRCoefficients::makeLowPass (kFs, 300.0));
        lp3.setCoefficients (juce::IIRCoefficients::makeLowPass (kFs, 3000.0));
        hp.setCoefficients (juce::IIRCoefficients::makeHighPass (kFs, 3000.0));
        double e[3] {}; int k = 0;
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            const float lo = lp.processSingleSampleRaw (m), mid3 = lp3.processSingleSampleRaw (m), hi = hp.processSingleSampleRaw (m);
            e[0] += lo * (double) lo; e[1] += (mid3 - lo) * (double) (mid3 - lo); e[2] += hi * (double) hi;
            if (++k == fr)
            {
                for (auto& x : e) { f.bands.push_back ((float) juce::jmax (-90.0, 10.0 * std::log10 (x / fr + 1e-20))); x = 0; }
                k = 0;
            }
        }
    }
    constexpr int order = 13, N = 1 << order;
    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> win (N, juce::dsp::WindowingFunction<float>::hann);
    std::vector<double> pw (N / 2, 0.0);
    std::vector<float> buf (N * 2);
    int frames = 0;
    for (int s = 0; s + N <= n; s += N / 2, ++frames)
    {
        for (int i = 0; i < N; ++i) buf[(size_t) i] = 0.5f * (L[s + i] + R[s + i]);
        std::fill (buf.begin() + N, buf.end(), 0.0f);
        win.multiplyWithWindowingTable (buf.data(), N);
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 0; k < N / 2; ++k) pw[(size_t) k] += buf[(size_t) k] * (double) buf[(size_t) k];
    }
    for (double fc = 30.0; fc < 18000.0; fc *= std::pow (2.0, 1.0 / 12.0))
    {
        const int k0 = (int) (fc * std::pow (2.0, -1.0 / 24.0) / kFs * N), k1 = juce::jmax (k0 + 1, (int) (fc * std::pow (2.0, 1.0 / 24.0) / kFs * N));
        double e = 0;
        for (int k = k0; k < k1 && k < N / 2; ++k) e += pw[(size_t) k];
        f.spec.push_back ((float) juce::jmax (-100.0, 10.0 * std::log10 (e / juce::jmax (1, frames) + 1e-20)));
    }
    return f;
}

// band3 = the largest level change in any 1/3-octave band (relative spectra), divided by 1.5: a de-harsher works on
// narrow regions, so "one 1/3-octave band moved by 1.5 dB or more" counts as audible even if the overall spectrum barely moves.
struct Dist { float rms, spec, env, side, peak, band, band3; float score() const { return std::max ({ std::abs (rms), spec, env, std::abs (side) * 0.5f, std::abs (peak), band, band3 / 1.5f }); } };

inline Dist distance (const Features& a, const Features& b)
{
    Dist d {};
    d.rms = b.rmsDb - a.rmsDb; d.peak = b.peakDb - a.peakDb; d.side = b.sideDb - a.sideDb;
    float ma = -200, mb = -200;
    for (size_t i = 0; i < a.spec.size(); ++i) { ma = juce::jmax (ma, a.spec[i]); mb = juce::jmax (mb, b.spec[i]); }
    double acc = 0; int cnt = 0;
    for (size_t i = 0; i < a.spec.size(); ++i)
    {
        const float x = juce::jmax (a.spec[i] - ma, -60.0f), y = juce::jmax (b.spec[i] - mb, -60.0f);
        if (x > -60.0f || y > -60.0f) { acc += std::abs (x - y); ++cnt; }
    }
    d.spec = cnt ? (float) (acc / cnt) : 0.0f;
    d.band3 = 0.0f;
    for (size_t i = 0; i + 4 <= a.spec.size(); i += 4)   // 4 x 1/12 octave = 1/3 octave
    {
        double ea = 0, eb = 0;
        for (size_t j = i; j < i + 4; ++j) { ea += std::pow (10.0, (a.spec[j] - ma) / 10.0); eb += std::pow (10.0, (b.spec[j] - mb) / 10.0); }
        const double la = 10.0 * std::log10 (ea + 1e-12), lb = 10.0 * std::log10 (eb + 1e-12);
        if (la > -50.0 || lb > -50.0) d.band3 = std::max (d.band3, (float) std::abs (la - lb));
    }
    acc = 0; cnt = 0;
    for (size_t i = 0; i < juce::jmin (a.env.size(), b.env.size()); ++i)
        if (a.env[i] > -70.0f || b.env[i] > -70.0f) { acc += std::abs (juce::jmax (a.env[i], -70.0f) - juce::jmax (b.env[i], -70.0f)); ++cnt; }
    d.env = cnt ? (float) (acc / cnt) : 0.0f;
    acc = 0; cnt = 0;
    for (size_t i = 0; i < juce::jmin (a.bands.size(), b.bands.size()); ++i)
        if (a.bands[i] > -70.0f || b.bands[i] > -70.0f) { acc += std::abs (juce::jmax (a.bands[i], -70.0f) - juce::jmax (b.bands[i], -70.0f)); ++cnt; }
    d.band = cnt ? (float) (acc / cnt) : 0.0f;
    return d;
}

inline juce::String fmt (float v, int w = 7) { return juce::String (v, 2).paddedLeft (' ', w); }

inline bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double fs = kFs)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (! os) return false;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), fs, 2, 24, {}, 0));
    if (! w) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

// Loudest 400 ms window RMS (dB).
inline float loudestWindowDb (const juce::AudioBuffer<float>& b)
{
    const int n = b.getNumSamples(), win = (int) (0.4 * kFs);
    std::vector<double> c ((size_t) n + 1, 0.0);
    for (int i = 0; i < n; ++i) { const float l = b.getSample (0, i), r = b.getSample (1, i); c[(size_t) i + 1] = c[(size_t) i] + 0.5 * (l * (double) l + r * (double) r); }
    double best = 1e-20;
    for (int i = 0; i + win <= n; i += 480) best = std::max (best, (c[(size_t) (i + win)] - c[(size_t) i]) / win);
    return (float) (10.0 * std::log10 (best));
}
} // namespace audit
