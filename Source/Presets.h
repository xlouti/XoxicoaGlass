#pragma once
#include "Params.h"

// Factory presets. Names describe the job only; none is made by or endorsed by any artist.
namespace xg
{
struct Preset { juce::String name; Patch values; };

inline const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> list = []
    {
        // name, depth, sharp, speed, focus_lo, focus_hi, mode, mix, output, target
        struct Row { const char* name; float depth, sharp, speed, lo, hi; int mode; float mix, out; int target; };
        const Row rows[] {
            { "Init",                 0.50f, 0.50f, 0.50f, 1500, 16000, 0, 1.00f, 0.0f, 1 },
            { "Tame Rage Lead",       0.55f, 0.55f, 0.60f, 1500, 12000, 0, 1.00f, 0.0f, 1 },
            { "Smooth Hats",          0.50f, 0.35f, 0.80f, 5000, 18000, 0, 1.00f, 0.0f, 1 },
            { "808 Grit Control",     0.50f, 0.45f, 0.55f,  700,  6000, 0, 1.00f, 0.0f, 1 },
            { "Vocal De-harsh",       0.40f, 0.60f, 0.65f, 2000, 10000, 0, 1.00f, 0.0f, 2 },
            { "Screech Softener",     0.70f, 0.75f, 0.70f, 1800,  9000, 1, 1.00f, 0.0f, 1 },
            { "Alias Cleaner",        0.65f, 0.90f, 0.50f, 3000, 20000, 1, 1.00f, 0.0f, 1 },
            { "Supersaw Wall Polish", 0.45f, 0.30f, 0.45f, 1200, 16000, 0, 1.00f, 0.0f, 1 },
            { "Pluggnb Keys Glow",    0.30f, 0.50f, 0.40f, 1000,  8000, 0, 0.80f, 0.0f, 2 },
            { "Bright Pluck Tamer",   0.50f, 0.60f, 0.85f, 2500, 14000, 0, 1.00f, 0.0f, 2 },
            { "Master Bus Gentle",    0.25f, 0.45f, 0.40f, 2000, 16000, 0, 0.70f, 0.0f, 1 },
        };
        std::vector<Preset> v;
        for (auto& r : rows)
        {
            Patch p = initPatch();
            p["depth"] = r.depth; p["sharp"] = r.sharp; p["speed"] = r.speed; p["focus_lo"] = r.lo; p["focus_hi"] = r.hi;
            p["mode"] = (float) r.mode; p["delta"] = 0; p["mix"] = r.mix; p["output"] = r.out; p["target"] = (float) r.target;
            v.push_back ({ r.name, p });
        }
        return v;
    }();
    return list;
}
} // namespace xg
