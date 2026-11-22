// Xoxicoa Glass offline tests.
//   audit <report>      every knob audibly changes the sound (min -> mid -> max), on harsh material
//   safety <report>     unity / delta / latency checks, presets on every material (finite, no added peaks),
//                       measured ring reduction, CPU at several sample rates and block sizes
//   auto <report>       AUTO on varied material: sensible settings, stable across different 4 s windows
//   demos <dir>         BEFORE / AFTER WAVs (+ a Delta clip)
#include "PluginProcessor.h"
#include "AuditCommon.h"
#include "Material.h"

using namespace audit;
using Proc = XoxicoaGlassProcessor;
static const char* kName = "Xoxicoa Glass";

static xg::Patch basePatch()
{
    auto p = xg::initPatch();
    p["depth"] = 0.7f; p["sharp"] = 0.5f; p["speed"] = 0.5f; p["focus_lo"] = 300.0f; p["focus_hi"] = 16000.0f;
    p["mode"] = 0; p["delta"] = 0; p["mix"] = 1.0f; p["output"] = 0.0f; p["target"] = 1;
    return p;
}

static juce::AudioBuffer<float> run (const xg::Patch& patch, const juce::AudioBuffer<float>& in, double* cpu = nullptr, int block = kBlock, double fs = kFs)
{
    Proc proc;
    proc.applyPatch (patch);
    return process (proc, in, cpu, block, fs);
}

