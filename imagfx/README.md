# IOKI FX

Six camera effect plugins for Resolume Arena/Avenue (FFGL 2.x, C++/OpenGL/GLSL). Each is a separate file and appears separately in Arena's Effects browser.

The plugins were called IMAG up to build 12. If you installed those, delete the old `Imag*.dll` / `Imag*.bundle` files from Extra Effects before copying the IOKI ones. They share plugin IDs, so Arena should only see one copy of each.

Every plugin has an **Effect On** toggle as its first control. Switched off, it passes the camera through untouched. When you switch it back on, its trails and history start fresh. Map it to a key or MIDI button to cut the effect in and out live.

| Plugin | File | What it does |
|---|---|---|
| **IOKI Face** | `IokiFace.dll` / `IokiFace.bundle` | Multi-face tracking: a box on every face, connecting lines, numbers (or detection confidence) and optional landmark dots, over the same five feeds as IOKI Blob |
| **IOKI Blob** | `IokiBlob.dll` / `IokiBlob.bundle` | Blob tracking: squares on moving subjects, connecting lines and numbers above each square, over a Normal / Monochrome / Dual Tone / ASCII / Halftone feed |
| **IOKI Key** | `IokiKey.dll` / `IokiKey.bundle` | Keys the performer out of the camera (difference, chroma or luma key) so the other effects can use **Keyed alpha** |
| **IOKI FX** | `IokiFX.dll` / `IokiFX.bundle` | Glitch looks: RGB time delay, feedback trails, noise warp, beat-synced slice glitch, sheen sweep, scanlines, static |
| **IOKI Flame** | `IokiFlame.dll` / `IokiFlame.bundle` | Monochrome camera with flame trails rising off the subject, heat-haze distortion, spectral dispersion and subject tracking with small tracking squares |
| **IOKI Looks** | `IokiLooks.dll` / `IokiLooks.bundle` | Effects panel: one dropdown of camera looks (night vision, fire map, gold edge, analog TV, glitter, comic, halftone, paper texture, animated cartoon, ...) plus overlays for every look: tracking squares, coloured echoes, ASCII, and distortion / dispersion / flames / depth / dual-tone levels with custom colours |

