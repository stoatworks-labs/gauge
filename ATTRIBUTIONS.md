# Attributions

Gauge is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

A provisional hand copy, in the shape `stoatworks-backend`'s
`scripts/sync-attributions.py` generates: gauge is not on the master lists yet. The
first sync after it is registered replaces this file; edit the lists there then, not
here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### 5x7 bitmap font — Stoatworks graticule

<https://github.com/stoatworks-labs/graticule>  
Licence: MIT  
Copyright: Stoatworks Labs

The 5x7 bitmap font the edge print is spelt out of is graticule's, copied unchanged.

### The stroked face and its distance-field atlas — Stoatworks jackpot (digits from polyhedral)

<https://github.com/stoatworks-labs/jackpot>  
Licence: MIT  
Copyright: Stoatworks Labs

The leader's numerals and PICTURE START are jackpot's built-in stroked face, its exact distance-field builder and its cell layout, copied with the font-file loading left behind. Its digits are polyhedral's.

### Transport — Stoatworks jackpot

<https://github.com/stoatworks-labs/jackpot>  
Licence: MIT  
Copyright: Stoatworks Labs

Resolume's tempo and bar phase, re-aligned to the host whenever it says, is jackpot's Transport with a bar count added.

### Harness shape, --pipe contract and verify — Stoatworks pitch and rebate

<https://github.com/stoatworks-labs/rebate>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness shape, the --pipe contract, the verify script and the negative-control pattern are pitch's by way of rebate; the host clock-unit voting is readout's.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl.

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### The SMPTE Universal Leader

The countdown is after the leader SMPTE introduced in the 1960s (now ANSI/SMPTE 55, the Television Leader): PICTURE START, eight to two a second apart in a target of two circles with a sweeping hand, the C C F F control frames round the four, the 2 for a single frame, black to the picture. Drawn from that description; no leader's artwork was copied, and the greys, the line weights and the typeface are this project's own.

### The soundtracks of a late release print

Analogue variable-area, DTS time code, Dolby Digital and SDDS are drawn where a print carries them, as data: hashed bits and dashes. No logo of any of them is drawn, and nothing in the plugin decodes or encodes any of their formats.

## Standards and published specifications

What the implementation is measured against.

- **SMPTE 139 / ISO 491, 35 mm film perforations** — the KS hole, its pitch and its edge distance; the frame line midway between two holes.
- **SMPTE 59 and the sound-print apertures, SMPTE 40 (the photographic sound record)** — the Academy and anamorphic apertures, and where the analogue track sits.
- **ISO 69 / SMPTE 109, 16 mm film** — the hole, its pitch and edge distance; the frame line through the hole; the camera aperture.
- **SMPTE 149 and 157, Super 8** — the hole, its pitch and edge distance, and the camera aperture.
- **Melissa E. O'Neill, "PCG: A Family of Simple Fast Space-Efficient Statistically Good Algorithms for Random Number Generation" (Harvey Mudd College, 2014)** — the pcg_hash output mix behind the data tracks' bits, written out rather than copied from anyone's source.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
