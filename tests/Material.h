// Synthetic, deliberately harsh test material (made here, so the tests need no audio files).
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

namespace material
{
constexpr double fs = 48000.0;
constexpr double twoPi = 6.283185307179586;

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    static Biquad peak (double f, double q, double db)
    {
        Biquad b; const double A = std::pow (10.0, db / 40.0), w = twoPi * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al / A;
        b.b0 = (1 + al * A) / a0; b.b1 = -2 * c / a0; b.b2 = (1 - al * A) / a0; b.a1 = -2 * c / a0; b.a2 = (1 - al / A) / a0; return b;
    }
    static Biquad hp (double f, double q = 0.707)
    {
        Biquad b; const double w = twoPi * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al;
        b.b0 = (1 + c) / 2 / a0; b.b1 = -(1 + c) / a0; b.b2 = (1 + c) / 2 / a0; b.a1 = -2 * c / a0; b.a2 = (1 - al) / a0; return b;
    }
    static Biquad lp (double f, double q = 0.707)
    {
        Biquad b; const double w = twoPi * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al;
        b.b0 = (1 - c) / 2 / a0; b.b1 = (1 - c) / a0; b.b2 = (1 - c) / 2 / a0; b.a1 = -2 * c / a0; b.a2 = (1 - al) / a0; return b;
    }
    static Biquad bp (double f, double q)
    {
        Biquad b; const double w = twoPi * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al;
        b.b0 = al / a0; b.b1 = 0; b.b2 = -al / a0; b.a1 = -2 * c / a0; b.a2 = (1 - al) / a0; return b;
    }
    float operator() (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
};

struct Rng { juce::uint32 s = 0x1234567; float next() { s = s * 1664525u + 1013904223u; return (float) (s >> 8) / 8388608.0f - 1.0f; } };

inline double mtof (double n) { return 440.0 * std::pow (2.0, (n - 69.0) / 12.0); }

inline void normalise (juce::AudioBuffer<float>& b, float peakDb)
{
    const float pk = b.getMagnitude (0, b.getNumSamples());
    if (pk > 0) b.applyGain (juce::Decibels::decibelsToGain (peakDb) / pk);
}

// Harsh rage supersaw lead at 160 BPM: 7 naive (aliasing) saws, driven, with ringing peaks at 620 Hz, 3.15 kHz and 5.6 kHz.
inline juce::AudioBuffer<float> rageLead (double secs, double bpm = 160.0)
{
    const int n = (int) (secs * fs);
    juce::AudioBuffer<float> b (2, n);
    const int riff[] { 71, 74, 78, 76, 74, 71, 69, 71, 71, 74, 78, 81, 79, 78, 76, 74 };
    const double eighth = 60.0 / bpm / 2.0;
    const double det[] { -22, -14, -7, 0, 7, 14, 22 };
    double ph[7] {};
    Biquad pk[2][3];
    for (int c = 0; c < 2; ++c) { pk[c][0] = Biquad::peak (620, 6, 8); pk[c][1] = Biquad::peak (3150, 9, 14); pk[c][2] = Biquad::peak (5600, 12, 10); }
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        const int step = (int) (t / eighth);
        const double inNote = t - step * eighth;
        const double f0 = mtof (riff[step % 16]);
        const double env = std::min (1.0, inNote / 0.003) * (inNote < eighth * 0.9 ? 1.0 : std::max (0.0, 1.0 - (inNote - eighth * 0.9) / 0.03));
        double l = 0, r = 0;
        for (int v = 0; v < 7; ++v)
        {
            ph[v] += f0 * std::pow (2.0, det[v] / 1200.0) / fs; ph[v] -= std::floor (ph[v]);
            const double s = 2.0 * ph[v] - 1.0;
            (v % 2 ? l : r) += s; (v % 2 ? r : l) += s * 0.5;
        }
        const double dl = std::tanh (l * 0.45) * env, dr = std::tanh (r * 0.45) * env;
        float yl = (float) dl, yr = (float) dr;
        for (int k = 0; k < 3; ++k) { yl = pk[0][k] (yl); yr = pk[1][k] (yr); }
        b.setSample (0, i, yl); b.setSample (1, i, yr);
    }
    normalise (b, -3.0f);
    return b;
}

