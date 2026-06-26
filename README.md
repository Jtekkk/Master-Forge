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
| **4-band EQ + analyzer** | Low shelf, two parametric bells (freq/gain/Q) and a high shelf, drawn over a real-time FFT spectrum. Drag the colour-coded band handles (with a live freq/gain/Q readout); mouse-wheel a bell handle to change its Q. **Stereo / Mid / Side** processing mode and a **freeze** button for the analyzer. |
| **3-band multiband compressor** | Linkwitz-Riley crossovers split the signal into low/mid/high bands, each with its own threshold, ratio and makeup (attack/release/knee are shared). Phase-compensated so the bands sum back flat. |
| **Saturation** | Smooth `tanh` drive with a dry/wet mix for harmonic warmth, **4x oversampled** so it stays clean (aliasing ~−78 dB vs ~−15 dB un-oversampled). |
| **Stereo width** | Mid/Side width from mono (0%) to wide (200%). |
| **Brickwall limiter** | 5 ms lookahead, sliding-window peak detection and a ceiling-clamped safety net — the output never exceeds the ceiling. An optional **True Peak** mode limits on a 4× linear-phase oversampled signal to catch inter-sample peaks. Reports latency to the host. |
| **Metering** | Momentary / short-term / gated-integrated **LUFS** (ITU-R BS.1770), output peak meters with peak-hold, per-band + limiter gain-reduction bars and a **stereo correlation** meter. |
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
| `-DMASTERFORGE_BUILD_TESTS=ON` | `OFF` | Build the no-host DSP + integration test apps and the UI screenshot tool. |
| `-DMASTERFORGE_AAX_SDK_PATH=/path/to/aax-sdk` | _(empty)_ | Enable the AAX (Pro Tools) build using the Avid AAX SDK. |
| `-DJUCE_GIT_TAG=8.0.4` | `7.0.12` | Fetch a different JUCE release. |
| `-DJUCE_SOURCE_DIR=/path/to/JUCE` | _(empty)_ | Use a local JUCE checkout instead of downloading. |

> **VST3** builds everywhere. **AU** is added automatically on macOS. **AAX**
> (Pro Tools) builds when you point `MASTERFORGE_AAX_SDK_PATH` at the Avid AAX
> SDK (Avid developer account + signing required for distribution).

### Tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DMASTERFORGE_BUILD_TESTS=ON
cmake --build build -j
./build/MasterForgeTests_artefacts/Release/MasterForgeTests          # per-module DSP checks
./build/MasterForgeIntegration_artefacts/Release/MasterForgeIntegration  # full processor + editor
```

### Windows installer

A signed-or-unsigned `.exe` installer (Inno Setup) drops the VST3 into the
shared system VST3 folder. The simplest way is the **Windows Installer** GitHub
Actions workflow — run it from the Actions tab (or push a `v*` tag) and download
the installer artifact, no Windows machine required. To build it locally on
Windows, see [`packaging/windows/README.md`](packaging/windows/README.md).

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
tools/
  render_ui.cpp             # offline UI screenshot renderer (no display needed)
```

## Status

Version 0.3.0 — full "awesome UI" pass on top of the v0.2 feature set:
forge/ember theme with glowing knobs and a gradient analyzer, draggable EQ
handles with a live readout, Stereo/Mid/Side EQ mode, analyzer freeze, a stereo
correlation meter, and AU/AAX build targets alongside VST3. Possible next steps:
resizable/scalable UI, M/S metering, oversampled saturation and an expanded
factory preset library.
