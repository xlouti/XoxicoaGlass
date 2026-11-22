# Xoxicoa Glass 1.0.0: what it does

Glass is a mixing effect that removes harshness. It looks at the sound many times a second, finds the frequencies that ring or poke out sharply above their neighbours (a screechy partial in a supersaw, a metallic ring in a hi-hat, the fizz on a distorted 808, a sharp "s" or nasal ring in a vocal), and turns down only those frequencies, only while they ring. Everything else passes through untouched. Think of a very fast, automatic, hundreds-of-bands EQ that only cuts what is harsh.

It also shows how your sound's overall tone compares to an approximate target curve for Rage or Pluggnb styles, so you can see at a glance whether your highs, mids or lows are heavier or lighter than usual for that style.

## Controls

| Control | What it does |
|---|---|
| **AUTO** | Press it while your track plays. Glass listens for 4 seconds, then sets Depth, Sharpness, Speed and the Focus range for you. It shows "Listening..." with a progress bar, then the result. Press again to cancel. If it hears nothing for 15 seconds it gives up and changes nothing. |
| **Depth** | How hard harsh frequencies get turned down. 0 = nothing happens (the output is bit-for-bit your input, just delayed). |
| **Sharpness** | Low = smooth, wide cuts (tames a whole harsh region). High = needle-thin cuts on single ringing tones, leaving the rest alone. |
| **Speed** | Slow = the cut follows the average (smooth, leaves the attack of hits alone). Fast = the cut grabs each ringing note or hit and lets go right after. |
| **Focus Low / Focus High** | The frequency range Glass is allowed to work on. Drag the LOW and HIGH handles in the spectrum or use the knobs. Double-click a handle to reset it. |
| **Soft / Hard** | Soft = gentle threshold and knee. Hard = reacts to smaller peaks and cuts them firmer. |
| **Delta (Listen)** | Plays only what Glass is removing, so you can check it is taking out harshness and not the good stuff. |
| **Mix** | Blend of the processed and the original sound. |
| **Output** | Output level, -24 to +12 dB. |
| **Tonal Target** | Off / Rage / Pluggnb. Display only: it never changes the sound. |
| **Presets** | Init, Tame Rage Lead, Smooth Hats, 808 Grit Control, Vocal De-harsh, Screech Softener, Alias Cleaner, Supersaw Wall Polish, Pluggnb Keys Glow, Bright Pluck Tamer, Master Bus Gentle. |

## The display

- Frosted white fill = what comes in. Cyan line = what goes out.
- Pale "icicles" hanging from the top = how much each frequency is being turned down right now.
- The area outside the Focus range is darker.
- REDUCTION = the biggest cut right now. LATENCY = the delay Glass adds (the host compensates for it).
- The strip at the bottom shows, in 1/3-octave bars, how far your sound is from the chosen target curve (+-8 dB scale). Cyan bars are within +-3 dB, pink bars are outside. The text sums it up for LOW / LOW-MID / MID / HIGH / AIR.

## About the target curves

The Rage and Pluggnb curves are **approximate**. They were drawn from general mixing knowledge about how these styles usually sit (heavy sub and 808, forward bright leads for Rage; softer, warmer top end for Pluggnb). They are **not** measured from, and do not claim to come from, any artist, song or producer. Use them as a rough guide, not a rule. The tonal measurement is smoothed over about 3 seconds and aligned to your track's level between 100 Hz and 5 kHz.

## How Glass is different from the other Xoxicoa plugins

| Plugin | What it is | Overlap with Glass |
|---|---|---|
| Xoxicoa FX | Creative multi-effect (Ghost Double, Plugg Haze, Rage Stutter, 808 Phone-Proof, Mosh Meter, Sidechain Pump, TikTok Check) | None: creative effects, no harshness removal, no tonal-balance view |
| Xoxicoa Synth / Air / Rage | Synthesizers (they make sound) | None: Glass processes sound, it does not make it |
| Xoxicoa Sampler | Performance sampler (BPM/key detection, pads, key shift) | None |
| Xoxicoa Shaper | Multi-lane curve shaper (Volume, Filter, Pan, Drive, Time, Width) | None: Shaper moves things in time to a drawn curve; Glass reacts to the sound itself |
| Xoxicoa Cube | 3D cube that morphs six creative effects (incl. Spectral Freeze) | None: Cube's spectral face is a creative freeze, not a cleanup tool |

None of the other plugins finds and turns down ringing frequencies on its own, and none compares your mix against a style target. Glass is the first "cleanup / mix-check" tool in the set: put it after Rage, Shaper or a bright sample to keep the energy and lose the ear fatigue.

## Numbers

- Latency: 1024 samples (21.3 ms at 48 kHz, 23.2 ms at 44.1 kHz; 2048 samples at 88.2/96 kHz = 21.3 ms). Reported to the host, so it is compensated automatically in a DAW.
- CPU (whole plugin, stereo, on the Linux build machine): about 1.7-1.9 % of one core at 44.1/48 kHz, about 5 % at 96 kHz, any block size. A 2015 MacBook Air i5 is roughly 2-3x slower per core, so expect about 4-6 % per instance at 48 kHz (an estimate; not measured on that Mac).
- UI: 900 x 560. Blur is pre-rendered once; the spectrum redraws 30 times a second without blur work (full-window repaint about 2.5 ms on the build machine).
- Formats: VST3, AU, Standalone. macOS 10.15 Catalina or newer, universal (Intel + Apple Silicon).

## Good to know

- Glass works best from about 150 Hz up. If you drag Focus Low very low (below ~300 Hz), the fundamentals of 808s and bass notes can look like "peaks" and get turned down. Glass raises its threshold under about 1.2 kHz to protect them, but keep Focus Low above the bass for most uses. AUTO never goes below 300 Hz on its own.
- Like every look-ahead spectral processor, it adds a small delay; for live monitoring through it, this is noticeable.