// Bright hi-hats: 1/16 closed hats with accents, an open hat on the "and" of 4, 1/32 roll at the end of every 2nd bar.
// 808-style metal (six square waves) + noise, with ringing peaks at 8.7 kHz and 12.3 kHz.
inline juce::AudioBuffer<float> hats (double secs, double bpm = 160.0)
{
    const int n = (int) (secs * fs);
    juce::AudioBuffer<float> b (2, n);
    const double six[] { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
    double ph[6] {};
    Rng rng;
    Biquad hp1 = Biquad::hp (7000), hp2 = Biquad::hp (7000), p1 = Biquad::peak (8700, 14, 12), p2 = Biquad::peak (12300, 10, 9);
    const double sixteenth = 60.0 / bpm / 4.0, bar = sixteenth * 16;
    std::vector<std::pair<double, double>> hits;   // time, decay
    for (double t0 = 0; t0 < secs; t0 += bar)
    {
        const int barIdx = (int) std::round (t0 / bar);
        for (int s = 0; s < 16; ++s)
        {
            if (barIdx % 2 == 1 && s >= 12) { for (int k = 0; k < 2; ++k) hits.push_back ({ t0 + (s + k * 0.5) * sixteenth, 0.025 }); continue; }
            hits.push_back ({ t0 + s * sixteenth, s == 14 ? 0.22 : 0.035 });
        }
    }
    size_t hi = 0; double env = 0, dec = 0.035; float acc = 1;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        while (hi < hits.size() && hits[hi].first <= t) { env = 1.0; dec = hits[hi].second; acc = (hi % 4 == 0) ? 1.0f : 0.6f; ++hi; }
        double m = 0;
        for (int k = 0; k < 6; ++k) { ph[k] += six[k] * 1.7 / fs; ph[k] -= std::floor (ph[k]); m += ph[k] < 0.5 ? 1 : -1; }
        double x = m * 0.12 + rng.next() * 0.6;
        float y = hp2 (hp1 (x));
        y = p2 (p1 (y));
        y *= (float) env * acc;
        env *= std::exp (-1.0 / (dec * fs));
        b.setSample (0, i, y); b.setSample (1, i, y * 0.92f + 0.08f * (float) (rng.next() * env * 0.2));
    }
    normalise (b, -3.0f);
    return b;
}

// Distorted 808: pitched sine with a punch drop, driven + clipped hard, with a fizzy ring at 2.2 kHz.
inline juce::AudioBuffer<float> dist808 (double secs, double bpm = 160.0)
{
    const int n = (int) (secs * fs);
    juce::AudioBuffer<float> b (2, n);
    const double beat = 60.0 / bpm;
    struct Hit { double t, len; int note; };
    std::vector<Hit> hits;
    const double pat[][3] { { 0, 1.5, 31 }, { 1.5, 1.0, 31 }, { 2.5, 1.5, 34 }, { 4, 1.5, 29 }, { 5.5, 0.5, 29 }, { 6, 2.0, 31 } };
    for (double t0 = 0; t0 < secs; t0 += beat * 8)
        for (auto& p : pat) hits.push_back ({ t0 + p[0] * beat, p[1] * beat, (int) p[2] });
    Biquad fizz = Biquad::peak (2200, 5, 7);
    double ph = 0;
    size_t hi = 0; double start = -1, len = 1; int note = 31;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        while (hi < hits.size() && hits[hi].t <= t) { start = hits[hi].t; len = hits[hi].len; note = hits[hi].note; ++hi; }
        double y = 0;
        if (start >= 0)
        {
            const double a = t - start;
            const double f = mtof (note) * std::pow (2.0, 12.0 * std::exp (-a / 0.03) / 12.0);
            ph += f / fs; ph -= std::floor (ph);
            const double env = std::min (1.0, a / 0.002) * (a < len ? std::exp (-a / 1.2) : std::exp (-len / 1.2) * std::max (0.0, 1.0 - (a - len) / 0.02));
            const double s = std::sin (twoPi * ph) * env;
            y = juce::jlimit (-0.7, 0.7, std::tanh (s * 6.0)) / 0.7;
        }
        const float o = fizz (y);
        b.setSample (0, i, o); b.setSample (1, i, o);
    }
    normalise (b, -3.0f);
    return b;
}

