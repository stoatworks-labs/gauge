# AGENTS.md — Gauge

Onboarding for whoever (or whatever) picks this up next. `CLAUDE.md` is the short
command reference; this is the *why*. Read "What is actually verified" before you tell
anybody this works.

---

## What the plugin is

The film strip itself, as two FFGL 2.1 plugins for Resolume Arena and Avenue from one
class (`GaugePlugin`): **SW Gauge** (`GU01`, effect) prints the clip onto the strip;
**SW Gauge Leader** (`GU02`, source) runs a countdown leader on it. C++17 + GLSL 4.10,
CMake, universal macOS bundles and Windows DLLs. MIT. Public at
`github.com/stoatworks-labs/gauge` from v0.1.0 (2026-10-14), with its project page at
`stoatworks-labs.com/software/gauge/` and the user guide (`docs/USER-GUIDE.md`) at
`stoatworks-labs.com/software/gauge/guide/`.

Asked for by Allan on 2026-10-12: "emulates the actual film strip, so it adds
perforations, frame lines, borders etc … an anamorphic style squeeze … a full screen
film strip/cell, or as a strip in motion as an overlay … versions that do a vintage
style countdown and sync flash too". Not grain. Built 2026-10-13 from
`~/Projects/resolume/specs/SPEC-gauge.md`, in one session, with rebate (the strip, the
harness), jackpot (a source and an effect from one core, `Transport`, the stroked face),
gate (held pictures across a resize) and regauss (the FFT input) as templates.

---

## The one idea

**A film strip is a standard.** Every gauge's holes, pitch, apertures and soundtrack
areas are published dimensions; a strip is a sequence of frames that each hold one
exposure. So:

1. **The strip** (`Strip.cpp`) is numbers in film millimetres: x across from the
   reference (guided) edge, y along toward the tail, frame k occupying [ kP, ( k + 1 ) P ).
2. **A camera** (`Camera.cpp`) maps each output pixel to film millimetres: Cell looks at
   one frame upright, scanned; Strip lays the film across the output at an angle, turned
   (never reflected) from the Cell view.
3. **The frames** each hold one exposure: in Cell the frame in the gate is exposed every
   host frame until it is pulled down; on a Strip a frame is exposed the first time it
   comes into view. Held pictures live in a ring of texture-array layers
   (`HeldFrames.cpp`), slot i mod N, tagged with i.
4. **The leader and the flashes are spliced onto the film**: frame i of the strip is a
   leader frame or a flash before it is a picture.
