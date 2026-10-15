# IMAG FX

Two camera-look effect plugins for Resolume Arena/Avenue (FFGL 2.x, C++/OpenGL/GLSL). Each is a separate file and appears separately in Arena's Effects browser.

| Plugin | File | What it does |
|---|---|---|
| **IMAG FX** | `ImagFX.dll` / `ImagFX.bundle` | Glitch looks: RGB time delay, feedback trails, noise warp, beat-synced slice glitch, sheen sweep, scanlines, static |
| **IMAG Flame** | `ImagFlame.dll` / `ImagFlame.bundle` | Monochrome camera with flame trails rising off the subject, heat-haze distortion, spectral dispersion and subject tracking with an optional HUD |

- `src/`: the plugins (`ImagFX.cpp`, `ImagFlame.cpp`) and their shaders
- `test/`: a headless FFGL host for rendering and checking the plugins without Resolume (Linux, or Windows/Wine)
- `scripts/build-windows-mingw.sh`: cross-compiles both DLLs from Linux
- Ready-made Windows and macOS builds: GitHub Actions (`.github/workflows/imagfx.yml`) attaches them to a release on every push
- `docs/ANALYSIS.md`: product analysis, how the effects work, build/install instructions, roadmap
- `docs/test-renders.png`, `docs/flame-renders.png`: frames rendered by the test host

## IMAG Flame

### How it works

Six GPU passes per frame, with no CPU readback:

1. **Frame history**: keeps the current and previous camera frames, for motion detection.
2. **Subject mask**: works out where the subject is, from motion, the keyed alpha, brightness, or motion inside the key.
3. **Moments**: the mask is reduced to a single pixel by mipmapping. That pixel holds the subject's centre and spread.
4. **Tracker**: smooths that centre and spread over time. The result places the HUD, focuses flame emission and centres the radial dispersion.
5. **Fire**: a heat field rises through curl-noise turbulence, cools into flickering tongues, and is fed new heat from the subject's body or outline.
6. **Composite**: the camera turns monochrome, the heat bends the image (heat haze), and a seven-tap spectral split adds dispersion. Flames are screened on top, optionally behind the subject, followed by the HUD and the dry/wet mix.

### Controls

| Control | What it does |
|---|---|
| Mix | Dry/wet against the original camera |
| Mono, Contrast, Mono Tint | How black-and-white the camera goes, its contrast, and a Neutral / Cold / Warm / Sepia tint |
| Subject Source | **Motion** (any camera; flames follow movement), **Keyed alpha** (use with NVIDIA Broadcast + Auto Mask; flames burn even when the subject stands still), **Brightness**, **Motion in key** |
| Threshold | How strong the signal must be to count as subject |
| Flame Amount, Flame Height, Turbulence, Flame Life | How much fire, how fast it rises, how much it swirls, and how long trails last |
| Burn Edges | 0 = flames from the whole subject, 1 = only from its outline |
| Wind | Pushes flames sideways (centre = none) |
| Flame Color, Flame Glow | Fire / Blue / Toxic / Violet / White palette, and brightness |
| Flames Behind | Hides flames in front of the subject so they appear to burn behind them |
| Heat Haze | Refraction shimmer driven by the flames |
| Distortion, Distort Scale | Extra noise warp over the whole image |
| Dispersion, Disperse Mode | Rainbow spectral split, either following the haze or radiating from the tracked subject |
| Track Focus | Concentrates flames around the tracked subject's centre |
| Track Smooth | Higher = steadier, slower tracking |
| Tracker HUD, HUD Opacity | Off / corner Box / Crosshair / spinning Ring that follows the subject |
| Beat Pulse | Flames surge on every beat of Resolume's BPM |
| Reset Flames | Clears the flames and re-centres the tracker |

**Tips:**
- On a static camera, **Motion** needs no keying, but a still subject stops emitting.
- For flames that keep burning on a still subject, key the camera (NVIDIA Broadcast background removal + Auto Mask) and choose **Keyed alpha**.
- A moving camera makes the whole frame "move", so use **Keyed alpha** there too.

## Build

Quick build (Windows, any recent Visual Studio with the C++ workload):

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

Copy `build\Release\ImagFX.dll` and `build\Release\ImagFlame.dll` to `Documents\Resolume Arena\Extra Effects\` and restart Arena. macOS and Linux steps are in `docs/ANALYSIS.md`.
