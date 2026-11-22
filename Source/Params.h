#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

// Every parameter of Xoxicoa Glass: IDs, ranges, defaults and choice lists.
// The DSP, presets, UI and the knob test all read this one table.
namespace xg
{
inline const juce::StringArray modeNames { "Soft", "Hard" };
inline const juce::StringArray targetNames { "Off", "Rage", "Pluggnb" };

enum class Kind { Float, Int, Choice, Bool };

struct ParamDef
{
    const char* id;
    const char* name;
    Kind kind;
    float min, max, def;
    float skewCentre = 0.0f;   // >0: this value sits mid-knob
    const char* unit = "";      // "%" shows 0..1 as percent
    const juce::StringArray* choices = nullptr;
    bool affectsSound = true;   // false for display-only controls (tonal target)
};

// id, display name, kind, min, max, default, skew centre, unit, choices, affects sound (0 = display only)
#define XG_PARAMS(X) \
    X (depth,    "Depth",          Float, 0, 1, 0.5f, 0, "%", nullptr, 1) \
    X (sharp,    "Sharpness",      Float, 0, 1, 0.5f, 0, "%", nullptr, 1) \
    X (speed,    "Speed",          Float, 0, 1, 0.5f, 0, "%", nullptr, 1) \
    X (focus_lo, "Focus Low",      Float, 40, 12000, 1500, 1000, "Hz", nullptr, 1) \
    X (focus_hi, "Focus High",     Float, 500, 20000, 16000, 5000, "Hz", nullptr, 1) \
    X (mode,     "Mode",           Choice, 0, 1, 0, 0, "", &modeNames, 1) \
    X (delta,    "Delta",          Bool, 0, 1, 0, 0, "", nullptr, 1) \
    X (mix,      "Mix",            Float, 0, 1, 1.0f, 0, "%", nullptr, 1) \
    X (output,   "Output",         Float, -24, 12, 0, 0, "dB", nullptr, 1) \
    X (target,   "Tonal Target",   Choice, 0, 2, 1, 0, "", &targetNames, 0)

enum P : int
{
#define XG_ENUM(id, ...) id,
    XG_PARAMS (XG_ENUM)
#undef XG_ENUM
    kNumParams
};

inline const std::vector<ParamDef>& paramDefs()
{
    static const std::vector<ParamDef> defs {
#define XG_DEF(id, name, kind, mn, mx, df, sk, unit, ch, snd) { #id, name, Kind::kind, (float) (mn), (float) (mx), (float) (df), (float) (sk), unit, ch, (snd) != 0 },
        XG_PARAMS (XG_DEF)
#undef XG_DEF
    };
    return defs;
}

inline const ParamDef* findDef (const juce::String& id)
{
    for (auto& d : paramDefs())
        if (id == d.id) return &d;
    return nullptr;
}

inline juce::NormalisableRange<float> rangeFor (const ParamDef& d)
{
    if (d.kind != Kind::Float) return { d.min, d.max, 1.0f };
    juce::NormalisableRange<float> r { d.min, d.max };
    if (d.skewCentre > 0.0f) r.setSkewForCentre (d.skewCentre);
    return r;
}

inline juce::String formatValue (const ParamDef& d, float v)
{
    const juce::String u (d.unit);
    if (u == "%") return juce::String (juce::roundToInt (v * 100.0f)) + "%";
    if (u == "ms") return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (v, v < 10.0f ? 1 : 0) + " ms";
    if (u == "Hz") return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (v, v < 10.0f ? 1 : 0) + " Hz";
    if (u == "dB") return juce::String (v, 1) + " dB";
    if (u == "st") return (v > 0 ? "+" : "") + juce::String (v, 1) + " st";
    return juce::String (v, 2);
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (auto& d : paramDefs())
    {
        const juce::ParameterID pid { d.id, 1 };
        if (d.kind == Kind::Choice)
            layout.add (std::make_unique<juce::AudioParameterChoice> (pid, d.name, *d.choices, (int) d.def));
        else if (d.kind == Kind::Bool)
            layout.add (std::make_unique<juce::AudioParameterBool> (pid, d.name, d.def > 0.5f));
        else if (d.kind == Kind::Int)
            layout.add (std::make_unique<juce::AudioParameterInt> (pid, d.name, (int) d.min, (int) d.max, (int) d.def,
                juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return (v > 0 ? "+" : "") + juce::String (v) + " st"; })));
        else
        {
            const ParamDef* dp = &d;
            layout.add (std::make_unique<juce::AudioParameterFloat> (pid, d.name, rangeFor (d), d.def,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([dp] (float v, int) { return formatValue (*dp, v); })));
        }
    }
    return layout;
}

using Patch = std::map<juce::String, float>;

inline Patch initPatch()
{
    Patch p;
    for (auto& d : paramDefs()) p[d.id] = d.def;
    return p;
}
} // namespace xg
