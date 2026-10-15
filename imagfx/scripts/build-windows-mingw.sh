#!/usr/bin/env bash
# Cross-compile the IMAG plugin DLLs for Windows x64 from Linux with MinGW-w64, and optionally test them under Wine.
#
#   scripts/build-windows-mingw.sh <ffgl-sdk-dir> <glew-2.1.0-source-dir> [out-dir]
#
# Needs: g++-mingw-w64-x86-64. For the test: wine64 and an X display (e.g. Xvfb :99 + DISPLAY=:99).
# The result is self-contained: it only imports KERNEL32, msvcrt and OPENGL32.
set -euo pipefail

SDK_DIR="$(cd "$1" && pwd)"
GLEW_DIR="$(cd "$2" && pwd)"
OUT_DIR="$(mkdir -p "${3:-build-win}" && cd "${3:-build-win}" && pwd)"
SRC_DIR="$(cd "$(dirname "$0")/.." && pwd)"

CC=x86_64-w64-mingw32-gcc
CXX=x86_64-w64-mingw32-g++
SDK_LIB="$SDK_DIR/source/lib"

# The SDK includes <Windows.h>; MinGW's header is lowercase and Linux is case-sensitive.
mkdir -p "$OUT_DIR/shim"
echo '#include <windows.h>' > "$OUT_DIR/shim/Windows.h"

FLAGS=(-O2 -DGLEW_STATIC -DNOMINMAX -I"$OUT_DIR/shim" -I"$GLEW_DIR/include" -I"$SDK_LIB")
CXXFLAGS=("${FLAGS[@]}" -std=c++14 -Wno-attributes)

$CC "${FLAGS[@]}" -c "$GLEW_DIR/src/glew.c" -o "$OUT_DIR/glew.o"
$CXX "${CXXFLAGS[@]}" -c "$SDK_LIB/FFGLSDK.cpp" -o "$OUT_DIR/ffglsdk.o"
for PLUGIN in ImagFX ImagFlame ImagLooks ImagKey; do
	$CXX "${CXXFLAGS[@]}" -c "$SRC_DIR/src/$PLUGIN.cpp" -o "$OUT_DIR/$PLUGIN.o"
	$CXX -shared -static -static-libgcc -static-libstdc++ -o "$OUT_DIR/$PLUGIN.dll" \
		"$OUT_DIR/$PLUGIN.o" "$OUT_DIR/ffglsdk.o" "$OUT_DIR/glew.o" -lopengl32
	echo "Built $OUT_DIR/$PLUGIN.dll"
done

$CXX "${CXXFLAGS[@]}" "$SRC_DIR/test/headless_host.cpp" "$OUT_DIR/glew.o" -static \
	-o "$OUT_DIR/headless_host.exe" -lopengl32 -lgdi32

echo "Test:  (cd $OUT_DIR && mkdir -p frames && wine64 headless_host.exe ImagFlame.dll frames flame_keyed 120)"