- `src/`: the plugins and their shaders; `TrackOverlay.h` holds the tracking and graphics that IOKI Blob and IOKI Face share
- `third_party/facedetection/`: build shim and licence for [libfacedetection](https://github.com/ShiqiYu/libfacedetection), the face detector in IOKI Face (BSD 3-Clause, Shiqi Yu; fetched at build time)
- `test/`: a headless FFGL host for rendering and checking the plugins without Resolume (Linux, or Windows/Wine)
- `scripts/build-windows-mingw.sh`: cross-compiles all six DLLs from Linux (IOKI Face needs `FACEDETECT_DIR`)
- Ready-made Windows and macOS builds: GitHub Actions (`.github/workflows/imagfx.yml`) attaches them to a release on every push
- `docs/ANALYSIS.md`: product analysis, how the effects work, build/install instructions, roadmap
- `docs/test-renders.png`, `docs/flame-renders.png`, `docs/looks-renders.png`, `docs/looks-overlays.png`, `docs/key-renders.png`, `docs/blob-renders.png`, `docs/blob-tracking-strip.png`: frames rendered by the test host

## IOKI Blob

Live blob tracking in the classic "data tracking" style: steady, thin-outlined boxes on the subject, a decimal label inside each box (0.1429, 0.2857, …) and a chain of lines linking them. All of it is drawn over the camera feed, which you can restyle.

**How it works:** the GPU builds the subject mask and a grid of blob positions; a small grid (a few KB) is read back each frame. On the CPU, the grid is smoothed over time and blobs become **tracks**:
- each track is matched to where it is predicted to be (it keeps a velocity), so fast movement doesn't lose it;
- it keeps its ID, label and number for its whole life;
- it has to be seen for a few frames before it appears, holds its place for **Hold Time** if a detection is missed, and fades in and out instead of blinking.

Squares, lines and digits are drawn as geometry, and digits and ASCII come from a small built-in pixel font. Every feed mode is its own small shader, compiled on first use. If a driver ever rejects one, the plain camera shows and **Status** gives the driver's reason.

| Group | Controls |
|---|---|
| Detection | **Detect** (Motion / Keyed alpha / Brightness / Motion in key), Threshold, Grid (8–64 cells), **Blob Mode** (Points / Regions), Max Blobs, Spacing, **Smoothing** (steadier, slower boxes), **Hold Time** (how long a box survives a missed detection) |
| Squares | Square Style (Outline / Filled / Corners), Square Size, **Size Variation** (0 = all the same size), Square Thickness, **Square Intensity**, Square Colour |
| Lines | Lines (Off / **Chain** / Nearest / Network), Links Per Blob, Link Distance, Line Thickness, Line Intensity, Line Colour |
| Numbers | Numbers (Off / **Decimal** / Random / Blob ID / Coordinates), Digits, Number Speed, Numbers On Beat, Number Size, **Number Position** (Inside top / Above / Centre), Number Intensity, Number Colour |
| Feed | **Feed** (Normal / Monochrome / Dual Tone / ASCII / Halftone), Feed Mix, Feed Contrast, ASCII / Dot Size, Feed Dark and Feed Light colours, Status |

**Tips:**
- The defaults give the classic look: Brightness detection, about 8 large boxes, Decimal labels inside, and a Chain of lines.
- Switch **Detect → Motion** to follow movement instead.
- To track only the performer, put **IOKI Key** first and set **Detect → Keyed alpha**.
- If boxes still feel jumpy, raise **Smoothing** and **Hold Time**.

## IOKI Face

A multi-face tracker that works like IOKI Blob, but on faces. Every face gets a box that glides with it and keeps its label. Lines link the faces, and the feed underneath can be restyled.

**How it works:** each frame is scaled down (Detect Size), read back and handed to a face detector running on its own CPU thread. The detector is libfacedetection, a small CNN that returns a box, a confidence and five landmarks (eyes, nose, mouth corners) per face. Rendering never waits for it: the boxes are drawn every frame from the latest results. Between results they ease toward the newest position, using the same tracking as IOKI Blob (stable labels, Hold Time, fade in and out).

| Group | Controls |
|---|---|
| Detection | **Detect Size** (320 fastest / 480 / 640 for small or distant faces), **Min Confidence**, Max Faces (1–16), Smoothing, Hold Time |
| Boxes | Box Style (Outline / Filled / Corners), **Box Shape** (Face / Square), Box Size (padding around the face), Box Thickness, Box Intensity, Box Colour, **Landmarks**, Landmark Size |
| Lines | Lines (Off / **Chain** / Nearest / Network), Links Per Face, Link Distance, Line Thickness, Line Intensity, Line Colour |
| Numbers | Numbers (Off / **Decimal** / Random / Face ID / Coordinates / **Confidence**), Digits, Number Speed, Numbers On Beat, Number Size, Number Position, Number Intensity, Number Colour |
| Feed | **Feed** (Normal / Monochrome / Dual Tone / ASCII / Halftone), Feed Mix, Feed Contrast, ASCII / Dot Size, Feed Dark and Feed Light colours, Status |

**Status** shows how many faces are found, the detector size and how long a detection takes. Detection takes about 20 ms at 480 on a typical desktop CPU, so boxes update 30–50 times a second.

**Requirements:** on Windows the detector needs a CPU with AVX2 (Intel 2013+ / AMD 2015+). Without it, Status says so and only the feed is drawn. Apple Silicon Macs use NEON. Intel Macs run a plain version that is about 3× slower, so use Detect Size 320 there.

**Tips:**
- Faces that are small or far away: set **Detect Size → 640**.
- Boxes appearing on things that aren't faces: raise **Min Confidence**.
- For a steadier look: raise **Smoothing**. For boxes that hold through a quick head turn: raise **Hold Time**.

## IOKI Key

Put **IOKI Key first** on the camera clip, then IOKI Looks / IOKI Flame after it with **Subject Source → Keyed alpha**. Its output is the performer with a transparent background.

### Setting up a key
1. Choose **Key Mode**:
   - **Difference (learn stage)**: for a fixed camera on any background. Clear the stage, press **Learn Background** (it averages one second), then bring the performer in. It also learns automatically from the first second after loading.
   - **Chroma (green/blue screen)**: set **Key Colour** to roughly the screen colour (green or blue is enough; the exact shade doesn't matter).
   - **Luma (bright subject)**: keeps bright areas; **Luma Invert** keeps dark ones.
2. Set **View → Matte**: white is kept, black is removed. Raise **Threshold** until the background is fully black, then lower it until the performer is solid white. **Softness** widens the transition.
3. Clean up with **Clean Specks** (removes noise dots), **Shrink / Grow** (centre is neutral), **Feather** (soft edges) and **Edge Smoothing** (reduces edge flicker over time). Use the **Crop** sliders to cut out areas that should never be keyed in, such as a lighting rig at the frame edge.
4. **View → Overlay** shows the removed area tinted red over the camera. Switch back to **View → Output** for the show.

| Control | What it does |
|---|---|
| Key Mode, Learn Background, Auto Update | Keyer, plate capture, and how fast the learned stage follows slow lighting changes |
| Threshold, Softness | Where the key cuts, and how soft the cut is |
| Ignore Shadows | Difference key: brightness changes alone (shadows, dimming) don't count as subject |
| Key Colour, Spill Removal | Chroma key: screen colour, and removing its colour cast from the performer's edges |
| Luma Invert | Luma key: keep dark instead of bright |
| Clean Specks, Shrink / Grow, Feather, Edge Smoothing | Matte clean-up |
| Crop Left / Right / Top / Bottom | Garbage matte |
| View | Output / Matte / Overlay |
| Background, Background Colour | Transparent (for chaining), black, or a colour |
| Status | What the key is doing, e.g. "Background learned" |

**Limits:** the difference key needs a camera that doesn't move or zoom after learning. It keeps anything that differs from the learned stage, so moving lights, video walls and haze also get keyed in. Crop them out, or use a green/blue screen where possible. For a moving camera, use a chroma or luma key, or an AI keyer upstream (NVIDIA Broadcast + Auto Mask). AI person segmentation inside the plugin is possible as a later step (ONNX Runtime), but it is a much larger build.

## IOKI Flame

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
| Tracker HUD, HUD Opacity | **Squares** (default): small tracking squares along the subject's outline, plus a few inside. **Squares + Lines** joins neighbouring squares. Also Box, Ring, or Off |
| Square Count, Square Size | Few / Medium / Many squares, and their size |
| Beat Pulse | Flames surge on every beat of Resolume's BPM |
| Reset Flames | Clears the flames and re-centres the tracker |

**Tips:**
- On a static camera, **Motion** needs no keying, but a still subject stops emitting.
- For flames that keep burning on a still subject, key the camera (NVIDIA Broadcast background removal + Auto Mask) and choose **Keyed alpha**.
- A moving camera makes the whole frame "move", so use **Keyed alpha** there too.

## IOKI Looks

Our own looks in the style of the reference reel. Every look follows the same recipe: crush the stage to black, turn the performer into high-contrast monochrome, recolour with a gradient map, then add the look's signature detail.

| Look | What it is |
|---|---|
| Night Vision | Green gradient map with soft glow, scanlines and grain |
| Fire Map | Black → red → orange → yellow, with hot edges |
| Gold Edge | Gold gradient map with strong glowing outlines |
| Blood Red | Crushed pink-red duotone |
| Analog TV | Grey with scanlines, horizontal ghosting and a rolling noise band |
| Glitter Ice | Ice-blue with sparkles thrown off the subject's edges |
| Ice Cyan | Cyan duotone with scanlines |
| Faded Film | Lifted blacks, warm desaturated tones |
| RGB Split | High-contrast mono with red/cyan split and beat-triggered slice glitches |
| Comic | Pop-art bands with Ben-Day dots and ink outlines |
| Halftone | CMYK dot screen on paper |
| Paper Texture | Printed onto fibrous paper, with stop-motion jitter |
| Animated Cartoon | Flat cel shading with ink outlines that "boil" like hand-drawn animation |

| Control | What it does |
|---|---|
| Mix | Dry/wet |
| Look | The dropdown above. Stylised looks sit below a separator |
| Intensity | Strength of the look |
| Black Crush, Contrast | How hard the background goes to black, and the S-curve |
| Edge Glow | Glowing outlines in the look's highlight colour |
| Dot / Cell Size | Halftone/comic dot size, cartoon shading bands |
| Texture | Grain, scanlines, paper fibres |
| Sparkles | Adds Glitter Ice's sparkles to any look |
| Hue Shift | Rotates the look's colours for your own palette |
| BPM Sync, Animation Rate | Beat-locks RGB Split's glitches; sets the cartoon boil / paper jitter rate |

### Overlays (work with every look)

These sit in their own groups under the look controls, and stack in this order over whichever look is selected: distortion, dispersion, depth, echoes, flames, dual tone, ASCII, tracking squares.

| Group | Control | What it does |
|---|---|---|
| Subject | Subject Source, Subject Threshold | How the performer is found: **Motion**, **Keyed alpha** (NVIDIA Broadcast + Auto Mask), **Brightness** or **Motion in key**. Tracking, echoes, ASCII, flames and depth all use this |
| Tracking | Tracking, Square Count, Square Size, Square Colour | Small tracking squares on the subject's outline (Off / Squares / Squares + Lines), with their own colour |
| Echo | Echo Level, Echo Delay, Echo Copies, Echo Colour A, Echo Colour B | Delay shift: up to 4 copies of the subject from earlier frames trail behind it, tinted from colour A to colour B |
| ASCII | ASCII, ASCII Size, ASCII Colour | Redraws the Subject, the Background or the Whole frame as ASCII characters chosen by brightness |
| Levels | Distortion | Noise warp, plus heat haze when flames are on |
| Levels | Dispersion | Rainbow spectral split radiating from the subject |
| Levels | Flames, Flame Colour | Flames rising off the subject (Fire / Blue / Toxic / Violet / White) |
| Levels | Depth | 3D pop-out: background defocuses and dims, subject extrudes behind itself |
| Levels | Dual Tone, Dual Dark, Dual Light | Maps the image between two colours you choose |

Colour controls are hue / saturation / brightness, shown in Arena as a rainbow colour picker. The Status field reads "OK: <look>", or shows the graphics driver's error if a look can't run on your machine.

**Tip:** for black backgrounds like the reel, key the camera (NVIDIA Broadcast + Auto Mask). The looks keep the transparency, so you can stack IOKI Looks after IOKI Flame or IOKI FX on the same clip.

## Colour controls

Every colour control in every plugin is **hue / saturation / brightness**, which Arena shows as a rainbow colour picker with saturation and brightness. This covers IOKI Looks (tracking squares, echo A/B, ASCII, dual tone), IOKI Blob and IOKI Face (squares/boxes, lines, numbers, feed colours) and IOKI Key (key colour, background colour). Plugins convert to RGB internally.

## Build

Quick build (Windows, any recent Visual Studio with the C++ workload):

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

Copy the six `.dll` files (`IokiFX`, `IokiFlame`, `IokiLooks`, `IokiKey`, `IokiBlob`, `IokiFace`) from `build\Release\` to `Documents\Resolume Arena\Extra Effects\` and restart Arena. macOS and Linux steps are in `docs/ANALYSIS.md`.
