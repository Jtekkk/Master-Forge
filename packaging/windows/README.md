# Windows installer

This folder packages the Master Forge **VST3** into a Windows installer using
[Inno Setup](https://jrsoftware.org/isinfo.php) (free). The installer copies the
plugin into the shared system VST3 folder
(`C:\Program Files\Common Files\VST3`) that every DAW scans.

## Easiest: build it in CI

The [`Windows Installer`](../../.github/workflows/windows-installer.yml) GitHub
Actions workflow builds the Windows VST3 and compiles the installer on a Windows
runner — no Windows machine needed.

- **Actions tab → Windows Installer → Run workflow** (set a version), then
  download the `MasterForge-<version>-Windows-x64` artifact, **or**
- push a tag like `v0.3.0`; the installer is built and attached to the release.

## Build it on Linux (no Windows machine)

The plugin can be cross-compiled for Windows with MinGW-w64 and packaged with
**NSIS** (which, unlike Inno Setup, runs on Linux). This produces a real,
self-contained installer `.exe` — handy when you don't have a Windows box.

```bash
sudo apt-get install g++-mingw-w64-x86-64 gcc-mingw-w64-x86-64 nsis cmake ninja-build
packaging/windows/build-installer-linux.sh 0.3.0
```

This configures with [`mingw-w64-x86_64.toolchain.cmake`](mingw-w64-x86_64.toolchain.cmake)
(static runtime, so no MinGW DLLs are needed), builds the VST3, and runs
`makensis` on [`master-forge.nsi`](master-forge.nsi) to produce
`MasterForge-<version>-Windows-x64-setup.exe` in the repo root.

> This is a **MinGW** build for convenience. It loads in the vast majority of
> Windows hosts, but the *official* build is the MSVC one from CI below.

## Build it locally on Windows

Prerequisites: **Visual Studio 2022** (Desktop C++), **CMake ≥ 3.22**, and
**Inno Setup 6.3+** (its `iscc` compiler on `PATH`).

```bat
:: 1. Build the VST3 (Release, x64). JUCE is fetched automatically.
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target MasterForge_VST3

:: 2. Compile the installer.
cd packaging\windows
iscc /DMyAppVersion=0.3.0 ^
     "/DMyVST3Dir=..\..\build\MasterForge_artefacts\Release\VST3\Master Forge.vst3" ^
     master-forge.iss
```

The installer lands in `packaging\windows\Output\MasterForge-0.3.0-Windows-x64.exe`.

## Notes

- **Admin rights** are required to install (the VST3 folder is under
  `Program Files`). The installer requests elevation automatically.
- **Code signing** is not included. Unsigned installers trigger a SmartScreen
  warning ("Windows protected your PC" → *More info* → *Run anyway*). To sign,
  add a `signtool` step in CI with your certificate, or set Inno Setup's
  `SignTool` directive.
- To also ship the **standalone app**, configure with
  `-DMASTERFORGE_BUILD_STANDALONE=ON` and add its `.exe` to the `[Files]`
  section of `master-forge.iss`.