5. **The composite** (`Shaders.cpp`) evaluates every output pixel on the film: holes
   (a rounded-rectangle distance), the rebate, the pictures (box-filtered coverage of
   each frame's aperture), the soundtracks, the edge print, averaged over up to sixteen
   positions of the shutter for motion blur.

| what is modelled | what comes out |
| --- | --- |
| holes at the published size, pitch and edge distance, in rows per format | **perforations** where the standards put them |
| the camera aperture's height against the frame pitch | Academy's 3 mm **frame line**, Scope's 0.4 mm, Super 8's 0.014 mm |
| the picture exposed through an anamorphic lens: display aspect = aperture aspect × squeeze | the **squeeze**: round is tall and thin on the film, the leader included |
| a frame keeps the picture it was exposed with | a moving strip is **a contact strip of the clip's past**; the frame above the gate is the previous one |
| the track's clear width is ( 1 + s ) / 2 of the scanned width, s the sound at the film's speed | the **optical track wiggles with the music**, finer for higher notes |
| the leader as film frames from PICTURE START, 24 to a count | the **countdown** at the film's rate: eight seconds, or eight beats at Rate Beat |
| a flash is a frame exposed fully | a **sync flash** one film frame long |

### What is not modelled

Grain, curves and dyes (rebate's), the projector's shutter, weave, dust and fading
(gate's). Splices and damage. The sound advance (a 35 mm print carries its sound ~21
frames ahead of the picture; here it is level, because the clip's future has not
happened). 70 mm, VistaVision, 2-perf and 3-perf. The Academy leader (11 to 3 in feet);
only the SMPTE Universal style. A negative stock: rebate does negatives properly.

---

## The shape of the code

| File | What it is |
| --- | --- |
| `source/Strip.{h,cpp}` | Every format as numbers, with where each came from. |
| `source/Camera.{h,cpp}` | Output pixel → film mm for Cell and Strip, the base frame kept on the CPU. |
| `source/HeldFrames.{h,cpp}` | The ring of held pictures: a GL_TEXTURE_2D_ARRAY, tags, reallocation that carries frames across. |
| `source/Gauge.{h,cpp}` | The plugin: parameters (two lists from one enum), the clock, the film position, the leader and flash scheduling, captures, the composite's uniforms. |
| `source/EffectPlugin.cpp`, `source/SourcePlugin.cpp` | One `CFFGLPluginInfo` each, in their own MODULE targets. |
| `source/Shaders.{h,cpp}` | capture, composite (four pieces: head, leader, tracks, main) and copy. |
| `source/Lettering.{h,cpp}` | jackpot's stroked face and its distance-field atlas, for the leader. |
| `source/Font.{h,cpp}` | graticule's 5x7 font, for the edge print. |
| `source/Transport.h` | jackpot's transport, with a bar count. |
| `source/Controls.{h,cpp}` | What a 0..1 slider means, with inverses; the option lists. |
| `source/Diag.{h,cpp}` | A log file, for the shader that will not compile. |
| `tools/gutest/` | The harness: `main.cpp` (sessions, sources, plumbing), `geometry.inc`, `time.inc`, `signal.inc`, `checks.inc`. |
| `tools/sweep.py` | No control of either plugin is silently dead. |
| `tools/verify.sh` | All of it, on a fresh universal build. |

Per host frame: the clock and the film position (double, CPU); presses and the beats they
wait for; the camera and the visible frames (through the whole shutter); the ring
(re)allocated; captures (the clip, fitted and squeezed, into held slots); the content
table (kind, value, spectrum row per visible frame, an RGBA32I texture); the composite,
straight to the host.

---

## Traps

Roughly in the order they cost time.

### ☠️ A box-filtered edge's centre is not the coverage-weighted mean of pixel centres

`--perfs` first put every 35 mm hole 0.007 px toward the film's middle at 320×180 (and
fifteen times less at 1280×720), with the holes' sizes exact. The plugin was right; the
harness measured a profile's centre as Σ c ( i + 0.5 ) / Σ c, which puts a partly covered
end pixel's ink at that pixel's centre when it is really against the covered side. The
LENGTH of an interval is the sum of its coverage, exactly; its ENDS are exact from the end
pixels (i0 + 1 − c( i0 ) and i1 + c( i1 )). `extentOf` does that and reports whether the
pixels between are solid. The same bias sits in any 2D centroid of a box-filtered shape;
nothing here uses one any more.

### ☠️ Two pictures across a frame line must be added, not mixed one after the other

The composite drew a frame's picture with `mix( col, pic, cov )` and then the neighbour's
across the frame line the same way. They never overlap, so the second mix scaled the
first's share down by its own coverage: on Super 8, whose frame line is 0.014 mm, every
pixel on a frame line lost picture, and `--aperture` read Super 8's picture 0.12 px short.
Now each frame adds `pic × cov` to a sum and its coverage to a total, and the base fills
what is left. A Clear frame adds coverage and no colour, and only the alpha loses it (it
was losing it twice).

### ☠️ The frame in the gate must be live

`--leader` read the clip at 0.698039 (178/255) instead of 0.7: in Cell with Frames Held,
the gate's frame is captured every host frame, so the ring held it and the content table
drew it from the 8-bit copy. The frame still being exposed, and every one below it that
has not been, now show the clip live at full precision; only frames pulled down past the
gate are held pictures.

### ☠️ The strip view reflected the film

The first Strip camera put film along on the output's x and across on its y. For the
still format that is the Cell view; for a motion picture format it swaps the Cell view's
axes, which is a reflection, and every frame on a horizontal strip was mirrored. Found by
eye on the test card's colour patches, before any check existed; `Camera::Strip` now turns
the Cell view a quarter turn, and a vertical Super 8 strip reads the card's patches in
order.

### Float noise is not coverage

`--perfs` failed seven formats of eight as "not solid": a flat field box-filtered over
three taps came back an ULP off its level, which read as 2e-9 of a pixel of coverage and
broke the run of fully covered pixels. Coverage under 1e-6 is now ignored at the ends,
which moves an end by less than that.

### The squeeze check measured the neighbours, and then the GPU's filter

Three things, one after another. In Cell with Frames Live every visible frame shows the
clip, so slivers of the disc above and below joined the moments: the check now reads only
the gate's picture (its box from the camera). A disc of 0.3 of the clip's height did not
fit inside a 2× crop of Super 16 or the still roll: its radius is now 0.35 of the crop's
shorter side. And at 1280×720 the ratio was 3e-5 off with a blur band of 1e-5: the
bilinear tap is a tent of variance 1/6 texel² (not just a box), the test disc was
supersampled 16×16 (now each edge pixel holds its exact area, integrated), and this
GPU's filter weights come in 1/256 steps (fiche's trap), which can move an edge pixel by
half a step. The band now carries all three, each derived.

