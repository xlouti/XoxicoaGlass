# Xoxicoa Glass

Dynamic de-harsh (resonance suppression) and tonal-balance display, with a frosted-glass UI. VST3 / AU / Standalone. JUCE 8.0.4, CMake.

See FEATURE_LIST.md for what it does in plain words.

## Build (macOS, same as the other Xoxicoa plugins)

```
cmake -S . -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 -DXG_JUCE_DIR=/path/to/JUCE
cmake --build build --config Release
```

CI: .github/workflows/build-mac.yml builds a universal binary on macos-15-intel, checks the weak-import of os_workgroup for Catalina, runs `auval -v aufx XxGl Xoxi`, and on a pushed `v*` tag publishes XoxicoaGlass_mac_vst3_au.zip and XoxicoaGlass_mac_universal.zip.

## Tests (Linux or macOS)

Configure with `-DXG_BUILD_TESTS=ON`, then build `XoxicoaGlassAudit` and `XoxicoaGlassUISnapshot`.

- `XoxicoaGlassAudit audit tests/results/knob_audit.txt`: every control must audibly change harsh test material (0 flagged).
- `XoxicoaGlassAudit safety tests/results/safety.txt`: null tests (Depth 0 / Delta / target display-only / mono), all presets on 7 materials, peak safety, CPU.
- `XoxicoaGlassAudit auto tests/results/auto_test.txt`: AUTO on 7 materials from 5 start points: stable, repeatable, sensible.
- `XoxicoaGlassAudit demos <dir>`: BEFORE/AFTER/DELTA WAVs.
- `XoxicoaGlassUISnapshot out.png [scale] [preset]`: renders the real editor to PNG.

## Engine

STFT, Hann/Hann, 75 % overlap, FFT 1024 (<= 50 kHz), 2048 (<= 100 kHz), 4096 above. Latency = FFT size. Per frame: linked-stereo level -> light frequency smoothing -> "tops" envelope (rides over ordinary harmonics) -> robust baseline -> excess -> threshold/knee/ratio/cap (Mode, Depth) -> focus window -> Sharpness spread -> per-bin attack/release (Speed) -> gain.
