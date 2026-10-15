# ph13labs "Resolume Camera Effects": analysis and build plan

## 0. What I could and could not examine

| Source | Status |
|---|---|
| YouTube video `UAhhl3Wv2Pw` | **Not viewed.** The analysis environment's network policy blocks youtube.com, and web search found no indexed title, transcript or description for that ID. |
| Gumroad page `ph13labs.gumroad.com/l/camerafx` | **Not loaded directly** (same network block). Its contents come from search-engine extracts of the page and its sister pages (store.ph13labs.com, other ph13labs Gumroad listings). |
| Resolume FFGL SDK (github.com/resolume/ffgl) | Cloned and used. The prototype compiles against it and runs in a test host. |

So section 1 is **not** a frame-by-frame breakdown of the video. It describes the product from its listing, plus a reconstruction of the techniques its listed effects (Red Delay, Sheen, Static, Color Bumper) almost certainly use. Everything marked *Inferred* should be checked against the video.

---

## 1. What the product is

### Confirmed (from the product listings)

- **It is not a compiled plugin.** It's sold as "20 Camera Effects ready to go all inside of base Resolume" (Launch Week Edition: +5 bonus effects; listed at $80). The listing says it was made in Resolume Arena 7.20.1.
- **Delivery format: a Resolume showfile (`.avc`).** The listing says the effects "have been designed as 'clips' to be layered on top of your camera". Install means opening the showfile and either opening it as a deck or copy-pasting the clips into your own composition.
- **Volume 2** is a separate pack of 10 effects with variations (~19), described as more BPM-focused. It was made in Arena 7.23.2 with "no additional plug-ins required".
- **Roadmap: "Version 2.0: Coming 2025 – Converted to wire patches."** Single effects are already sold as **Resolume Wire patches** (`.wired`, Arena 7.4+), e.g. *Red Delay* ($39, "built to enhance your IMAG looks"), *Sheen* and *Static*. A free one, *Color Bumper*, uses 12 blend modes.
- **The vendor's tips:** run the camera through **NVIDIA Broadcast** (background blur/removal) and put Resolume's **Auto Mask** on the source.

### What that tells us technically (high confidence)

