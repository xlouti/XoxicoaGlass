#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include <cmath>

// Tonal balance: long-term 1/3-octave band levels of the input and output, compared with an
// approximate genre target curve. Display / AUTO only: it never changes the sound.
namespace xg
{
inline constexpr int kBands = 28;
inline constexpr std::array<float, kBands> kBandHz { 31.5f, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
                                                     1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000 };

// Approximate target curves: relative 1/3-octave band energy (dB) of a finished, loud modern mix in the style.
// Built from general mix knowledge (a strong sub / 808 shelf, then roughly -3 dB per octave of tilt; Rage keeps
// more bite at 2-6 kHz, Pluggnb rolls the top off sooner and keeps warmer mids). Not measured from any artist's tracks.
struct TargetPoint { float hz, db; };
inline float targetDb (int target, float hz)
{
    static const std::vector<TargetPoint> rage {
        { 20, -4 }, { 31.5f, 1 }, { 50, 3 }, { 63, 3 }, { 80, 2 }, { 100, 0 }, { 160, -2.5f }, { 250, -4.5f }, { 400, -6 }, { 630, -7.5f },
        { 1000, -9 }, { 1600, -10.5f }, { 2500, -11.5f }, { 4000, -13 }, { 6300, -15 }, { 10000, -18.5f }, { 16000, -25 }, { 20000, -31 } };
    static const std::vector<TargetPoint> plugg {
        { 20, -4 }, { 31.5f, 0 }, { 50, 2.5f }, { 63, 2.5f }, { 80, 1.5f }, { 100, 0 }, { 160, -1.5f }, { 250, -3.5f }, { 400, -5.5f }, { 630, -7 },
        { 1000, -9 }, { 1600, -11 }, { 2500, -12.5f }, { 4000, -14.5f }, { 6300, -17 }, { 10000, -21 }, { 16000, -27.5f }, { 20000, -33 } };
    const auto& t = target == 2 ? plugg : rage;
    if (hz <= t.front().hz) return t.front().db;
    for (size_t i = 1; i < t.size(); ++i)
        if (hz <= t[i].hz)
        {
            const float a = std::log2 (t[i - 1].hz), b = std::log2 (t[i].hz), x = (std::log2 (hz) - a) / (b - a);
            return t[i - 1].db + (t[i].db - t[i - 1].db) * x;
        }
    return t.back().db;
}
inline constexpr float kTargetTolerance = 3.0f;   // +-dB band drawn around the curve

class TonalBalance
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        order = fs <= 50000.0 ? 12 : 13;   // ~12 Hz bins
        N = 1 << order;
        fft = std::make_unique<juce::dsp::FFT> (order);
        win.resize ((size_t) N);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
        for (int s = 0; s < 2; ++s) { ring[s].assign ((size_t) N, 0.0f); frame[s].assign ((size_t) N * 2, 0.0f); lev[s].fill (-120.0f); acc[s].fill (0.0); }
        pos = 0; count = 0; accFrames = 0;
        const double bw = fs / N;
        for (int b = 0; b < kBands; ++b)
        {
            const double lo = kBandHz[(size_t) b] * std::pow (2.0, -1.0 / 6.0), hi = kBandHz[(size_t) b] * std::pow (2.0, 1.0 / 6.0);
            k0[(size_t) b] = (int) std::ceil (lo / bw); k1[(size_t) b] = (int) std::floor (hi / bw);
            widthScale[(size_t) b] = 1.0f;
            if (k1[(size_t) b] < k0[(size_t) b]) { k0[(size_t) b] = k1[(size_t) b] = (int) std::round (kBandHz[(size_t) b] / bw); widthScale[(size_t) b] = (float) ((hi - lo) / bw); }
        }
        // 3 s time constant per analysis frame (hop N/2)
        coeff = (float) std::exp (-(N / 2.0) / fs / 3.0);
    }

    // stream 0 = input, 1 = output (mono sums)
    void push (const float* const* in, const float* const* out, int numCh, int n)
    {
        for (int j = 0; j < n; ++j)
        {
            float a = 0, b = 0;
            for (int c = 0; c < numCh; ++c) { a += in[c][j]; b += out[c][j]; }
            ring[0][(size_t) pos] = a / (float) numCh; ring[1][(size_t) pos] = b / (float) numCh;
            if (++pos == N) pos = 0;
            if (++count == N / 2) { count = 0; analyse(); }
        }
    }

    // Long-term band levels, dB (stream 0 input, 1 output). Thread-safe-enough copy for display.
    std::array<float, kBands> levels (int stream) const { const juce::SpinLock::ScopedLockType sl (lock); return lev[stream]; }

    // Plain average while AUTO listens.
    void startAccumulate() { const juce::SpinLock::ScopedLockType sl (lock); acc[0].fill (0.0); accFrames = 0; }
    std::array<float, kBands> accumulated() const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::array<float, kBands> r {};
        for (int b = 0; b < kBands; ++b) r[(size_t) b] = (float) (10.0 * std::log10 (acc[0][(size_t) b] / std::max (1.0, accFrames) + 1e-20));
        return r;
    }

    // Deviation of measured band levels from the target, after lining the curves up over 100 Hz - 5 kHz.
    static std::array<float, kBands> deviation (const std::array<float, kBands>& lv, int target)
    {
        std::array<float, kBands> d {};
        double off = 0; int n = 0;
        for (int b = 0; b < kBands; ++b)
            if (kBandHz[(size_t) b] >= 100 && kBandHz[(size_t) b] <= 5000) { off += lv[(size_t) b] - targetDb (target, kBandHz[(size_t) b]); ++n; }
        off /= std::max (1, n);
        for (int b = 0; b < kBands; ++b) d[(size_t) b] = lv[(size_t) b] - targetDb (target, kBandHz[(size_t) b]) - (float) off;
        return d;
    }
    // mean deviation over a frequency range
    static float rangeDev (const std::array<float, kBands>& dev, float lo, float hi)
    {
        double s = 0; int n = 0;
        for (int b = 0; b < kBands; ++b) if (kBandHz[(size_t) b] >= lo && kBandHz[(size_t) b] <= hi) { s += dev[(size_t) b]; ++n; }
        return n ? (float) (s / n) : 0.0f;
    }

