#!/usr/bin/env python3
"""Every control must actually change the picture -- in both plugins.

A GLSL uniform whose name does not match the C++ is ignored without a word:
glGetUniformLocation returns -1 and glUniform on -1 is a documented no-op. So a
slider can be wired to nothing while the plugin compiles, loads and renders
perfectly. This renders each parameter at both ends of its range against the
same test card and reports any that made no difference at all.

    python3 tools/sweep.py [--binary build/gutest] [--size WxH] [--jobs N]

Exit code 1 means something is dead.

------------------------------------------------------------------ the traps

**Most controls are conditional, and each says what has to be true.** Rack,
Drift and Framing belong to Cell; Size, Angle, Position, Travel and Shutter to
Strip; Shutter only blurs a strip that moves more than a pixel a frame. The
leader's controls need a leader rolling, and Pop Frame needs it to have reached
its 2. The table below is the honest list: empty it once and see what goes dead.

**Keys beginning with "_" are harness settings, not plugin parameters:**
  _frames     how many host frames to render (default 40)
  _tone       an FFT bin and magnitude to feed, "K=M"
  _low/_high  the two positions to compare, when not the range's ends

**Events are pressed by setting them to 1 before the first frame**, which is a
rising edge, so the press lands on frame 0.

**An option's range is its element count.** `gutest --list` prints it.

**Never sweep the About block.** Those are buttons that open a web browser.
"""
import argparse
import concurrent.futures
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent

WIDTH, HEIGHT = 320, 180
FRAMES = 40

STRIP = {"Mode": "1"}
LEADER = {"Roll Leader": "1"}
# Host frame 363 at 60 Hz has film frame 145 in the gate, and a leader rolled
# on frame 0 starts at film frame 1: so that is the 2.
AT_TWO = {"Roll Leader": "1", "_frames": "364"}

CONTEXT = {
    "effect": {
        # Cell controls are live on the default Cell. Framing's far end shows
        # the film's edges; Rack and Drift move the frame line.
        # A label frame (every 16th) in the gate, for the edge print.
        "Edge Print": {"_frames": "41"},
        # The track carries a tone.
        "Track Level": {"_tone": "3=0.5"},
        # Strip controls.
        "Size": STRIP,
        # Angle's two ends are the same turn: sweep 0 against 90 degrees.
        "Angle": {"Mode": "1", "_low": "0.5", "_high": "0.75"},
        "Position": STRIP,
        "Travel": STRIP,
        # A strip at 24 fps moves 12 px a frame here; at 2 fps, less than one.
        "Shutter": {"Mode": "1", "Travel": "1"},
        # Held against live shows on a strip, whose frames came in at
        # different times; a Cell's neighbours are slivers of a still card.
        "Frames": STRIP,
        # The leader.
        "Start On": LEADER,
        "Pop Frame": AT_TWO,
        # A flash on film frame 1 is in the gate on host frame 3.
        "Flash": {"_frames": "4"},
        # A beat at 120 bpm lands on host frame 30, a bar not until 120: Off
        # against Beat.
        "Auto Flash": {"_frames": "31", "_high": "1"},
        # The surround shows past the film's edges at the widest framing.
        "Surround": {"Framing": "1"},
    },
    "source": {
        "Edge Print": {"_frames": "41"},
        "Size": STRIP,
        "Angle": {"Mode": "1", "_low": "0.5", "_high": "0.75"},
        "Position": STRIP,
        "Travel": STRIP,
        "Shutter": {"Mode": "1", "Travel": "1"},
        # The loop and what comes after it differ once the first pass is over:
        # 192 film frames is 480 host frames at 60 Hz.
        "Loop": {"_frames": "500"},
        "After": {"Loop": "0", "_frames": "500"},
        # Roll Leader restarts the leader from PICTURE START.
        "Roll Leader": {"_frames": "100"},
        "Start On": {"Roll Leader": "1", "_frames": "100"},
        # The source's leader starts on frame 0, so its 2 is at film frame 144:
        # host frame 360.
        "Pop Frame": {"_frames": "361"},
        "Flash": {"_frames": "4"},
        "Auto Flash": {"_frames": "31", "_high": "1"},
        "Surround": {"Framing": "1"},
    },
}


def parameters(binary, plugin):
    """id, name, kind, low, high from the harness's own declaration."""
    out = subprocess.run([binary, "--plugin", plugin, "--list"], capture_output=True, text=True)
    if out.returncode != 0:
        print("could not list parameters:", out.stdout, out.stderr)
        sys.exit(1)
    found = []
    for line in out.stdout.splitlines():
        m = re.match(r"\s*(\d+)\s+(.+?)\s{2,}(\S+)\s+([\d.eE+-]+)\s+\[\s*([\d.eE+-]+)\s*\.\.\s*([\d.eE+-]+)\s*\]", line)
        if m:
            found.append((int(m.group(1)), m.group(2).strip(), m.group(3), float(m.group(5)), float(m.group(6))))
    return found


