#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Build a Windows VST3 installer entirely on Linux, no Windows machine needed:
# cross-compile with MinGW-w64, then package with NSIS.
#
#   sudo apt-get install g++-mingw-w64-x86-64 gcc-mingw-w64-x86-64 nsis cmake ninja-build
#   packaging/windows/build-installer-linux.sh [version]
#
# This is a convenience build. The official Windows binary is the MSVC build
# produced by the GitHub Actions "Windows Installer" workflow.
# ---------------------------------------------------------------------------
set -euo pipefail

VERSION="${1:-0.3.0}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build-win"
TOOLCHAIN="$ROOT/packaging/windows/mingw-w64-x86_64.toolchain.cmake"

echo ">> Configuring (MinGW-w64 cross toolchain)"
cmake -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DMASTERFORGE_COPY_AFTER_BUILD=OFF

echo ">> Building Windows VST3"
cmake --build "$BUILD" --target MasterForge_VST3 -j"$(nproc)"

BUNDLE_PARENT="$BUILD/MasterForge_artefacts/Release/VST3"
OUT="$ROOT/MasterForge-${VERSION}-Windows-x64-setup.exe"

echo ">> Packaging installer with NSIS"
makensis -V2 \
    -DVERSION="$VERSION" \
    -DBUNDLE_PARENT="$BUNDLE_PARENT" \
    -DOUTFILE="$OUT" \
    "$ROOT/packaging/windows/master-forge.nsi"

echo ">> Done: $OUT"
