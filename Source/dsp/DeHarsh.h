#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>
#include <algorithm>

// Xoxicoa Glass: dynamic resonance smoothing ("de-harsh").
// STFT, Hann analysis + Hann synthesis, 75 % overlap. FFT 1024 at 44.1/48 kHz (2048 at 88.2/96, 4096 above),
// so the time and frequency resolution stay the same at every sample rate. Latency = FFT size.
// Per frame: linked-stereo power spectrum -> dB -> smooth "neighbourhood" baseline across frequency ->
// how far each bin sticks out above its neighbours = excess -> gain reduction (threshold, ratio, knee, cap)
// -> focus window -> spread across frequency (Sharpness) -> attack / release per bin (Speed) -> mask both channels.
namespace xg
{
struct DeHarshSettings
{
    float depth = 0.5f, sharp = 0.5f, speed = 0.5f, lo = 1500.0f, hi = 16000.0f;
    int mode = 0;   // 0 Soft, 1 Hard
};

// What AUTO collects while it listens (per bin, summed over frames that carry signal).
struct AutoStats
{
    std::vector<double> narrow, broad;
    double frames = 0, flux = 0;
    void reset (int bins) { narrow.assign ((size_t) bins, 0.0); broad.assign ((size_t) bins, 0.0); frames = 0; flux = 0; }
};

class DeHarsh
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        order = fs <= 50000.0 ? 10 : (fs <= 100000.0 ? 11 : 12);
        N = 1 << order; hop = N / 4; bins = N / 2 + 1;
        fft = std::make_unique<juce::dsp::FFT> (order);
        win.resize ((size_t) N);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
        for (int c = 0; c < 2; ++c)
        {
            inBuf[c].assign ((size_t) N, 0.0f); outAcc[c].assign ((size_t) N, 0.0f); outReady[c].assign ((size_t) hop, 0.0f);
            frame[c].assign ((size_t) N * 2, 0.0f); dryRing[c].assign ((size_t) N, 0.0f);
        }
        for (auto* v : { &tops, &powr, &envP, &envDb, &ldb, &base, &tmp, &grTarget, &grSm, &grSpread, &focusW, &freq, &prevLdb, &hw, &lowLift, &aEnv, &aTops, &aBase, &aNarrow, &aBroad, &dispIn, &dispOut, &dispGr })
            v->assign ((size_t) bins, 0.0f);
        for (auto& v : dispIn) v = -120.0f;
        for (auto& v : dispOut) v = -120.0f;
        prefix.assign ((size_t) bins + 1, 0.0);
        for (int k = 0; k < bins; ++k)
        {
            freq[(size_t) k] = (float) (k * fs / N);
            // AUTO's harshness weighting: the ear is most touchy about roughly 2.5-9 kHz
            const float f = juce::jmax (1.0f, freq[(size_t) k]);
            hw[(size_t) k] = juce::jlimit (0.08f, 1.0f, 1.0f - 0.7f * juce::jmax (0.0f, std::log2 (2500.0f / f))
                                                             - 0.35f * juce::jmax (0.0f, std::log2 (f / 9000.0f)));
            // Below ~1.2 kHz single notes and their first harmonics sit far apart and look like peaks.
            // Raise the threshold there (up to +6 dB at 400 Hz and below) so bass / chord fundamentals are left alone.
            lowLift[(size_t) k] = 6.0f * juce::jlimit (0.0f, 1.0f, std::log2 (1200.0f / f) / 1.585f);
        }
        fill = 0; dryPos = 0; lastLo = lastHi = -1.0f;
        // Measure the round-trip scale (FFT engines differ) so unity mask = bit-for-bit level match.
        std::vector<float> t ((size_t) N * 2, 0.0f);
        t[1] = 1.0f;
        fft->performRealOnlyForwardTransform (t.data(), true);
        fft->performRealOnlyInverseTransform (t.data());
        const float overlap = 1.5f;   // sum of hann^2 at 75 % overlap
        olaScale = 1.0f / (juce::jmax (1e-9f, t[1]) * overlap);
        normDb = 20.0f * std::log10 ((float) N / 4.0f);   // full-scale sine -> about 0 dB
        hopSec = (float) hop / (float) fs;
    }

    int latency() const { return N; }
    int numBins() const { return bins; }
    float binHz() const { return (float) (fs / N); }
    float lastReductionDb() const { return lastMaxGr; }

    // In place: ch becomes the processed (wet) signal; dryOut gets the input delayed by latency().
    void process (float* const* ch, int numCh, int n, const DeHarshSettings& s, float* const* dryOut, AutoStats* stats)
    {
        numCh = juce::jlimit (1, 2, numCh);
        for (int j = 0; j < n; ++j)
        {
            for (int c = 0; c < numCh; ++c)
            {
                const float x = ch[c][j];
                dryOut[c][j] = dryRing[c][(size_t) dryPos];
                dryRing[c][(size_t) dryPos] = x;
                inBuf[c][(size_t) (N - hop + fill)] = x;
                ch[c][j] = outReady[c][(size_t) fill];
            }
            if (++dryPos == N) dryPos = 0;
            if (++fill == hop) { runFrame (numCh, s, stats); fill = 0; }
        }
    }

    // Display snapshot (dB per bin): input level, output level, gain reduction. Called from the UI thread.
    void copyDisplay (std::vector<float>& in, std::vector<float>& out, std::vector<float>& gr)
    {
        const juce::SpinLock::ScopedLockType sl (dispLock);
        in = dispIn; out = dispOut; gr = dispGr;
    }

    // Excess of each bin over a robust neighbourhood mean of the given width (octaves). Public for AUTO + tests.
    void baseline (const std::vector<float>& src, float widthOct, std::vector<float>& dst)
    {
        meanAcross (src, widthOct, 2, dst);
        for (int k = 0; k < bins; ++k) tmp[(size_t) k] = std::min (src[(size_t) k], dst[(size_t) k] + 2.0f);
        meanAcross (tmp, widthOct, 2, dst);
    }