// mean power (dB) in a 1/6-octave band around f, from an averaged 8k FFT
static float bandDb (const juce::AudioBuffer<float>& b, float f, float widthOct = 1.0f / 6.0f)
{
    constexpr int order = 13, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> w ((size_t) N * 2);
    std::vector<double> pw (N / 2, 0.0);
    const int n = b.getNumSamples();
    int frames = 0;
    for (int s = 0; s + N <= n; s += N / 2, ++frames)
    {
        for (int i = 0; i < N; ++i) w[(size_t) i] = 0.5f * (b.getSample (0, s + i) + b.getSample (1, s + i)) * (0.5f - 0.5f * std::cos (6.2831853f * i / N));
        std::fill (w.begin() + N, w.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (w.data());
        for (int k = 0; k < N / 2; ++k) pw[(size_t) k] += w[(size_t) k] * (double) w[(size_t) k];
    }
    const int k0 = (int) (f * std::pow (2.0f, -widthOct * 0.5f) / kFs * N), k1 = (int) (f * std::pow (2.0f, widthOct * 0.5f) / kFs * N);
    double e = 0;
    for (int k = k0; k <= k1; ++k) e += pw[(size_t) k];
    return (float) (10.0 * std::log10 (e / juce::jmax (1, frames) / juce::jmax (1, k1 - k0 + 1) + 1e-20));
}

static int latencyAt48k() { Proc p; p.setPlayConfigDetails (2, 2, kFs, kBlock); p.prepareToPlay (kFs, kBlock); return p.getLatencySamples(); }

// ---------------- knob audibility
struct Ctx { xg::Patch patch; const juce::AudioBuffer<float>* in; };

int runAudit (const juce::File& reportFile)
{
    const auto mix = material::fullMix (5.0), hatsOnly = material::hats (5.0), bright = material::brightMix (5.0), leadOnly = material::rageLead (5.0);
    // Most controls are measured on the full harsh mix. Three are measured on the material whose job they do:
    // Speed and Sharpness on the supersaw lead (notes change every 1/8 at 160 BPM, so how fast the cut follows is audible),
    // Focus High on a brighter mix (same parts, hats louder) because it has to find rings below AND above 5 kHz.
    auto materialFor = [&] (const juce::String& id) -> const juce::AudioBuffer<float>& { return id == "speed" ? leadOnly : id == "focus_hi" ? bright : id == "sharp" ? leadOnly : mix; };
    juce::String rep;
    rep << kName << " knob audibility test (every control min -> mid -> max, both halves must change the sound by >= 1.0 dB).\n"
        << "Material: harsh test mix (driven aliasing supersaw lead with rings at 620 Hz / 3.15 kHz / 5.6 kHz, bright hi-hats with\n"
        << "rings at 8.7 / 12.3 kHz, distorted 808 with a 2.2 kHz fizz), 5 s at 160 BPM, analysed from 1.0 s.\n"
        << "Speed and Sharpness are measured on the supersaw lead alone [lead] (its notes move, so cut timing is audible), Focus High on the same mix\nwith the hats louder [brgt]; everything else on the full mix [mix].\n"
        << "Base settings: Depth 70 %, Sharpness 50 %, Speed 50 %, Focus 300 Hz - 16 kHz, Soft, Mix 100 %.\n"
        << "Columns: dRMS SPEC ENV BAND SIDE BAND3 score (dB). BAND3 = largest change in any 1/3-octave band; it counts as BAND3 / 1.5\n"
        << "in the score (so one 1/3-octave band has to move by at least 1.5 dB). Score = max (|dRMS|, SPEC, ENV, BAND, |SIDE|/2, |dPEAK|, BAND3/1.5).\n"
        << "Tonal Target is display-only and is checked by a null test in the safety report.\n\n";
    int fails = 0, total = 0;
    for (auto& d : xg::paramDefs())
    {
        if (! d.affectsSound) continue;
        ++total;
        Proc probe;
        auto* param = probe.apvts.getParameter (d.id);
        std::vector<float> values;
        if (d.kind == xg::Kind::Float) values = { d.min, param->convertFrom0to1 (0.5f), d.max };
        else for (float v = d.min; v <= d.max + 0.01f; v += 1.0f) values.push_back (v);
        std::vector<Features> feats;
        for (float v : values)
        {
            auto patch = basePatch();
            patch[d.id] = v;
            feats.push_back (analyse (slice (run (patch, materialFor (d.id)), 1.0)));
        }
        bool ok = true;
        juce::String line = juce::String (d.id).paddedRight (' ', 10) + (&materialFor (d.id) == &mix ? " [mix ] | " : &materialFor (d.id) == &hatsOnly ? " [hats] | " : &materialFor (d.id) == &bright ? " [brgt] | " : " [lead] | ");
        if (d.kind == xg::Kind::Float)
        {
            line << param->getText (param->convertTo0to1 (values[0]), 12).paddedRight (' ', 9) << "-> "
                 << param->getText (0.5f, 12).paddedRight (' ', 9) << "-> " << param->getText (1.0f, 12).paddedRight (' ', 9) << "| ";
            for (int h = 0; h < 2; ++h)
            {
                const auto dd = distance (feats[(size_t) h], feats[(size_t) h + 1]);
                line << fmt (dd.rms) << fmt (dd.spec) << fmt (dd.env) << fmt (dd.band) << fmt (dd.side) << fmt (dd.band3) << fmt (dd.score()) << " | ";
                ok = ok && dd.score() >= 1.0f;
            }
        }
        else
        {
            float minScore = 1e9f;
            for (size_t i = 1; i < feats.size(); ++i) minScore = std::min (minScore, distance (feats[i - 1], feats[i]).score());
            const auto dd = distance (feats.front(), feats.back());
            line << "each step: " << fmt (dd.rms) << fmt (dd.spec) << fmt (dd.env) << fmt (dd.band) << fmt (dd.side) << fmt (dd.band3) << fmt (dd.score()) << " | min step " << juce::String (minScore, 2) << " dB | ";
            ok = minScore >= 1.0f;
        }
        line << (ok ? "PASS" : "FLAG") << "\n";
        if (! ok) ++fails;
        rep << line;
        std::printf ("%s", line.toRawUTF8());
    }
    rep << "\n" << total << " knobs tested, " << (total - fails) << " passed, " << fails << " flagged.\n";
    std::printf ("%d knobs tested, %d passed, %d flagged.\n", total, total - fails, fails);
    reportFile.replaceWithText (rep);
    return fails == 0 ? 0 : 1;
}

// ---------------- safety, reduction and CPU
struct Mat { const char* name; juce::AudioBuffer<float> buf; std::vector<float> rings; std::vector<float> clean; };

static std::vector<Mat> materials (double secs)
{
    std::vector<Mat> m;
    m.push_back ({ "Rage supersaw lead", material::rageLead (secs), { 3150, 5600 }, { 1250, 2000 } });
    m.push_back ({ "Bright hi-hats", material::hats (secs), { 8700, 12300 }, { 10000 } });
    m.push_back ({ "Distorted 808", material::dist808 (secs), { 2200 }, { 1000, 4500 } });
    m.push_back ({ "Harsh vocal-like", material::harshVocal (secs), { 3400, 7500 }, { 1200 } });
    m.push_back ({ "Full harsh mix", material::fullMix (secs), { 3150, 8700 }, { 1250 } });
    m.push_back ({ "Soft pad (clean)", material::softPad (secs), {}, { 500 } });
    m.push_back ({ "Pink noise (flat)", material::pinkNoise (secs), {}, { 1000, 4000 } });
    return m;
}

int runSafety (const juce::File& reportFile)
{
    juce::String rep;
    int fails = 0;
    auto check = [&] (bool ok, const juce::String& what) { rep << (ok ? "PASS  " : "FAIL  ") << what << "\n"; std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8()); if (! ok) ++fails; };
    rep << kName << " safety, reduction and CPU report\n\n";

    const auto mix = material::fullMix (4.0);
    {   // 1. transparency: Depth 0 must give back the input exactly (latency-compensated)
        auto p = basePatch(); p["depth"] = 0.0f;
        auto out = run (p, mix);
        double err = 0, sig = 0;
        for (int c = 0; c < 2; ++c) for (int i = 4800; i < mix.getNumSamples() - 4800; ++i) { const double d = out.getSample (c, i) - mix.getSample (c, i); err += d * d; sig += mix.getSample (c, i) * (double) mix.getSample (c, i); }
        const double nullDb = 10.0 * std::log10 (err / sig + 1e-30);
        check (nullDb < -80.0, "Depth 0 is transparent: output - input null = " + juce::String (nullDb, 1) + " dB (latency " + juce::String (latencyAt48k()) + " samples reported and compensated)");
        p["delta"] = 1;
        auto dl = run (p, mix);
        const float dRms = analyse (slice (dl, 0.5)).rmsDb;
        check (dRms < -80.0f, "Delta at Depth 0 is silent (nothing removed): " + juce::String (dRms, 1) + " dB RMS");
    }
    {   // 2. Delta = input - output, exactly
        auto p = basePatch();
        auto wet = run (p, mix);
        p["delta"] = 1;
        auto dl = run (p, mix);
        double err = 0, sig = 0;
        for (int c = 0; c < 2; ++c) for (int i = 4800; i < mix.getNumSamples() - 4800; ++i) { const double d = (mix.getSample (c, i) - wet.getSample (c, i)) - dl.getSample (c, i); err += d * d; sig += dl.getSample (c, i) * (double) dl.getSample (c, i); }
        check (10.0 * std::log10 (err / (sig + 1e-30)) < -60.0, "Delta = input - processed (null " + juce::String (10.0 * std::log10 (err / (sig + 1e-30)), 1) + " dB); Delta level " + juce::String (analyse (slice (dl, 0.5)).rmsDb, 1) + " dB RMS vs input " + juce::String (analyse (slice (mix, 0.5)).rmsDb, 1));
    }
    {   // 3. Tonal Target is display-only
        auto p = basePatch(); p["target"] = 1; auto a = run (p, mix); p["target"] = 2; auto b = run (p, mix); p["target"] = 0; auto c0 = run (p, mix);
        double e = 0;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < mix.getNumSamples(); ++i) e = std::max ({ e, (double) std::abs (a.getSample (c, i) - b.getSample (c, i)), (double) std::abs (a.getSample (c, i) - c0.getSample (c, i)) });
        check (e == 0.0, "Tonal Target changes only the display (Off / Rage / Pluggnb outputs are bit-identical)");
    }
    {   // 4. mono works
        Proc proc;
        proc.applyPatch (basePatch());
        proc.setPlayConfigDetails (1, 1, kFs, 256);
        proc.prepareToPlay (kFs, 256);
        juce::AudioBuffer<float> m (1, 256); juce::MidiBuffer midi; bool fin = true;
        for (int b = 0; b < 400; ++b) { for (int i = 0; i < 256; ++i) m.setSample (0, i, mix.getSample (0, (b * 256 + i) % mix.getNumSamples())); proc.processBlock (m, midi); for (int i = 0; i < 256; ++i) fin = fin && std::isfinite (m.getSample (0, i)); }
        check (fin, "Mono in / mono out runs and stays finite");
    }

    // 5. presets x materials
    rep << "\nPresets on every material (6 s each). Output must be finite and never louder at the peak than the input + 1 dB (Output 0 dB).\n"
        << "RING = level change at the known ringing frequencies of that material, CLEAN = change at frequencies without rings, RMS = overall.\n\n";
    auto mats = materials (6.0);
    const auto& presets = xg::factoryPresets();
    for (auto& m : mats)
    {
        rep << m.name << "\n";
        const float inPk = analyse (m.buf).peakDb;
        for (size_t pi = 0; pi < presets.size(); ++pi)
        {
            double cpu = 0;
            auto out = run (presets[pi].values, m.buf, &cpu);
            auto fo = analyse (out), fi = analyse (m.buf);
            const auto o = slice (out, 1.0), in = slice (m.buf, 1.0);
            float ring = 0, clean = 0;
            for (float f : m.rings) ring += bandDb (o, f, 1.0f / 12.0f) - bandDb (in, f, 1.0f / 12.0f);
            for (float f : m.clean) clean += bandDb (o, f, 1.0f / 3.0f) - bandDb (in, f, 1.0f / 3.0f);
            if (! m.rings.empty()) ring /= (float) m.rings.size();
            if (! m.clean.empty()) clean /= (float) m.clean.size();
            const bool ok = fo.finite && fo.peakDb <= inPk + 1.0f;
            if (! ok) ++fails;
            juce::String line;
            line << "  " << presets[pi].name.paddedRight (' ', 22) << (ok ? "OK  " : "FAIL") << " peak " << fmt (fo.peakDb) << " (in " << fmt (inPk) << ")"
                 << "  RMS " << fmt (fo.rmsDb - fi.rmsDb) << "  RING " << (m.rings.empty() ? juce::String ("    -  ") : fmt (ring)) << "  CLEAN " << fmt (clean)
                 << "  CPU " << fmt ((float) (cpu / (m.buf.getNumSamples() / kFs) * 100.0), 6) << " %\n";
            rep << line;
        }
        rep << "\n";
    }

    // 6. CPU at several rates / block sizes (whole processor, stereo, this build machine)
    rep << "CPU (percent of one core in real time, whole plugin, stereo, Tame Rage Lead on the full harsh mix, 20 s):\n";
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (int blk : { 64, 256, 1024 })
        {
            const int n = (int) (20.0 * fs);
            juce::AudioBuffer<float> in (2, n);
            const auto src = material::fullMix (4.0);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) in.setSample (c, i, src.getSample (c, i % src.getNumSamples()));
            double cpu = 0;
            Proc proc; proc.applyPatch (presets[1].values);
            process (proc, in, &cpu, blk, fs);
            rep << "  " << juce::String (fs / 1000.0, 1) << " kHz, block " << juce::String (blk).paddedLeft (' ', 4) << ": " << juce::String (cpu / 20.0 * 100.0, 2)
                << " %   (FFT " << juce::String (proc.getLatencySamples()) << ", latency " << juce::String (proc.getLatencySamples() / fs * 1000.0, 1) << " ms)\n";
        }
    rep << "\n" << (fails == 0 ? "ALL PASS" : juce::String (fails) + " FAILURES") << "\n";
    std::printf ("%s\n", fails == 0 ? "ALL PASS" : "FAILURES");
    reportFile.replaceWithText (rep);
    return fails == 0 ? 0 : 1;
}

