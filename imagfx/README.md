# IMAG FX

A camera-look effect plugin for Resolume Arena/Avenue (FFGL 2.x, C++/OpenGL/GLSL). It does RGB time delay, feedback trails, noise warp, beat-synced slice glitch, sheen sweep, scanlines and static, all in one effect with its own controls.

- `src/`: the plugin (`ImagFX.cpp`, `ImagFXShaders.h`)
- `test/`: a headless FFGL host for rendering and checking the plugin without Resolume (Linux, or Windows/Wine)
- `scripts/build-windows-mingw.sh`: cross-compiles `ImagFX.dll` from Linux
- Ready-made Windows and macOS builds: GitHub Actions (`.github/workflows/imagfx.yml`) attaches them to a release on every push
- `docs/ANALYSIS.md`: product analysis, how the effects work, build/install instructions, roadmap
- `docs/test-renders.png`: frames rendered by the test host for six presets

Quick build (Windows):

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Copy `build\Release\ImagFX.dll` to `Documents\Resolume Arena\Extra Effects\` and restart Arena. macOS and Linux steps are in `docs/ANALYSIS.md`.
