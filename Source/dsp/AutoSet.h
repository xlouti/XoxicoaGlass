#pragma once
#include "DeHarsh.h"
#include "TonalBalance.h"

// AUTO: after listening for a few seconds, choose Depth, Sharpness, Speed and the Focus range for the material.
// Deterministic: the same audio always gives the same settings.
namespace xg
{
struct AutoResult { bool valid = false; float depth = 0.5f, sharp = 0.5f, speed = 0.5f, lo = 1500.0f, hi = 16000.0f; float harshIndex = 0, narrowShare = 0, highDev = 0; };

inline float niceHz (float hz)   // round to 2 significant digits so the knobs show tidy values
{
    const float p = std::pow (10.0f, std::floor (std::log10 (hz)) - 1.0f);
    return std::round (hz / p) * p;
}

inline AutoResult decideAuto (const AutoStats& st, float binHz, const std::array<float, kBands>* tonal, int target)
{
    AutoResult r;
    if (st.frames < 20 || st.narrow.empty()) return r;
    const int bins = (int) st.narrow.size();
    // harshness per octave (so wide high bands do not out-vote the low end just by having more bins)
    std::vector<double> m ((size_t) bins, 0.0);
    double total = 0, nar = 0;
    for (int k = 1; k < bins; ++k)
    {
        const double f = k * (double) binHz;
        const double perOct = binHz / (f * std::log (2.0));
        const double nk = st.narrow[(size_t) k] / st.frames * perOct, bk = st.broad[(size_t) k] / st.frames * perOct;
        m[(size_t) k] = nk + bk; total += nk + bk; nar += nk;
    }
    // worst 1/3 octave: a ring is concentrated, sparse tonal partials are spread out
    double worst = 0; float worstHz = 0;
    {
        std::vector<double> pre ((size_t) bins + 1, 0.0);
        for (int k = 1; k < bins; ++k) pre[(size_t) k + 1] = pre[(size_t) k] + m[(size_t) k];
        for (int k = 1; k < bins; ++k)
        {
            const int a = juce::jmax (1, (int) std::floor (k / 1.1225)), b = juce::jmin (bins - 1, (int) std::ceil (k * 1.1225));
            const double v = pre[(size_t) b + 1] - pre[(size_t) a];
            if (v > worst) { worst = v; worstHz = k * binHz; }
        }
    }
    juce::ignoreUnused (worstHz);
    // harshness index: how much sticks out overall + extra weight when it is concentrated (a ring)
    r.harshIndex = (float) (total + 2.0 * worst);
    r.narrowShare = total > 1e-9 ? (float) (nar / total) : 0.5f;
    r.valid = true;
    if (total < 0.02)   // nothing sticks out: clean material, gentle default
    {
        r.depth = 0.15f; r.sharp = 0.5f; r.speed = 0.5f; r.lo = 2000.0f; r.hi = 16000.0f;
        return r;
    }
    // focus = from the lowest to the highest 1/3 octave that holds at least 4 % of the worst one, widened by half an octave
    float p10 = 0, p95 = 0;
    {
        std::vector<double> pre ((size_t) bins + 1, 0.0);
        for (int k = 1; k < bins; ++k) pre[(size_t) k + 1] = pre[(size_t) k] + m[(size_t) k];
        for (double fc = 150.0; fc < 19000.0; fc *= 1.0595)   // 1/3-octave windows stepped by 1/12 octave
        {
            const int a = juce::jmax (1, (int) std::floor (fc / 1.1225 / binHz)), b = juce::jmin (bins - 1, (int) std::ceil (fc * 1.1225 / binHz));
            if (pre[(size_t) b + 1] - pre[(size_t) a] >= 0.04 * worst) { if (p10 == 0) p10 = (float) fc; p95 = (float) fc; }
        }
    }
    if (p10 == 0) { p10 = 2000.0f; p95 = 12000.0f; }
    float lo = juce::jlimit (300.0f, 8000.0f, p10 * 0.7071f);   // never below 300 Hz: keeps AUTO off 808 and bass fundamentals
    float hi = juce::jlimit (2000.0f, 20000.0f, p95 * 1.4142f);
    if (hi < lo * 4.0f) hi = juce::jmin (20000.0f, lo * 4.0f);
    if (hi < lo * 4.0f) lo = hi / 4.0f;
    r.lo = niceHz (lo); r.hi = niceHz (hi);
    r.sharp = juce::jlimit (0.1f, 0.95f, 0.1f + 0.9f * r.narrowShare);
    r.depth = juce::jlimit (0.1f, 0.8f, 0.1f + 0.22f * std::sqrt (r.harshIndex));
    const float flux = (float) (st.flux / st.frames);
    r.speed = juce::jlimit (0.3f, 0.85f, 0.35f + 0.12f * flux);
    if (tonal != nullptr && target != 0)
    {
        const auto dev = TonalBalance::deviation (*tonal, target);
        r.highDev = TonalBalance::rangeDev (dev, 2500.0f, 10000.0f);
        // highs above the (approximate) target: a little more Depth, but only if something actually rings
        if (r.highDev > 1.5f) r.depth = juce::jmin (0.8f, r.depth + juce::jmin (0.1f, 0.03f * (r.highDev - 1.5f)) * juce::jmin (1.0f, r.harshIndex));
    }
    r.depth = std::round (r.depth * 100.0f) / 100.0f;
    r.sharp = std::round (r.sharp * 100.0f) / 100.0f;
    r.speed = std::round (r.speed * 100.0f) / 100.0f;
    return r;
}
} // namespace xg