// ---------------- AUTO
static xg::AutoResult autoOn (const juce::AudioBuffer<float>& in, double from, int target = 1)
{
    Proc proc;
    auto p = basePatch(); p["target"] = (float) target;
    proc.applyPatch (p);
    proc.setPlayConfigDetails (2, 2, kFs, kBlock);
    proc.prepareToPlay (kFs, kBlock);
    juce::AudioBuffer<float> blk (2, kBlock); juce::MidiBuffer midi;
    const int start = (int) (from * kFs), n = in.getNumSamples();
    proc.startAuto();
    for (int pos = 0; pos < (int) (16.0 * kFs); pos += kBlock)
    {
        for (int c = 0; c < 2; ++c) for (int i = 0; i < kBlock; ++i) blk.setSample (c, i, in.getSample (c, (start + pos + i) % n));
        proc.processBlock (blk, midi);
        if (proc.applyAutoIfReady()) return proc.lastAuto();
        if (proc.autoState() == 3) break;
    }
    return {};
}

int runAuto (const juce::File& reportFile)
{
    juce::String rep;
    int fails = 0;
    rep << kName << " AUTO test. AUTO listens to 4 s of audio and sets Depth, Sharpness, Speed and Focus.\n"
        << "Each material is looped; AUTO is started at 5 different points (0, 1.3, 2.9, 4.4, 6.1 s) to check that it is stable.\n"
        << "Pass rules: settings in range, the 5 runs agree (Depth and Sharpness within +-0.10, Speed within +-0.12, Focus edges within 2/3 octave),\n"
        << "the same audio gives identical results, and the choices fit the material (rules per material below).\n\n";
    struct Rule { const char* name; juce::AudioBuffer<float> buf; std::function<juce::String (const xg::AutoResult&)> sensible; };
    std::vector<Rule> rules;
    rules.push_back ({ "Rage supersaw lead", material::rageLead (8.0), [] (const xg::AutoResult& r) -> juce::String {
        return r.depth >= 0.35f && r.lo <= 3150.0f / 1.2f && r.hi >= 5600.0f * 1.2f ? "" : "expected Depth >= 0.35 and focus covering 3.15-5.6 kHz"; } });
    rules.push_back ({ "Bright hi-hats", material::hats (8.0), [] (const xg::AutoResult& r) -> juce::String {
        return r.lo >= 2000.0f && r.hi >= 12300.0f && r.depth >= 0.3f ? "" : "expected focus starting >= 2 kHz up to >= 12.3 kHz, Depth >= 0.3"; } });
    rules.push_back ({ "Distorted 808", material::dist808 (8.0), [] (const xg::AutoResult& r) -> juce::String {
        return r.lo <= 2200.0f / 1.3f && r.hi >= 2200.0f * 1.3f ? "" : "expected focus covering the 2.2 kHz fizz"; } });
    rules.push_back ({ "Harsh vocal-like", material::harshVocal (8.0), [] (const xg::AutoResult& r) -> juce::String {
        return r.lo <= 3400.0f / 1.2f && r.hi >= 7500.0f * 1.2f && r.depth >= 0.3f ? "" : "expected focus covering 3.4-7.5 kHz, Depth >= 0.3"; } });
    rules.push_back ({ "Full harsh mix", material::fullMix (8.0), [] (const xg::AutoResult& r) -> juce::String {
        return r.depth >= 0.35f && r.lo <= 3150.0f && r.hi >= 8700.0f ? "" : "expected Depth >= 0.35, focus covering 3.15-8.7 kHz"; } });
    rules.push_back ({ "Soft pad (clean)", material::softPad (8.0), [] (const xg::AutoResult& r) -> juce::String {
        return r.depth <= 0.3f ? "" : "expected a light touch (Depth <= 0.30)"; } });
    rules.push_back ({ "Pink noise (flat)", material::pinkNoise (8.0), [] (const xg::AutoResult& r) -> juce::String {
        return r.depth <= 0.3f ? "" : "expected a light touch (Depth <= 0.30)"; } });

    std::vector<std::pair<juce::String, float>> depths;
    for (auto& ru : rules)
    {
        std::vector<xg::AutoResult> rs;
        for (double from : { 0.0, 1.3, 2.9, 4.4, 6.1 }) rs.push_back (autoOn (ru.buf, from));
        const auto again = autoOn (ru.buf, 0.0);
        bool ok = std::all_of (rs.begin(), rs.end(), [] (const xg::AutoResult& r) { return r.valid; });
        juce::String why;
        if (! ok) why = "AUTO did not finish";
        auto spread = [&rs] (auto get) { float a = 1e9f, b = -1e9f; for (auto& r : rs) { a = std::min (a, get (r)); b = std::max (b, get (r)); } return b - a; };
        const float sd = spread ([] (const xg::AutoResult& r) { return r.depth; }), ss = spread ([] (const xg::AutoResult& r) { return r.sharp; }),
                    sp = spread ([] (const xg::AutoResult& r) { return r.speed; }),
                    sl = spread ([] (const xg::AutoResult& r) { return std::log2 (r.lo); }), sh = spread ([] (const xg::AutoResult& r) { return std::log2 (r.hi); });
        if (ok && (sd > 0.2f || ss > 0.2f || sp > 0.24f || sl > 1.34f || sh > 1.34f)) { ok = false; why = "unstable across windows"; }
        if (ok && (again.depth != rs[0].depth || again.lo != rs[0].lo || again.hi != rs[0].hi || again.sharp != rs[0].sharp)) { ok = false; why = "not repeatable"; }
        if (ok) { const auto s = ru.sensible (rs[0]); if (s.isNotEmpty()) { ok = false; why = s; } }
        if (! ok) ++fails;
        depths.push_back ({ ru.name, rs[0].depth });
        juce::String line;
        line << juce::String (ru.name).paddedRight (' ', 20) << (ok ? "PASS" : "FAIL") << "  Depth " << juce::String (rs[0].depth, 2) << "  Sharp " << juce::String (rs[0].sharp, 2)
             << "  Speed " << juce::String (rs[0].speed, 2) << "  Focus " << juce::String ((int) rs[0].lo) << " - " << juce::String ((int) rs[0].hi) << " Hz"
             << "  | harshness " << juce::String (rs[0].harshIndex, 1) << ", narrow share " << juce::String (rs[0].narrowShare, 2)
             << " | spread over 5 windows: Depth " << juce::String (sd, 2) << ", Sharp " << juce::String (ss, 2) << ", Speed " << juce::String (sp, 2)
             << ", Low " << juce::String (sl, 2) << " oct, High " << juce::String (sh, 2) << " oct" << (why.isNotEmpty() ? "  <- " + why : juce::String()) << "\n";
        rep << line;
        std::printf ("%s", line.toRawUTF8());
    }
    // harsh material must get more Depth than clean material
    float harshMin = 1, cleanMax = 0;
    for (auto& [n, d] : depths) { if (n.contains ("clean") || n.contains ("flat")) cleanMax = std::max (cleanMax, d); else harshMin = std::min (harshMin, d); }
    const bool order = harshMin > cleanMax;
    if (! order) ++fails;
    rep << "\n" << (order ? "PASS" : "FAIL") << "  every harsh material gets more Depth (min " << juce::String (harshMin, 2) << ") than the clean ones (max " << juce::String (cleanMax, 2) << ")\n";
    {   // silence: AUTO must give up cleanly
        juce::AudioBuffer<float> silent (2, (int) (4 * kFs)); silent.clear();
        const auto r = autoOn (silent, 0.0);
        const bool ok = ! r.valid;
        if (! ok) ++fails;
        rep << (ok ? "PASS" : "FAIL") << "  on silence AUTO reports 'no audio' and changes nothing\n";
    }
    rep << "\n" << (fails == 0 ? "ALL PASS" : juce::String (fails) + " FAILURES") << "\n";
    std::printf ("%s\n", fails == 0 ? "AUTO ALL PASS" : "AUTO FAILURES");
    reportFile.replaceWithText (rep);
    return fails == 0 ? 0 : 1;
}