def render(binary, plugin, path, overrides):
    frames = overrides.get("_frames", str(FRAMES))
    args = [binary, "--plugin", plugin, "--out", path, "--size", f"{WIDTH}x{HEIGHT}", "--frames", frames]
    if "_tone" in overrides:
        args += ["--tone", overrides["_tone"]]
    for name, value in overrides.items():
        if not name.startswith("_"):
            args += ["--set", f"{name}={value}"]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        print("render failed:", " ".join(args), r.stdout, r.stderr)
        sys.exit(1)
    return pathlib.Path(path).read_bytes()


def pixels(png):
    """Raw RGBA out of the harness's own PNG (filter 0 rows)."""
    i = 8
    idat = b""
    width = height = 0
    while i < len(png):
        length = int.from_bytes(png[i:i + 4], "big")
        kind = png[i + 4:i + 8]
        data = png[i + 8:i + 8 + length]
        if kind == b"IHDR":
            width = int.from_bytes(data[0:4], "big")
            height = int.from_bytes(data[4:8], "big")
        elif kind == b"IDAT":
            idat += data
        i += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray()
    for row in range(height):
        out += raw[row * (stride + 1) + 1:(row + 1) * (stride + 1)]
    return out


def difference(a, b):
    pa, pb = pixels(a), pixels(b)
    if len(pa) != len(pb):
        return 1.0, len(pa)
    changed = sum(1 for x, y in zip(pa, pb) if x != y)
    return changed / max(len(pa), 1), changed


def sweep_one(job):
    binary, scratch, plugin, pid, name, low, high, context = job
    lo = dict(context)
    hi = dict(context)
    lo[name] = context.get("_low", f"{low:g}")
    hi[name] = context.get("_high", f"{high:g}")
    a = render(binary, plugin, f"{scratch}/{plugin}_{pid}_lo.png", lo)
    b = render(binary, plugin, f"{scratch}/{plugin}_{pid}_hi.png", hi)
    fraction, count = difference(a, b)
    print(f"  swept {plugin} {pid:3d} {name}", file=sys.stderr, flush=True)
    return plugin, pid, name, fraction, count


def main():
    global WIDTH, HEIGHT
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", default=str(ROOT / "build" / "gutest"))
    ap.add_argument("--size", default="%dx%d" % (WIDTH, HEIGHT))
    ap.add_argument("--jobs", type=int, default=0)
    args = ap.parse_args()
    if "x" in args.size:
        WIDTH, HEIGHT = (int(v) for v in args.size.split("x", 1))
    jobs = args.jobs or min(8, os.cpu_count() or 1)

    binary = str(pathlib.Path(args.binary).resolve())
    if not pathlib.Path(binary).exists():
        print(f"{binary} is not built")
        return 1
    scratch = tempfile.mkdtemp(prefix="gusweep")

    skipped = []
    work = []
    for plugin in ("effect", "source"):
        for pid, name, kind, low, high in parameters(binary, plugin):
            if kind == "about":
                skipped.append((plugin, name, "a button that opens a web browser"))
                continue
            if kind in ("buffer", "text"):
                skipped.append((plugin, name, "the host's FFT input: Track Level sweeps what it does"))
                continue
            work.append((binary, scratch, plugin, pid, name, low, high, CONTEXT[plugin].get(name, {})))

    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        results = list(pool.map(sweep_one, work))

    dead = []
    for plugin, pid, name, fraction, count in sorted(results):
        if count == 0:
            dead.append(f"{plugin}:{name}")
            print(f"DEAD  {plugin:6s} {pid:3d}  {name}")
        else:
            print(f"ok    {plugin:6s} {pid:3d}  {name}  ({count} subpixels, {fraction * 100:.2f}%)")
    print()
    for plugin, name, why in skipped:
        print(f"skip  {plugin:6s} {name}: {why}")
    print(f"\n{len(results)} swept, {len(dead)} dead, {len(skipped)} skipped, {jobs} at a time")
    if dead:
        print("\nDEAD CONTROLS: " + ", ".join(dead))
        print("either the uniform name does not match the shader, or the sweep")
        print("needs a CONTEXT entry saying what else has to be true.")
        return 1
    print(f"all {len(results)} swept parameters measurably change the picture")
    return 0


if __name__ == "__main__":
    sys.exit(main())