1. **No custom GPU code is involved in Vol. 1/2.** Every look is a stack of Resolume's built-in effects, and their parameters are animated by Resolume's BPM-synced parameter animation. The "plugin" is really a **preset library**.
2. **The camera feed reaches the effect clips through routing.** A "clip layered on top of your camera" that processes the camera image most plausibly uses a **Layer Router** source pointed at the camera layer (or the camera input itself as the clip source). The effect chain runs on that copy, and the clip is composited over the original with a layer blend mode (Add / Screen / Lighten / Alpha). *(Inferred: this is the standard pattern for this kind of pack.)*
3. **The NVIDIA Broadcast + Auto Mask tip shows the looks depend on alpha.** Background removal gives a performer-shaped alpha matte (Auto Mask turns the virtual camera's black background into transparency). Effects like a glow, an outline, a sheen sweep or trails then act on **the person only**, not the stage behind them. That one step is what separates "IMAG camera FX" from a generic full-frame filter.
4. **Moving to Wire** is the natural upgrade path. Wire is Resolume's node-based patcher, which turns a 6–10-effect clip stack into one effect with a curated set of exposed controls. It's the same idea we want, built in a node graph instead of C++.

### What each named effect is almost certainly doing (*Inferred*)

| Name | Likely signal path |
|---|---|
| **Red Delay** | The red channel is shown from N frames ago, while G/B are live. Moving performers grow a red "ghost" on one side and cyan on the other. Built-in route: a delay/RGB-delay style effect, or two routed copies (one through a Freeze/Delay with only red passing) blended with Add. |
| **Sheen** | A bright soft-edged diagonal band sweeps across the frame once per beat or bar. It's masked by the performer alpha, so it reads as light catching the subject. Built-in route: a gradient/shape source moved by a BPM-synced animation, used as a mask or blended with Add over the keyed camera. |
| **Static** | Per-frame random grain, horizontal tear lines and scanlines, often with a small RGB offset. Built-in route: Noise / Static-type generators blended with Overlay or Screen, plus a shift/RGB effect. |
| **Color Bumper** | Per-beat colour flashes/pulses cycling through 12 layer blend modes. Built-in route: a Solid Color source + a blend-mode selector + BPM-synced opacity envelopes. |
| Typical others in packs like this | Feedback trails (zoom/rotate echo), mirror/kaleido, posterize/threshold "comic" looks, edge-detect outlines, slice glitch, VHS warp, strobe. |

**Real-time vs pre-rendered:** All of the above runs in real time on the live camera; nothing in the listing implies pre-rendered content. The only possible "generated" elements would be texture overlays (grain plates, light-leak loops) used as clips. Even those are inferred, not confirmed.

---

## 2. How these effects work underneath

Resolume renders everything on the GPU with OpenGL. Each layer/clip is a **texture**, each effect is one or more **fragment-shader passes** that read input textures and write to an off-screen **framebuffer object (FBO)**, and layers are composited with **blend modes** (per-pixel formulas such as `max(a,b)` for Lighten, `a+b−ab` for Screen, `a+b` for Add). The building blocks below cover every look in this category:

| Concept | What it is | Where it shows up |
|---|---|---|
| **Frame history (ring buffer)** | Keep the last N frames as N textures and write each new frame into the oldest slot. | Red Delay / RGB time-split, echo, freeze, slit-scan. |
| **Per-channel sampling** | Read R, G and B from different textures or different UV offsets. | Chromatic aberration, RGB delay, analogue "misregistration". |
| **Feedback loop** | Output frame *t* is an input to frame *t+1* (ping-pong between two FBOs). Transform the fed-back image slightly each frame (zoom 1.01×, rotate 0.5°, hue +2°) and it becomes a tunnel, spiral or rainbow trail. | Trails, echo, infinite zoom, "ghost" smears. Use 16-bit float buffers, because 8-bit feedback bands and never fully decays. |
| **Displacement / UV warping** | Offset the lookup coordinate by a noise field or texture: `colour = tex(uv + k·noise(uv,t))`. | Warp, wobble, heat haze, VHS wave, glitch slices (displacement per row band). |
| **Masking / alpha** | A matte (from AI background removal, chroma or luma key) limits where an effect applies. FFGL textures are **premultiplied** (rgb already × alpha). | Effects that touch only the performer. |
| **Procedural generators** | Hash/noise functions evaluated per pixel per frame. | Static grain, tear lines, scanlines, sheen bands. |
| **Clock / BPM** | Resolume gives plugins `bpm` and `barPhase` (0–1 across a 4/4 bar). From that you derive a beat phase ramp and a "new beat" trigger. | Sheen sweep per beat, glitch hit on the downbeat, colour bumps. |
| **Optical flow** | Per-pixel motion vectors between frames. | **Probably not used.** Built-in Resolume effects don't expose flow, and nothing in the listing implies it. Worth considering later for "smear in the direction of motion" looks. |
| **Particles** | GPU-simulated points emitted from the image or matte. | Not indicated for this product. The FFGL SDK ships a particle example if we want it later. |

---

## 3. Can we build something similar? Yes

The core visual vocabulary is standard real-time GPU image processing. There are three routes:

| Route | What you get | Limits | Verdict |
|---|---|---|---|
| **A. Resolume built-ins + presets** (what ph13labs did) | Layer Router + effect stacks + BPM-synced parameter animation, saved as clips or a deck. | Fixed to what built-in effects offer. Lots of UI to manage. Fragile when moving between showfiles. | Fast to prototype looks. This is how to **design** looks before coding them. |
| **B. Resolume Wire patch** | One effect node with our own exposed controls, built from Wire's node library, with custom GLSL where needed. Distributed as `.wired` and works in Arena/Avenue 7.4+. | Needs a Wire licence for us (users only need Arena). Node graphs get unwieldy for heavy custom logic. | Good mid-way option, and it's where ph13labs is heading. |
| **C. FFGL plugin (C++ + OpenGL/GLSL)** | A native `.dll`/`.bundle` effect. Full control: own buffers, any number of passes, frame history, BPM clock, custom UI parameters. Fastest at runtime. | You maintain C++ builds for Windows + macOS (universal binary). | **Recommended** for an owned, customisable product. **Prototype included** (`imagfx/`). |

Why FFGL rather than Vulkan or DirectX: Resolume's effect plugin interface **is** FFGL (OpenGL 4.1 core on macOS, OpenGL with GLEW on Windows). Vulkan, HLSL or CUDA can't plug into Arena's effect chain directly. Anything non-OpenGL would need a separate app sending video into Resolume via **Spout** (Windows) or **Syphon** (macOS), which adds latency and operator complexity. Use that only for heavy ML work (see roadmap phase 5).

---

## 4. Prototype: `IMAG FX` (FFGL effect, in `imagfx/`)

### Processing pipeline (three full-screen passes per frame)

```
host input ──► [1 COPY] ──► history ring (31 × RGBA8)
                                │  newest, newest−R, newest−B
                                ▼
feedback[read] (RGBA16F) ──► [2 TRAIL] glitch slices → noise warp → RGB time-split + radial chroma
                                │       → zoom/rotate/hue-shifted feedback, blended (Lighten/Screen/Add)
                                ▼
                          feedback[write] ──► [3 FINISH] sheen sweep → scanlines → static/tears
                                                   │      → dry/wet mix vs original → premultiply
                                                   ▼
                                             host framebuffer        (swap read/write)
```

Effects are split across passes on purpose. Anything that should *persist* (RGB delay, warp, glitch) lives in pass 2, which feeds back. Anything that should stay crisp (sheen, grain, scanlines) lives in pass 3, so it never smears into the trails.

### Parameters exposed in Resolume

| Parameter | Range → meaning |
|---|---|
| Mix | 0–1 dry/wet against the untouched input |
| Red Delay, Blue Delay | 0–30 frames of time offset per channel (green is always live). Display shows "N frames". |
| Chroma Split | Radial R/B spatial offset |
| Trails | Feedback amount (curved so the useful range spreads across the slider) |
| Trail Zoom / Trail Rotate | Per-frame transform of the feedback (0.5 = none; ±5 %/frame, ±5°/frame) |
| Trail Hue | Hue rotation per feedback iteration, which gives rainbow trails |
| Trail Blend | Lighten / Screen / Add |
| Warp, Warp Scale, Warp Speed | Value-noise displacement map |
| Glitch | Horizontal slice displacement fired at each cycle start, decaying over the cycle |
| Sheen, Sheen Width, Sheen Angle | Soft light band sweeping once per cycle, weighted by alpha |
| Scanlines, Static | CRT lines; per-frame grain + random tear lines |
| BPM Sync | On: cycle follows Resolume's clock. Off: free-runs at *Free Rate*. |
| Beat Rate | 1/4 beat, 1/2 beat, 1 beat, 2 beats, 1 bar |
| Free Rate | Cycle speed when not BPM-synced |
| Reset Trails | Event: clears the feedback buffer |

All parameters can be MIDI/OSC-mapped and animated in Resolume like any built-in effect.

### Verified here

- Compiles against the current `resolume/ffgl` master (Linux build).
- A headless FFGL host (`imagfx/test/headless_host.cpp`) loads the plugin through `plugMain` like Resolume does. It drives SetTime/SetBeatInfo at 120 BPM, renders 120 frames for each of 6 presets, and checks the FFGL state contract after every frame (program 0, texture unit 0, no bound texture, host FBO restored, no GL errors). **All 6 presets: 0 failures.** Output: `docs/test-renders.png`.
- Rendering ran on Mesa llvmpipe (a CPU rasteriser), so this proves correctness, **not performance**.

### Not verified here

- **The Windows `.dll` and macOS `.bundle` builds were not compiled** (no Windows/macOS toolchain in this environment), and **the plugin hasn't been loaded in Resolume Arena**. The CMake paths follow the SDK's own Visual Studio/Xcode settings (static GLEW on Windows, OpenGL framework + universal binary on macOS). Expect small first-build fixes.

### Build and install

**Windows (Visual Studio 2022 or newer, x64):**
```bat
cd imagfx
cmake -S . -B build -A x64
cmake --build build --config Release
copy build\Release\ImagFX.dll "%USERPROFILE%\Documents\Resolume Arena\Extra Effects\"
```
**macOS (Xcode command-line tools):**
```sh
cd imagfx
cmake -S . -B build -G Xcode
cmake --build build --config Release
cp -R build/Release/ImagFX.bundle ~/Documents/Resolume\ Arena/Extra\ Effects/
```
CMake fetches the FFGL SDK automatically, or you can pass `-DFFGL_SDK_DIR=/path/to/ffgl`. Arena 7 uses `Documents/Resolume Arena/Extra Effects` (the SDK readme gives the older `Documents/Resolume/Extra Effects`). You can also add any folder as an extra effects folder in Arena's Preferences. Restart Arena after copying. "IMAG FX" appears in the Effects browser; drop it on the camera clip or layer. On macOS an unsigned bundle may need `xattr -dr com.apple.quarantine ImagFX.bundle` (or proper codesigning before distribution).

**Headless test (Linux):**
```sh
cmake -S imagfx -B build -DIMAGFX_BUILD_TEST_HOST=ON && cmake --build build
Xvfb :99 & DISPLAY=:99 build/test/headless_host build/ImagFX.so /tmp everything 120
```

### Testing it in Resolume
1. Put a camera (or the NVIDIA Broadcast virtual camera + **Auto Mask**) on a layer.
2. Add IMAG FX. Start with Red Delay ≈ 8 frames and Chroma ≈ 0.3, then layer on Trails / Sheen.
3. Toggle BPM Sync and tap tempo. Sheen and Glitch should lock to the beat.
4. Check the per-effect GPU time in Arena's performance view at your show resolution.

---

## 5. Roadmap

Each phase leaves a shippable plugin, and each module can be developed and tested on its own in the headless host (add a preset, render, diff images).

**Phase 0: look development (no code, 1–2 days).** Rebuild the target looks with Resolume built-ins (Layer Router + effect stacks) and agree with operators which ones matter. Note the parameter ranges they actually use. This becomes the spec. If you have the video, list every look in it with a timestamp here.

**Phase 1: core plugin (done as prototype).** History ring, RGB time-split, chroma, feedback trails, warp, slice glitch, sheen, static, BPM clock.

**Phase 2: production hardening (≈1 week).**
- Windows + macOS CI builds (GitHub Actions: `windows-latest` with MSVC, `macos-latest` universal), plus codesigning/notarisation for macOS.
- Allocate the history buffer lazily, sized to the largest delay actually used, and free it when delays return to 0. Set a VRAM budget (see performance).
- Parameter groups (FFGL `FF_GET_PARAM_GROUP`) so the UI shows *Time / Trails / Distort / Surface / Clock* sections.
- Smoothing on parameters to avoid zipper noise when MIDI-mapped.
- Built-in look presets (an option parameter that sets several others via value-change events, Arena 7.4+).

**Phase 3: alpha-aware looks (≈1 week).** Matte-driven effects that use the performer alpha: outline/rim light (alpha edge via Sobel), inner glow, an alpha-only trail mode (trails of the silhouette tinted by Trail Hue), and a "background vs subject" target selector for each effect group.

**Phase 4: more modules (pick per show).** Mirror/kaleido, posterize/threshold, halftone/dot screen, slit-scan (each row from a different history frame, reusing the ring buffer), luma-keyed trails, LUT colour grade (3D LUT file via `FF_TYPE_FILE`), audio-reactive inputs (FFGL FFT parameter).

**Phase 5 (optional): ML/optical flow.** Built-in segmentation (ONNX Runtime + DirectML/CoreML) would remove the NVIDIA Broadcast dependency, and optical flow (NVIDIA Optical Flow SDK, or a coarse GPU block-matching pass) would enable motion-direction smears. This is substantially more work; read the SDK's `bundling-native-dependencies-windows.md` before shipping native DLLs.

### Performance notes for a show PC

- **Cost per pixel is tiny:** pass 2 does about 5 texture reads plus some noise maths, and pass 3 about 3. At 1080p60 that's roughly 1 GB/s of texture traffic, well within any discrete GPU. Expect well under a millisecond per frame on a modern NVIDIA/AMD card *(estimate, not measured)*.
- **VRAM is the real cost:** the 31-frame RGBA8 history uses 31 × 8.3 MB ≈ **257 MB at 1080p** and ≈ **1 GB at 4K**, plus two RGBA16F feedback buffers (~33 MB at 1080p). Lazy allocation (phase 2) and a "Max Delay" setting keep this in check. If several instances need history, share it per camera via one instance, or route the camera once and stack effects after it.
- Keep everything in **one plugin** rather than several chained plugins. Each extra Resolume effect adds a full-frame copy.
- Avoid `glGetError`, readbacks and `glFinish` in the render path, and allocate nothing per frame (the prototype allocates only when resolution changes).
- Run Arena's composition at the camera's native resolution and frame rate, and feed it a capture card with GPU-direct upload (e.g. Blackmagic/AJA at 1080p50/60). Most IMAG latency comes from the capture path, not shaders.
- Test on the target GPU with Arena's per-effect timing. On NVIDIA laptops, force the discrete GPU for Arena.

---

## 6. Sources

- [Resolume Camera Effects – ph13labs (Gumroad)](https://ph13labs.gumroad.com/l/czutp) · [store.ph13labs.com/l/camerafx](https://store.ph13labs.com/l/camerafx) · [ph13labs.gumroad.com/l/camerafx](https://ph13labs.gumroad.com/l/camerafx) *(via search extracts)*
- [Red Delay wire patch](https://ph13labs.gumroad.com/l/reddelay) · [Free Color Bumper wire patch](https://ph13labs.gumroad.com/p/free-wire-patch-color-bumper-v1-0) · [ph13labs storefront](https://ph13labs.gumroad.com/)
- [Resolume forum: NVIDIA Broadcast virtual camera shows black instead of alpha (Auto Mask fix)](https://resolume.com/forum/viewtopic.php?t=20134)
- [Resolume FFGL SDK](https://github.com/resolume/ffgl) (source used directly)
