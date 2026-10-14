# gauge

The film strip itself, as two FFGL plugins for Resolume Arena/Avenue from one core:
**SW Gauge** (`GU01`, effect, `Gauge.bundle`) and **SW Gauge Leader** (`GU02`, source,
`Gauge Leader.bundle`). C++/GLSL, CMake MODULE → universal `.bundle`s (macOS) + Windows
`.dll`s. MIT.

Read `AGENTS.md` before changing the strip's numbers, the camera, the held frames or
the leader.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Universal (what ships): `cmake -B build-universal -DCMAKE_BUILD_TYPE=Release`
- Build: `cmake --build build --parallel`
- Install into Arena: `cmake --install build` -- **not run from a session**, it writes
  into `~/Documents/Resolume Arena/Extra Effects`
- Render a frame offline: `./build/gutest --out /tmp/f.png --size 1920x1080`
- The source: `--plugin source` on any command
- Set anything by name: `--set "Format=35 mm Scope" --set "Mode=Strip" --set "Travel=0.8"`
  (0..1 for sliders, an option by its element's name or index, 0/1 for booleans);
  anything that is neither a number nor an option is refused with exit 2
- Press an event at a frame: `--press "Roll Leader@10"`
- Feed the FFT input: `--tone 3=0.5` (bin 3, magnitude 0.5; repeatable)
- List parameters, kinds, defaults, ranges and option names: `./build/gutest --list`
- Sources: `--source card|flat|disc|white`, `--level V` for flat
- The transport: `--bpm N` (the harness hands the plugin an exact bar phase every frame)
- The exact GLSL the plugin compiles: `./build/gutest --dump-shaders DIR`
- Footage through the real shaders -- **`--pipe`**, raw RGBA frames in, raw RGBA frames
  out, `--size WxH`, `--fps N`, an optional `--script` of `frame Parameter Name value`
  cues (interpolated); the source pipes `--frames` frames with no input. A cue naming no
  parameter is refused with exit 2, a partial frame ends the stream cleanly, a failed
  render or a closed stdout exits 1:
  `ffmpeg -i in.mov -vf fps=60 -f rawvideo -pix_fmt rgba - | ./build/gutest --pipe --size 1920x1080 | ffmpeg …`
- Apple's software renderer instead of the GPU: `--software` (what a GPU-less CI runner
  has; slow: several minutes a check)
- Diagnostics from inside a check: `GU_DEBUG=1 ./build/gutest --perfs`

## Verify
- Everything: `tools/verify.sh` (fresh universal build + glslc + reserved words + every
  check at 320x180 AND 1280x720 + --pipe + the sweep + the bench + both bundles, ~40 s)
- Holes where the standards put them, every format: `./build/gutest --perfs`
- Pictures and frame lines: `./build/gutest --aperture`
- The squeeze, from a disc's moments: `./build/gutest --squeeze`
- Frames keep their pictures: `./build/gutest --held`
- The countdown, frame by frame, and on the bar: `./build/gutest --leader`
- A flash is one film frame: `./build/gutest --flash`
- The optical track is the sound at the film's speed: `./build/gutest --track`
- Film opaque, the rest as the controls say: `./build/gutest --alpha`
- Held frames survive a resize: `./build/gutest --resize`
- The checks can fail: `./build/gutest --negative`; one perturbation verbosely:
  `./build/gutest --perturb BITS --perfs` (bits in `Gauge.h`)
- Every check takes `--size WxH`; CI runs them at 320x180
- No dead controls in either plugin: `python3 tools/sweep.py` (`--size`, `--jobs`);
  `--no-context` sweeps on the defaults alone, which is the list of what needs context
- Render cost: `./build/gutest --bench` (best of three; the GPU here is shared)
- What a host sees: `~/Projects/resolume/oxbow/build/oxbow probe "build-universal/Gauge Leader.bundle"`

## Notes
- **The strip is numbers in film millimetres.** `Strip.cpp` holds every format; x runs
  across from the reference (guided) edge, y along toward the tail from the top of a
  frame. Nothing in the shader is placed at a screen position.
- **The harness keeps its own table of the published standards** (`geometry.inc`),
  separate from `Strip.cpp` on purpose: a transcription error must fail, not be measured
  back faithfully.
- **Measure a box-filtered edge from its end pixels, never by a coverage-weighted mean of
  pixel centres** (`extentOf`): the mean puts a partly covered pixel's ink at its centre.
- **The camera** (`Camera.cpp`) maps an output pixel to film mm with a 64-bit base frame
  kept on the CPU; the shader only sees small film coordinates. Strip turns the Cell view,
  never reflects it.
- **Pictures across a frame line add, they don't mix in turn**: two frames never overlap.
- **The gate's frame in Cell is live**, every frame above it held. On a strip a frame is
  exposed when it first comes into view. The held ring is a texture array; a resize
  carries every held frame across by its index.
- **No absolute time in GLSL.** The film clock, phases and the transport are double on the
  CPU; the shader gets relative frame numbers and phases reduced mod 2π.
- `Perturb` bits are test hooks, always 0 in the plugins.
- Parameter names must be unique -- `--set` and the sweep find them by name.
- `SetParamInfo` clamps a STANDARD default into 0..1 before `SetParamRange`, so every
  slider is 0..1 and `Controls.cpp` holds the units, with inverses. Options by index.
- Override `SetTextParameter` to return FF_SUCCESS for the About block, or no host can
  instantiate the plugin.
- `gauge_core` is an OBJECT library, not STATIC; `EffectPlugin.cpp` and `SourcePlugin.cpp`
  each sit in their own MODULE target, or both plugins register into both bundles.
- `FFGLScopedFBOBinding.h` is not in the umbrella header; include `<ffglex/FFGLScopedFBOBinding.h>`.
- macOS build must be universal. Verify with `lipo`, never the build log.
- **Generated, do not edit here:** `source/StoatworksAbout.h` (stoatworks-backend's
  `sync-about.py`, from the website's `projects.json`: User guide, Project page, Source on
  GitHub, Support the work); `ATTRIBUTIONS.md`, `.github/FUNDING.yml` and the issue forms
  (the backend's sync scripts, once the repo is public); the README's downloads block
  (`gen-downloads.py`, after the tag).
- `docs/USER-GUIDE.md` is the user guide, published at
  `stoatworks-labs.com/software/gauge/guide/`. Keep it in step with the controls.

## Not done yet
- **Never loaded into Resolume on macOS.** `oxbow` loads both bundles.
- Windows: gated in Arena 7.27.1 on win-lab (llvmpipe, no sound): 15 passed, audio skipped; effect 24 live,
  leader 20 live + Edge Print, Shutter inconclusive; plugin-bench
  `arena/expect/gauge.json`.
- Seen on a test card and Resolume's demo clips through `--pipe`, never on camera footage.
- No browser demo, no OpenFX port, no presets.
- `.github/workflows/` are adapted and hadn't run before v0.1.0. They are for a PUBLIC repo
  only, which this now is (`github.com/stoatworks-labs/gauge`).

## Diagnostics

`source/Diag.{h,cpp}` -- log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/gauge/gauge.YYYY-MM-DD.log        (macOS)
    %LOCALAPPDATA%\gauge\logs\gauge.YYYY-MM-DD.log   (Windows)

`GAUGE_LOG_DIR` overrides the folder.
