# Master Forge

A mastering VST3 plugin built with [JUCE](https://juce.com) and C++. Master
Forge is a single-window mastering chain: tone shaping with a live analyzer,
3-band multiband compression, harmonic saturation, stereo-width control and a
true-peak-safe brickwall limiter, with BS.1770 LUFS metering and a preset
system so you can hit a loudness target with confidence.

```
input gain → 4-band EQ → 3-band multiband comp → saturation → stereo width → output gain → brickwall limiter (true-peak)
                                                                                                  │
                                                              analyzer · LUFS · peak · per-band gain-reduction metering
```

## Features

| Module | What it does |
| --- | --- |
| **Input / Output gain** | Trim level into the chain and drive into the limiter (±24 dB). |
| **4-band EQ + analyzer** | Low shelf, two parametric bells (freq/gain/Q) and a high shelf, drawn over a real-time FFT spectrum. Drag the band handles on the graph; mouse-wheel a bell handle to change its Q. |
| **3-band multiband compressor** | Linkwitz-Riley crossovers split the signal into low/mid/high bands, each with its own threshold, ratio and makeup (attack/release/knee are shared). Phase-compensated so the bands sum back flat. |
| **Saturation** | Smooth `tanh` drive with a dry/wet mix for harmonic warmth. |
| **Stereo width** | Mid/Side width from mono (0%) to wide (200%). |
| **Brickwall limiter** | 5 ms lookahead, sliding-window peak detection and a ceiling-clamped safety net — the output never exceeds the ceiling. An optional **True Peak** mode limits on a 4× linear-phase oversampled signal to catch inter-sample peaks. Reports latency to the host. |
| **Metering** | Momentary / short-term / gated-integrated **LUFS** (ITU-R BS.1770), output peak meters and per-band + limiter gain-reduction bars. |
| **Presets** | Built-in factory presets, save/load of user presets to disk, and an **A/B** compare pair with copy-across. |

The output gain sits *before* the limiter so it acts as the limiter drive,
while the limiter **Ceiling** remains the true final peak.

## Building

You need **CMake ≥ 3.22** and a C++17 compiler. JUCE is downloaded
automatically by CMake (no manual setup), so the only other requirement is the
usual platform audio/GUI toolchain.

### Linux dependencies

```bash
sudo apt-get install build-essential cmake pkg-config \
    libasound2-dev libjack-jackd2-dev \
    libfreetype6-dev libx11-dev libxcomposite-dev libxcursor-dev \
    libxext-dev libxinerama-dev libxrandr-dev libxrender-dev \
    libwebkit2gtk-4.1-dev libglu1-mesa-dev
```

### Configure & build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

The built VST3 lands in
`build/MasterForge_artefacts/Release/VST3/Master Forge.vst3` and (because
`COPY_PLUGIN_AFTER_BUILD` is on) is also copied to your user VST3 folder:

- **Linux** — `~/.vst3`
- **macOS** — `~/Library/Audio/Plug-Ins/VST3`
- **Windows** — `%COMMONPROGRAMFILES%\VST3`

### Handy CMake options

| Option | Default | Effect |
| --- | --- | --- |
| `-DMASTERFORGE_BUILD_STANDALONE=ON` | `OFF` | Also build a standalone app for testing without a DAW. |
| `-DMASTERFORGE_BUILD_TESTS=ON` | `OFF` | Build the no-host DSP + integration test apps. |
| `-DJUCE_GIT_TAG=8.0.4` | `7.0.12` | Fetch a different JUCE release. |
| `-DJUCE_SOURCE_DIR=/path/to/JUCE` | _(empty)_ | Use a local JUCE checkout instead of downloading. |

> AU (macOS) and AAX (Pro Tools) are intentionally not built yet — only VST3.
> They can be added later by extending `FORMATS` in `CMakeLists.txt`.

### Tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DMASTERFORGE_BUILD_TESTS=ON
cmake --build build -j
./build/MasterForgeTests_artefacts/Release/MasterForgeTests          # per-module DSP checks
./build/MasterForgeIntegration_artefacts/Release/MasterForgeIntegration  # full processor + editor
```

## Project layout

```
CMakeLists.txt              # build, fetches JUCE
source/
  Parameters.h              # parameter IDs + APVTS layout
  PresetManager.{h,cpp}     # factory presets, user save/load, A/B compare
  PluginProcessor.{h,cpp}   # audio engine, chain wiring, state, metering taps
  PluginEditor.{h,cpp}      # UI: analyzer, knob sections, meters, preset bar
  dsp/
    ParametricEQ.h          # 4-band shelving/bell EQ
    Compressor.h            # soft-knee, stereo-linked compressor (one band)
    MultibandCompressor.h   # 3-band Linkwitz-Riley split + per-band compressors
    Saturation.h            # tanh saturator with dry/wet
    StereoWidth.h           # M/S width
    Limiter.h               # lookahead brickwall limiter
    LoudnessMeter.h         # BS.1770 momentary/short/integrated LUFS
    SpectrumAnalyzer.h      # lock-free FFT analyzer
  gui/
    ForgeLookAndFeel.{h,cpp} # dark "molten metal" theme + rotary knobs
tests/
  dsp_test.cpp              # per-module DSP smoke tests
  integration_test.cpp      # end-to-end processor + editor test
```

## Status

Version 0.2.0 — multiband compression, analyzer + draggable EQ curve, true-peak
limiting and a preset system with A/B compare. Possible next steps: an EQ
frequency-readout while dragging, spectrum grab/freeze, M/S EQ mode, AU/AAX
targets and an expanded factory preset library.