private:
    void maxAcross (const std::vector<float>& src, float widthOct, std::vector<float>& dst)
    {
        const float h = std::exp2 (widthOct * 0.5f);
        for (int k = 0; k < bins; ++k)
        {
            const int a = juce::jlimit (0, bins - 1, std::min ((int) ((float) k / h), k - 1));
            const int b = juce::jlimit (0, bins - 1, std::max ((int) std::ceil ((float) k * h), k + 1));
            float m = src[(size_t) a];
            for (int i = a + 1; i <= b; ++i) m = std::max (m, src[(size_t) i]);
            dst[(size_t) k] = m;
        }
    }
    void meanAcross (const std::vector<float>& src, float widthOct, int minHalf, std::vector<float>& dst)
    {
        prefix[0] = 0.0;
        for (int k = 0; k < bins; ++k) prefix[(size_t) k + 1] = prefix[(size_t) k] + src[(size_t) k];
        const float h = std::exp2 (widthOct * 0.5f);
        for (int k = 0; k < bins; ++k)
        {
            int a = (int) ((float) k / h), b = (int) std::ceil ((float) k * h);
            a = juce::jlimit (0, bins - 1, std::min (a, k - minHalf));
            b = juce::jlimit (0, bins - 1, std::max (b, k + minHalf));
            dst[(size_t) k] = (float) ((prefix[(size_t) b + 1] - prefix[(size_t) a]) / (double) (b - a + 1));
        }
    }

    void updateFocus (float lo, float hi)
    {
        if (lo == lastLo && hi == lastHi) return;
        lastLo = lo; lastHi = hi;
        const float a = std::log2 (juce::jmax (20.0f, std::min (lo, hi))), b = std::log2 (juce::jmax (lo, hi));
        constexpr float fade = 0.5f;   // half-octave raised-cosine edges
        for (int k = 0; k < bins; ++k)
        {
            const float lf = std::log2 (juce::jmax (1.0f, freq[(size_t) k]));
            float w = 1.0f;
            if (lf < a) w = lf < a - fade ? 0.0f : 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (a - lf) / fade);
            else if (lf > b) w = lf > b + fade ? 0.0f : 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (lf - b) / fade);
            focusW[(size_t) k] = w;
        }
    }

    // Detector excess (dB above the harmonic-aware baseline, minus the low-frequency lift) at a given Sharpness.
    // Uses powr from the current frame; scratch vectors aEnv / aTops / aBase. Only used while AUTO listens.
    void detectExcess (float sharp, std::vector<float>& out)
    {
        meanAcross (powr, 0.12f * (1.0f - sharp) + 0.02f, 1, aEnv);
        for (int k = 0; k < bins; ++k) aEnv[(size_t) k] = 10.0f * std::log10 (aEnv[(size_t) k] + 1e-20f);
        maxAcross (aEnv, 0.5f - 0.42f * sharp, aTops);
        baseline (aTops, 1.5f - 0.9f * sharp, aBase);
        for (int k = 0; k < bins; ++k) out[(size_t) k] = aEnv[(size_t) k] - aBase[(size_t) k] - lowLift[(size_t) k];
    }

    void runFrame (int numCh, const DeHarshSettings& s, AutoStats* stats)
    {
        for (int c = 0; c < numCh; ++c)
        {
            auto& f = frame[c];
            for (int i = 0; i < N; ++i) f[(size_t) i] = inBuf[c][(size_t) i] * win[(size_t) i];
            std::fill (f.begin() + N, f.end(), 0.0f);
            fft->performRealOnlyForwardTransform (f.data(), true);
        }
        // linked power -> dB
        float frameMax = -200.0f;
        const float chScale = numCh == 2 ? 0.5f : 1.0f;
        for (int k = 0; k < bins; ++k)
        {
            float p = 0.0f;
            for (int c = 0; c < numCh; ++c) { const float re = frame[c][(size_t) (2 * k)], im = frame[c][(size_t) (2 * k + 1)]; p += re * re + im * im; }
            const float d = 10.0f * std::log10 (p * chScale + 1e-20f) - normDb;
            ldb[(size_t) k] = d;
            if (k > 0) frameMax = std::max (frameMax, d);
        }
        const float gate = std::max (frameMax - 60.0f, -100.0f);

        for (int k = 0; k < bins; ++k) powr[(size_t) k] = std::pow (10.0f, 0.1f * ldb[(size_t) k]);

        // AUTO statistics: run the same harmonic-aware detector at two fixed settings (narrow = Sharpness 0.85,
        // broad = Sharpness 0.5) so AUTO judges the audio the way the suppressor will actually see it.
        // only 150 Hz - 19 kHz is judged.
        if (stats != nullptr && frameMax > -62.0f)
        {
            if ((int) stats->narrow.size() != bins) stats->reset (bins);
            detectExcess (0.85f, aNarrow);
            detectExcess (0.5f, aBroad);
            double fl = 0; int fc = 0;
            for (int k = 1; k < bins; ++k)
            {
                const float f = freq[(size_t) k];
                if (f < 150.0f || f > 19000.0f || ldb[(size_t) k] < gate) continue;
                const float lw = hw[(size_t) k];
                stats->narrow[(size_t) k] += lw * std::max (0.0f, aNarrow[(size_t) k] - 3.0f);
                stats->broad[(size_t) k]  += lw * std::max (0.0f, aBroad[(size_t) k] - 2.0f);
                if (f > 4000.0f) { fl += std::max (0.0f, ldb[(size_t) k] - prevLdb[(size_t) k]); ++fc; }
            }
            stats->flux += fc ? fl / fc : 0.0;
            stats->frames += 1.0;
        }
        std::copy (ldb.begin(), ldb.end(), prevLdb.begin());

        // gain-reduction target
        //  1. detection level: power lightly averaged across frequency (smooths noise; wider at low Sharpness)
        //  2. "tops" envelope: the local maximum of that level over a window (1/2 octave .. 1/12 octave with Sharpness).
        //     It runs across the tops of ordinary harmonics, so a normal bright sound is NOT seen as ringing;
        //     a region (or, at high Sharpness, a single tone) that rings above the tops around it is.
        //  3. baseline: robust neighbourhood mean of the tops (1.5 .. 0.6 octave); excess = level - baseline
        //  4. threshold / knee / ratio / cap (Mode, Depth) -> focus window
        const float sharp = juce::jlimit (0.0f, 1.0f, s.sharp), depth = juce::jlimit (0.0f, 1.0f, s.depth);
        const float detW = 0.12f * (1.0f - sharp) + 0.02f;
        meanAcross (powr, detW, 1, envP);
        for (int k = 0; k < bins; ++k) envDb[(size_t) k] = 10.0f * std::log10 (envP[(size_t) k] + 1e-20f);
        maxAcross (envDb, 0.5f - 0.42f * sharp, tops);
        baseline (tops, 1.5f - 0.9f * sharp, base);
        updateFocus (s.lo, s.hi);
        const bool hard = s.mode == 1;
        const float thr = hard ? 0.0f : 1.0f, ratio = (hard ? 3.2f : 2.0f) * 2.0f * depth, knee = hard ? 1.5f : 4.0f, cap = 24.0f * depth;
        for (int k = 0; k < bins; ++k)
        {
            float g = 0.0f;
            if (ldb[(size_t) k] >= gate && focusW[(size_t) k] > 0.0f)
            {
                const float x = envDb[(size_t) k] - base[(size_t) k] - thr - lowLift[(size_t) k];
                const float over = x <= -0.5f * knee ? 0.0f : (x >= 0.5f * knee ? x : (x + 0.5f * knee) * (x + 0.5f * knee) / (2.0f * knee));
                g = std::min (cap, over * ratio) * focusW[(size_t) k];
            }
            grTarget[(size_t) k] = g;
        }
        // Sharpness, low end: spread the cuts over up to half an octave (broad, smooth); high end: bin-narrow cuts
        const float spread = (1.0f - sharp) * 0.5f;
        if (spread > 0.02f)
        {
            meanAcross (grTarget, spread, 1, grSpread);   // widen each cut without making its centre shallower
            for (int k = 0; k < bins; ++k) grTarget[(size_t) k] = std::max (grTarget[(size_t) k], std::min (cap, grSpread[(size_t) k] * 2.0f));
        }
        // Speed: slow = attack 300 ms, release 1.2 s (the cut follows the average: smooth, leaves transients alone);
        //        fast = attack 1 ms, release 15 ms (the cut grabs every ringing hit and note, and lets go right after)
        const float sp = juce::jlimit (0.0f, 1.0f, s.speed) * juce::jlimit (0.0f, 1.0f, s.speed);   // mid knob = about 70 ms / 400 ms
        const float att = 0.3f * std::pow (1.0f / 300.0f, sp), rel = 1.2f * std::pow (0.015f / 1.2f, sp);
        const float ca = std::exp (-hopSec / att), cr = std::exp (-hopSec / rel);
        float maxGr = 0.0f;
        for (int k = 0; k < bins; ++k)
        {
            float& g = grSm[(size_t) k];
            const float t = grTarget[(size_t) k];
            g = t > g ? t + (g - t) * ca : t + (g - t) * cr;
            maxGr = std::max (maxGr, g);
        }
        lastMaxGr = maxGr;
        // apply mask, inverse, overlap-add
        for (int c = 0; c < numCh; ++c)
        {
            auto& f = frame[c];
            for (int k = 0; k < bins; ++k)
            {
                const float gl = std::exp (-0.11512925f * grSm[(size_t) k]);
                f[(size_t) (2 * k)] *= gl; f[(size_t) (2 * k + 1)] *= gl;
            }
            fft->performRealOnlyInverseTransform (f.data());
            auto& acc = outAcc[c];
            for (int i = 0; i < N; ++i) acc[(size_t) i] += f[(size_t) i] * win[(size_t) i] * olaScale;
            std::copy (acc.begin(), acc.begin() + hop, outReady[c].begin());
            std::copy (acc.begin() + hop, acc.end(), acc.begin());
            std::fill (acc.end() - hop, acc.end(), 0.0f);
            std::copy (inBuf[c].begin() + hop, inBuf[c].end(), inBuf[c].begin());
        }
        // display (smoothed in dB, ~120 ms fall)
        if (dispLock.tryEnter())
        {
            for (int k = 0; k < bins; ++k)
            {
                const float in = ldb[(size_t) k], out = in - grSm[(size_t) k];
                auto& di = dispIn[(size_t) k]; auto& dout = dispOut[(size_t) k]; auto& dg = dispGr[(size_t) k];
                di = in > di ? in : di + (in - di) * 0.25f;
                dout = out > dout ? out : dout + (out - dout) * 0.25f;
                dg += (grSm[(size_t) k] - dg) * 0.35f;
            }
            dispLock.exit();
        }
    }

    double fs = 48000.0;
    int order = 10, N = 1024, hop = 256, bins = 513, fill = 0, dryPos = 0;
    float olaScale = 1.0f, normDb = 0.0f, hopSec = 0.005f, lastLo = -1, lastHi = -1, lastMaxGr = 0.0f;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> win, inBuf[2], outAcc[2], outReady[2], frame[2], dryRing[2];
    std::vector<float> tops, powr, envP, envDb, ldb, base, tmp, grTarget, grSm, grSpread, focusW, freq, prevLdb, hw, lowLift, aEnv, aTops, aBase, aNarrow, aBroad;
    std::vector<double> prefix;
    juce::SpinLock dispLock;
    std::vector<float> dispIn, dispOut, dispGr;
};
} // namespace xg