### The crosshair is not spread evenly round the circle

The leader's swept fraction is read in an annulus with the crosshair's bands left out.
The bands sit at 0°, 90°, 180° and 270°, so leaving them out does not take an even share
from the swept and unswept parts, and k / 24 was 0.01 off. The expected fraction is now
counted from the same pixels' centres.

### A leader carries no programme sound

The source had Audio and Track Level; the sweep found Track Level dead there, because a
leader frame's track is silent but for the pop on the 2. They are gone from the source.

### Edge print once a foot blinks

A key number once a foot meant a registered Cell showed edge print on one frame in
sixteen: a 1/24 s blink every two-thirds of a second. The stock's code now prints on every
frame and the number joins it on the foot.

### Inherited from the fleet, and all still true here

`ScopedFBOBinding` does not restore the viewport (the host's is captured first and put
back before the composite); every `ffglex::Scoped*` clears to 0 on exit, so the ring's
(re)allocation and its copies happen before anything is bound; `SetParamInfo` clamps a
STANDARD default into 0..1 before `SetParamRange`; the core is an OBJECT library;
`SetTextParameter` must return `FF_SUCCESS` for the About block; the harness drives a
synthetic clock and a declared clock unit; an option's range reads back 0..1; Resolume's
clock overflows a float, so the film clock, the strip's offset, the drift and every phase
are double on the CPU; the clip is premultiplied (jackpot's), so its rgb is the picture
over black; a source's sampler on texture 0 is "unloadable" to Apple's driver, so the
source binds a real texture it never reads.

---

## Would this hold on another rasteriser, at another raster?

Every check ran at 320×180 and 1280×720 in `verify.sh`, the geometry at 640×480 and
333×187 by hand, and every check on Apple's software renderer at 320×180 (`--software`).
Coverage is an analytic ramp evaluated per pixel (`ramp`, `span`, the hole's distance),
not a sample, so it is exact at any raster up to float; the tolerances below are float's.

| check | what it measures | tolerance and where it comes from | raster dependence |
| --- | --- | --- | --- |
| `--perfs` | hole extents from the ends of box-coverage profiles; scale from the film's width (alpha) in the same picture | **1e-3 px per edge** against the model: a film coordinate near 40 mm carries 4e-6 mm, ~3e-5 px; each coverage within 2 ULP; an edge sums two or three. Against the published table, **0.005 mm** more: the model rounds inch figures to 0.01 mm (2.01 for 0.079 in) | the hole's middle row must lie in its straight part: corner radius 0.508 mm, so the hole must be over ~3 px along; at 320×180 it is 7–10 px |
| `--perfs` frame line | a hole centre's distance past the frame line, in pitches | three centres (two pictures and a hole), 2 edges each, over the pitch | as above |
| `--aperture` | picture extents and centre the same way, from two flashed frames isolated by neighbours at the rebate's red | 2 edges a length, 4 a centre | none |
| `--squeeze` | second moments of a disc, round as seen, inside the gate's picture | the ratio's band from blur per axis: 2/12 px² (the clip sampled over a pixel, and the pixel) + 3/12 texel² (the disc's own box and the bilinear tent) + E, E = perimeter × 1/512 × 3σ² / Σ (filter weights in 1/256 steps, all one way) | the band is computed from the raster |
| `--held` | each visible frame's level against the host frame it came into view on; a picture's left edge from one pixel's coverage at two times | levels: half an RGBA8 step (neighbouring levels are 2.1 steps apart); motion: 2 × 1e-3 px | none |
| `--leader` | per frame: rings at r = 0.36, the swept fraction of an annulus, the number by template against the atlas, the mean of a black frame or the clip | sweep: the hand's black line across the annulus and half a pixel either side of each radial edge, over the annulus (0.0088 at 320×180, 0.0046 at 1280×720; the worst read was 0.0040 and 0.0020); number: agreement > 0.97 and the runner-up 0.05 behind; means 1e-6 | the template is drawn at the picture's own pixel size |
| `--flash` | the gate's picture centre equal to 1.0, frame by frame | exact: a flash is `vec3( 1.0 )` | none |
| `--track` | each row's clear width across the track, s recovered, a least-squares sine at the predicted frequency | amplitude and residual 1e-3 of full modulation: GLSL gives sin no accuracy at all; a wrong frequency leaves ~0.18 | rows are sampled at their centres, exactly as drawn |
| `--alpha` | alpha at chosen points | exact 0 and 1: `mix( a, 0, 1 )` is 0 either way it is evaluated, and film alpha is never mixed | none |
| `--resize` | levels before and after a resize, and the ring's size | the same level; the ring must have changed size | the check doubles whatever raster it is given |

Deliberately NOT relied on: a coverage-weighted mean as an edge's centre, 2D centroids of
box-filtered shapes, `sin` accuracy, and bit-exact filtering (the squeeze band carries the
GPU's 1/256 weight steps).

### The negative controls

`gutest --negative` runs ten; each perturbs the *plugin's* model through a `Perturb` bit
the shipped plugins carry at zero, never the harness's expectation.

| perturbation | caught by |
| --- | --- |
| a hole's across and along swapped | `--perfs` |
| 35 mm's frame line through a hole (16 mm's rule) | `--perfs` |
| the sound prints' picture centred on the film | `--aperture` |
| the squeeze ignored | `--squeeze` |
| every frame on a strip re-exposed every host frame | `--held` |
| 25 frames to a leader count | `--leader` |
| a flash two frames long | `--flash` |
| the track drawn at 25 fps whatever the rate (amplitude found 0.009, residual 0.18) | `--track` |
| clear holes drawn opaque | `--alpha` |
| a resize that forgets the held frames | `--resize` |

### The mutation

On a clean committed tree (2026-10-13, at `b09385c`): in `holeCoverage`,
`HoleGrid.y + jh * HoleGrid.x` → `HoleGrid.y - jh * HoleGrid.x`, built in its own
directory. Caught by `--perfs`, `--aperture` and `--alpha`; `--squeeze`, `--held`,
`--leader`, `--flash`, `--track` and `--resize` passed, correctly (they do not look at
where the holes are). Reverted with `git checkout`; the tree was clean before and after.

### The sweep's context, audited

`python3 tools/sweep.py --no-context` sweeps every control on the defaults alone. It found
24 dead, and they are exactly the ones `CONTEXT` names: the Strip controls and Frames need
Mode Strip, Shutter a strip moving faster than a pixel a frame, Track Level a tone, the
leader's controls a leader rolling (Pop Frame one that has reached its 2), Flash and Auto
Flash the right frame in the gate, the source's Loop and After a leader that has finished
once. Angle's two ends are the same turn, so it sweeps 0° against 90°.

---

## Decisions taken without asking

- **The name** is `gauge` (`SW Gauge`, `SW Gauge Leader`): the gauge is what sets the
  strip. `reel` was taken in spirit (relay is RL01, and "reel" is a social-media word all
  over the release tooling); `leader` named only half of it.
- **One effect and one source.** The countdown and the flash are on both: the effect
  splices a leader in front of the clip (Roll Leader), the source runs it on its own,
  looping by default.
- **Format is one list** of gauge-and-aperture: 35 mm Academy (a sound print: projection
  width, camera height, analogue track and DTS), 35 mm Scope (2×, all four soundtracks),
  35 mm Full (silent), 35 mm Still (135, the picture across the film), 16 mm (double
  perf), 16 mm Sound, Super 16, Super 8. **Squeeze** defaults to the format's own.
- **Super 8's hole is beside the middle of its frame.** SMPTE 157 places the picture by
  the hole two pitches on; the arithmetic puts the adjacent hole level with the frame's
  middle, and I put it exactly there.
- **Placed by eye**, in the space the standards leave: DTS at 7.55–8.10 mm, Dolby Digital
  blocks 2.2 mm square between the near holes with a clear middle, SDDS at 0.20–1.75 mm
  from each edge, the 16 mm optical track at 13.45–15.25 mm, the edge print in the far
  margin (the near one where a format's track has the far).
- **The stocks** are Colour Print (black rebate, a cyan-dye track, light edge print), B&W
  Print (the picture in luma) and Clear Base (unexposed print on a light box). Their codes,
  GP 7712, GB 7302 and GC 7015, are invented.
- **Cell's Framing** runs from the picture filling the output to the film's width and a
  quarter, log-interpolated, centred on the picture at one end and the film at the other;
  0.7 shows a 35 mm frame with its holes and track on a 16:9 output.
- **Drift is a rolling frame line**, wrapped to a frame, not the film slipping away from
  the gate: the frames in view stay the gate's.
- **The future is the present.** Frames below the gate in Cell have not been exposed, and
  show the clip as it is now (on real film they would be the next frames; nothing here can
  know them).
- **Held frames are RGBA8**, up to a 40-megapixel budget across all slots, sized to the
  camera's scale with hysteresis (grow at 19%, shrink at 68%), grown in depth and never
  shrunk. The gate's frame and Live frames sample the clip at full precision.
- **Rate Beat is 24 frames a beat**, the film clock taken straight from the transport's
  beat count, so a count is a beat, PICTURE START on a bar puts the picture on a bar two
  later, and a looping source's leader starts on even bars.
- **Start On** waits for the next beat or bar (one the frame sits on counts) and lands on
  the frame that boundary starts; Press takes the next frame (Cell) or the next to come
  into view (Strip). It governs Flash too.
- **The leader** is the SMPTE Universal style only: PICTURE START at frame 0, the counts
  8 to 3 for 24 frames each (8 has 23, after PICTURE START), the 2 on frame 144, black to
  191, picture at 192. The greys are 0.62 and 0.30, the rings at 0.30 and 0.36 of the
  picture's height, the numeral 0.40 high and 0.04 bolder than the face. **Pop Frame
  Flash** whitens the 2 instead of drawing it.
- **The track**: bins assumed linear to 22,050 Hz (nobody has measured Resolume's 64),
  sqrt of each magnitude as regauss does, each bin a sine with a fixed phase and a phase
  that advances with the film, left out where it is finer than half a pixel. Scope's two
  channels carry the same mono signal.
- **Output alpha**: film is opaque (a transparent clip prints as black film); holes and
  the surround are what Holes and Surround say; Mix blends premultiplied.
- **No factory presets**, no Fit for the source, no Frames for the source.
- **StoatworksAbout.h is generated** by stoatworks-backend's `sync-about.py` from the
  website's `projects.json`: four buttons, User guide, Project page, Source on GitHub and
  Support the work. It started as a hand copy with the first two left out, as graticule's
  did, until gauge was registered. `ATTRIBUTIONS.md`, `.github/FUNDING.yml` and the issue
  forms come from the backend's sync scripts once the repo is public. Edit none of them
  here.
- **The commit trailer** is `Claude Opus 5.5`, the model that wrote it, where BRIEF.md's
  template says Fable 5.1.

---

## What is actually verified, and what is assumed

### Verified by measurement, on an M4 Max running macOS 26.4.1 (2026-10-13)

`tools/verify.sh` against a fresh universal Release build, every check at 320×180 and
1280×720: see the README's Status table for the numbers. Every check also passes on
Apple's software renderer at 320×180 (`--software`), at several minutes a check.
Both bundles are universal, export `_plugMain`, carry their identifiers and ad-hoc sign;
`oxbow` reports `SW Gauge` / `GU01` / effect and `SW Gauge Leader` / `GU02` / source and
renders 120 frames through each. All 50 swept controls change the picture: every one of
the 51 declared but the Audio input, the host's FFT, which Track Level sweeps.

### Assumed, or not done

- ☠️ **Never loaded into Resolume on macOS.** Nothing in a host on a Mac has seen the
  parameters, the Audio input, the beat info or the event buttons.

- **Windows: gated in Resolume Arena 7.27.1 on win-lab** (2026-10-14; MSVC x64; Mesa
  llvmpipe, no GPU, no sound device): 15 passed, 0 failed, 1 skipped (audio). Every
  control as declared; the effect's 24 testable controls all live (7 under a
  precondition); the leader's 20 live (7 under a precondition) in run 1 and 17 in the formal
  run 2 on the release build, the rest inconclusive against its moving countdown (Edge Print
  and Shutter in both; Squeeze, Soundtracks and Travel in run 2 only); none dead. The expectation is plugin-bench
  `arena/expect/gauge.json`. Audio, Track Level and the event buttons were never fed or
  pressed there.

- **The workflows** are adapted from rebate and jackpot and hadn't run before v0.1.0.
  They are for a public repo only, which this now is.
- **Resolume's FFT bins and beat info** are the fleet's assumptions (jackpot's transport);
  Start On Beat/Bar and Rate Beat have met only the harness's exact bar phase.
- **The placements by eye** (above) are not measured against anything.
- **Seen on** a synthetic card and Resolume's bundled demo clips, never camera footage.
- No browser demo, OpenFX port or presets. The user guide is `docs/USER-GUIDE.md`; keep it
  in step with the controls.

---

## Open questions

- **Should Drift slip the film past the gate** (a projector losing its loop) instead of
  rolling the frame line? The roll keeps the picture current; a slip would show old frames.
- **A Strip with Travel 0 never brings a new frame into view**, so Roll Leader and Flash
  on a stopped strip wait for it to move. That is what stopped film does; it may not be
  what an operator expects.
- **Real programme sound on the leader's own track?** The source has none; a "sound
  check" tone on the counts would be invented.
- **70 mm, VistaVision and 2-perf** would be more rows in `Strip.cpp` and the published
  table; the Academy leader (11 to 3 in feet, upside down for the projectionist) another
  leader style.

---

## Siblings

- **rebate** — the emulsion and its scan; the harness shape, the 5x7 font by way of
  graticule, the strip it draws as a backdrop. Its KS holes were turned through 90° when
  gauge was built (the 2.794 mm dimension ran along the strip); rebate fixed them on
  2026-10-13 (`6a69209`), so the two strips agree now.
- **gate** — the projector: put it after gauge for flicker, weave and dirt.
- **jackpot** — a source and an effect from one class, `Transport`, the stroked face.
- **regauss** — the FFT input and the sqrt of its bins.
- **fiche** — the 1/256 filter-weight trap.
- **oxbow** — `oxbow probe` and `oxbow selftest` load both bundles as a host.