// A soft, smooth pad (should need very little).
inline juce::AudioBuffer<float> softPad (double secs)
{
    const int n = (int) (secs * fs);
    juce::AudioBuffer<float> b (2, n);
    const int ch[] { 59, 62, 66, 69 };
    double ph[4] {};
    Biquad lp[2] { Biquad::lp (1400), Biquad::lp (1400) };
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        double x = 0;
        for (int k = 0; k < 4; ++k) { ph[k] += mtof (ch[k]) * (1.0 + 0.002 * k) / fs; ph[k] -= std::floor (ph[k]); x += 1.0 - 4.0 * std::abs (ph[k] - 0.5); }
        const double env = std::min (1.0, t / 0.4);
        const float y = (float) (x * 0.25 * env);
        b.setSample (0, i, lp[0] (y)); b.setSample (1, i, lp[1] (y * 0.97f));
    }
    normalise (b, -6.0f);
    return b;
}

inline juce::AudioBuffer<float> pinkNoise (double secs)
{
    const int n = (int) (secs * fs);
    juce::AudioBuffer<float> b (2, n);
    Rng r;
    for (int c = 0; c < 2; ++c)
    {
        double b0 = 0, b1 = 0, b2 = 0;
        for (int i = 0; i < n; ++i)
        {
            const double w = r.next();
            b0 = 0.99765 * b0 + w * 0.0990460; b1 = 0.96300 * b1 + w * 0.2965164; b2 = 0.57000 * b2 + w * 1.0526913;
            b.setSample (c, i, (float) ((b0 + b1 + b2 + w * 0.1848) * 0.2));
        }
    }
    normalise (b, -6.0f);
    return b;
}

// Vocal-like test: a sung "ah" (pulse + formants) with sharp "s" sounds ringing at 7.5 kHz.
inline juce::AudioBuffer<float> harshVocal (double secs, double bpm = 140.0)
{
    const int n = (int) (secs * fs);
    juce::AudioBuffer<float> b (2, n);
    Biquad f1 = Biquad::bp (750, 6), f2 = Biquad::bp (1200, 8), f3 = Biquad::bp (2700, 10), sib = Biquad::bp (7500, 5), ring = Biquad::peak (3400, 8, 9);
    Rng r;
    double ph = 0;
    const double beat = 60.0 / bpm;
    const int mel[] { 62, 64, 66, 64, 62, 59, 61, 62 };
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        const int st = (int) (t / beat);
        const double inB = t - st * beat;
        const double f = mtof (mel[st % 8] - 12) * (1.0 + 0.006 * std::sin (twoPi * 5.5 * t));
        ph += f / fs; ph -= std::floor (ph);
        const double src = ph < 0.12 ? 1.0 : -0.14;
        const double voiced = (f1 (src) * 1.0 + f2 (src) * 0.6 + f3 (src) * 0.35) * (inB > 0.08 ? 1.0 : 0.0);
        const double s = inB < 0.08 ? sib (r.next()) * 2.5 * (1.0 - inB / 0.08) : 0.0;
        const float y = ring ((float) (voiced * 1.4 + s));
        b.setSample (0, i, y); b.setSample (1, i, y);
    }
    normalise (b, -3.0f);
    return b;
}

inline juce::AudioBuffer<float> fullMix (double secs)
{
    auto a = rageLead (secs), h = hats (secs), d = dist808 (secs);
    juce::AudioBuffer<float> m (2, a.getNumSamples());
    m.clear();
    for (int c = 0; c < 2; ++c) { m.addFrom (c, 0, a, c, 0, a.getNumSamples(), 0.55f); m.addFrom (c, 0, h, c, 0, a.getNumSamples(), 0.35f); m.addFrom (c, 0, d, c, 0, a.getNumSamples(), 0.6f); }
    normalise (m, -1.0f);
    return m;
}
// Same parts, hats louder: for the Focus High test (it needs rings both below and above 5 kHz).
inline juce::AudioBuffer<float> brightMix (double secs)
{
    auto a = rageLead (secs), h = hats (secs), d = dist808 (secs);
    juce::AudioBuffer<float> m (2, a.getNumSamples());
    m.clear();
    for (int c = 0; c < 2; ++c) { m.addFrom (c, 0, a, c, 0, a.getNumSamples(), 0.45f); m.addFrom (c, 0, h, c, 0, a.getNumSamples(), 0.9f); m.addFrom (c, 0, d, c, 0, a.getNumSamples(), 0.35f); }
    normalise (m, -1.0f);
    return m;
}
} // namespace material