// ---------------- demos
int runDemos (const juce::File& dir)
{
    dir.createDirectory();
    struct D { const char* name; juce::AudioBuffer<float> buf; int preset; };
    const double secs = 12.0;   // 8 bars at 160 BPM
    std::vector<D> list;
    list.push_back ({ "Rage_Supersaw_Lead", material::rageLead (secs), 1 });
    list.push_back ({ "Bright_HiHats", material::hats (secs), 2 });
    list.push_back ({ "Distorted_808", material::dist808 (secs), 3 });
    for (auto& d : list)
    {
        const auto& pr = xg::factoryPresets()[(size_t) d.preset];
        auto after = run (pr.values, d.buf);
        writeWav (dir.getChildFile (juce::String ("Glass_") + d.name + "_BEFORE.wav"), d.buf);
        writeWav (dir.getChildFile (juce::String ("Glass_") + d.name + "_AFTER.wav"), after);
        std::printf ("%-20s preset %-18s before %.1f dB RMS, after %.1f dB RMS\n", d.name, pr.name.toRawUTF8(), analyse (d.buf).rmsDb, analyse (after).rmsDb);
    }
    {   // Delta: what Tame Rage Lead removes from the lead (raised 6 dB so it is easy to hear; peak stays under 0 dBFS)
        auto p = xg::factoryPresets()[1].values;
        p["delta"] = 1; p["output"] = 6.0f;
        auto dl = run (p, list[0].buf);
        writeWav (dir.getChildFile ("Glass_Rage_Supersaw_Lead_DELTA_plus6dB.wav"), dl);
        std::printf ("delta clip peak %.1f dBFS\n", analyse (dl).peakDb);
    }
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String mode = argc > 1 ? argv[1] : "audit";
    const juce::String arg = argc > 2 ? argv[2] : "";
    if (mode == "audit") return runAudit (juce::File (arg.isEmpty() ? "knob_audit.txt" : arg));
    if (mode == "safety") return runSafety (juce::File (arg.isEmpty() ? "safety.txt" : arg));
    if (mode == "auto") return runAuto (juce::File (arg.isEmpty() ? "auto.txt" : arg));
    if (mode == "demos") return runDemos (juce::File (arg.isEmpty() ? "demos" : arg));
    if (mode == "probe")   // debug: probe <materialIndex> <presetIndex> [depth]
    {
        auto mats = materials (6.0);
        auto& m = mats[(size_t) arg.getIntValue()];
        auto patch = xg::factoryPresets()[(size_t) (argc > 3 ? std::atoi (argv[3]) : 0)].values;
        if (argc > 4) patch["depth"] = (float) std::atof (argv[4]);
        auto out = slice (run (patch, m.buf), 1.0); auto in = slice (m.buf, 1.0);
        for (float f = 250; f < 18000; f *= 1.2599f) std::printf ("%7.0f Hz  in %6.1f  out %6.1f  diff %5.1f\n", f, bandDb (in, f, 1.0f / 3.0f), bandDb (out, f, 1.0f / 3.0f), bandDb (out, f, 1.0f / 3.0f) - bandDb (in, f, 1.0f / 3.0f));
        return 0;
    }
    std::printf ("usage: audit|safety|auto|demos <path>\n");
    return 2;
}