private:
    void analyse()
    {
        std::array<double, kBands> e[2];
        float peak = 0;
        for (int s = 0; s < 2; ++s)
        {
            auto& f = frame[s];
            for (int i = 0; i < N; ++i) { const float x = ring[s][(size_t) ((pos + i) % N)]; f[(size_t) i] = x * win[(size_t) i]; if (s == 0) peak = std::max (peak, std::abs (x)); }
            std::fill (f.begin() + N, f.end(), 0.0f);
            fft->performRealOnlyForwardTransform (f.data(), true);
            for (int b = 0; b < kBands; ++b)
            {
                double sum = 0;
                for (int k = k0[(size_t) b]; k <= k1[(size_t) b]; ++k) sum += (double) f[(size_t) (2 * k)] * f[(size_t) (2 * k)] + (double) f[(size_t) (2 * k + 1)] * f[(size_t) (2 * k + 1)];
                e[s][(size_t) b] = sum * widthScale[(size_t) b] / ((double) N * N / 16.0);
            }
        }
        if (peak < 1e-4f) return;   // ignore silence (keeps the curve when playback stops)
        const juce::SpinLock::ScopedTryLockType sl (lock);   // never wait on the UI from the audio thread
        if (! sl.isLocked()) return;
        for (int s = 0; s < 2; ++s)
            for (int b = 0; b < kBands; ++b)
            {
                const float d = (float) (10.0 * std::log10 (e[s][(size_t) b] + 1e-20));
                auto& l = lev[s][(size_t) b];
                l = l < -100.0f ? d : d + (l - d) * coeff;
            }
        for (int b = 0; b < kBands; ++b) acc[0][(size_t) b] += e[0][(size_t) b];
        accFrames += 1.0;
    }

    double fs = 48000.0;
    int order = 12, N = 4096, pos = 0, count = 0;
    float coeff = 0.99f;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> win, ring[2], frame[2];
    std::array<int, kBands> k0 {}, k1 {};
    std::array<float, kBands> widthScale {};
    std::array<float, kBands> lev[2];
    std::array<double, kBands> acc[2];
    double accFrames = 0;
    mutable juce::SpinLock lock;
};
} // namespace xg
