/**
 * Gauge — browser demo.
 *
 * The film strip itself (SW Gauge, GU01, an effect; SW Gauge Leader, GU02, a
 * source). The one idea, from AGENTS.md: a film strip is a standard. Every
 * gauge's holes, pitch, apertures and soundtrack areas are published
 * dimensions, so the strip is built in film millimetres from them, a camera
 * looks at it, and each frame on it is exposed once and keeps that picture as
 * it travels. Perforations, frame lines, the anamorphic squeeze, a moving
 * strip as a contact strip of the clip's past, the countdown and the sync
 * flash all fall out of that; none of it is drawn at a place on the screen.
 *
 * ---------------------------------------------------------------------------
 * What runs here, and what does not
 * ---------------------------------------------------------------------------
 *
 * **This page does not port the plugin. It runs it.** `gauge-core.wasm` is the
 * plugin's own C++ compiled UNMODIFIED with emscripten
 * (demo/tools/build-wasm.sh): `source/Gauge.cpp` — the one class both plugins
 * are made from, with its constructor and both parameter lists, its clock and
 * seconds-or-milliseconds vote, its transport, the presses and the beats they
 * wait for, the leader and the flashes, the camera, the held frames, the
 * captures and every uniform of the composite — and everything it calls:
 * Strip (every format as numbers), Camera, HeldFrames (the ring of held
 * pictures, a GL_TEXTURE_2D_ARRAY), Controls, Font (the edge print's 5x7),
 * Lettering (the leader's stroked face, and the distance-field atlas it builds
 * from the strokes at start-up — the same atlas the plugin builds), Shaders
 * and Diag, with the FFGL SDK's CFFGLPlugin, its parameter bookkeeping,
 * FFGLShader, FFGLScreenQuad and the scoped bindings. Its GL calls go to this
 * page's WebGL2 context through emscripten's GL library.
 *
 * What is the page's and not the plugin's:
 *
 *   - **The host.** demo/wasm/glue.cpp constructs the class with the flag each
 *     plugin's registration passes (true for SW Gauge, false for SW Gauge
 *     Leader), reads its parameter declarations back through the SDK's host
 *     getters — the panel below is built from them, not from a list typed
 *     here — and forwards SetFloatParameter and, every frame, SetTime,
 *     SetBeatInfo and ProcessOpenGL, in the order tools/gutest's Session makes
 *     them. (The one text parameter, the About line, is display only and
 *     never sent.)
 *   - **The transport.** A browser has no Resolume transport, so the page
 *     keeps a BPM clock: the Host BPM field under the picture (120 at first,
 *     the SDK's default and the harness's), and a bar phase advanced from the
 *     page's own seconds, handed to SetBeatInfo every frame as Resolume hands
 *     over its own. Simulated, and said so on the page.
 *   - **No audio.** Audio is an FFT buffer Resolume fills every frame; nothing
 *     here writes it, so the optical track is a band of constant width and
 *     Track Level has nothing to scale. The leader's pop on the 2 is drawn by
 *     the composite, not heard, so it is there.
 *   - **One GL entry point** (demo/wasm/gl_shim.cpp). glShaderSource: the
 *     plugin hands GL desktop GLSL 4.10; `shaderSource()` below refuses any
 *     text that is not byte for byte one of this page's assemblies of its copy
 *     (shaders.js, held to source/Shaders.cpp and the order its stage
 *     functions and compileShaders() join the pieces in by
 *     demo/tools/check_shaders.py), then applies the kit's `port()` — the
 *     version line and the ES precision defaults, nothing else.
 *   - **The panel's read-outs.** The number beside a slider is Controls.cpp's
 *     conversion, compiled in (glue.cpp's gu_convert pairs each control with
 *     the function ProcessOpenGL applies to it); the units printed after it,
 *     and every tooltip, are this page's words.
 *   - **The presets** are this page's, from the user guide's Start here.
 *   - **The About block's links.** The plugin opens them with system(); the
 *     page opens the same addresses itself, read from the plugin's own table.
 *
 * ---------------------------------------------------------------------------
 * Decisions this page made, and why
 * ---------------------------------------------------------------------------
 *
 * **Both plugins, one page.** One class with a flag, so the Plugin dropdown
 * is a new instance, as it would be in Resolume: the film starts again and
 * every control goes back to its constructor's default. The panel is the two
 * declaration lists merged in order (each is a subsequence of the class's own
 * Kind order); a row the chosen plugin does not declare is hidden, and the
 * source's Surround, which declares three elements to the effect's four (no
 * Clip), hides the fourth. The merge is checked at start-up: a declaration
 * the two plugins disagree on stops the page rather than show either one's
 * wrongly. `?plugin=leader` opens the source; Copy link carries it.
 *
 * **Kit types for FFGL types.** The kit has no event or buffer control. Roll
 * Leader and Flash (FF_TYPE_EVENT) are buttons that send the press and hold
 * it through one frame, releasing it on the next, as Resolume sends an event;
 * they are on the panel where the plugin declares them, and repeated under
 * the picture. An About button sends the press and the release at once.
 * Audio (FF_TYPE_BUFFER, FF_USAGE_FFT) is shown where the plugin declares it,
 * read-only, saying there is no audio here. The About line (FF_TYPE_TEXT) is
 * shown read-only.
 *
 * **The transport is a bar phase, not only a BPM**, as zoetrope's page: the
 * plugin's Transport re-aligns to a host bar phase whenever one arrives, so
 * the page sends one, as gutest does. The phase is accumulated, so changing
 * the BPM changes how fast it runs from then on and never jumps it.
 */

import { mountDemo } from './vendor/demo.js';
import { port, GLError } from './vendor/gl.js';
import * as S from './shaders.js';
import createGauge from './gauge-core.js';

//===========================================================================
// FFGL.h's parameter types, by number: what GetParamType returns.
//===========================================================================

const FF_TYPE_BOOLEAN = 0;
const FF_TYPE_EVENT = 1;
const FF_TYPE_STANDARD = 10;
const FF_TYPE_OPTION = 11;
const FF_TYPE_BUFFER = 12;
const FF_TYPE_INTEGER = 13;
const FF_TYPE_TEXT = 100;

//===========================================================================
// The GLSL: the page's checked copy, assembled stage by stage in the order
// the plugin joins the pieces (shaders.js's ASSEMBLY, read out of Gauge.cpp's
// compileShaders() and Shaders.cpp's stage functions by check_shaders.py).
//===========================================================================

const ASSEMBLED = Object.fromEntries(
  Object.entries(S.ASSEMBLY).map(([stage, pieces]) => [stage, pieces.map((piece) => S[piece]).join('')]),
);
const FRAGMENT_STAGES = Object.keys(ASSEMBLED).filter((stage) => stage !== 'vertex');
const PAGE_SHADERS = {
  vertex: [ASSEMBLED.vertex],
  fragment: FRAGMENT_STAGES.map((stage) => ASSEMBLED[stage]),
};

/** Which stages the compiled plugin handed over, and whether each was ours. */
const shaderCheck = { matched: [], refused: null };

/**
 * Called by gl_shim.cpp's glShaderSource with the text the compiled plugin
 * handed to GL. Anything but one of this page's own assemblies is refused (an
 * empty source, so the plugin's InitGL fails and logs which pass) rather than
 * compiled.
 */
function shaderSource(stage, text) {
  const ours = PAGE_SHADERS[stage].indexOf(text);
  if (ours < 0) {
    shaderCheck.refused = { stage, length: text.length };
    return '';
  }
  shaderCheck.matched.push(stage === 'vertex' ? 'vertex' : FRAGMENT_STAGES[ours]);
  return port(text, { stage });
}

//===========================================================================
// The module.
//===========================================================================

let core;
try {
  core = await createGauge({
    gaugeShaderSource: shaderSource,
    print: (line) => console.log(line),
    printErr: (line) => console.warn(line),
  });
} catch (error) {
  const root = document.querySelector('#demo');
  root.textContent = `The demo could not start: this browser would not run its WebAssembly (${error.message}). The plugin, its source and its downloads are at https://github.com/stoatworks-labs/gauge.`;
  throw error;
}

const text = (pointer) => core.UTF8ToString(pointer);

// Diag.cpp opens its log in the plugin's constructor, so its directory must
// exist in the page's memory before the first instance is made.
core._gu_prepare_log();

//===========================================================================
// The declarations, read from both plugins' constructors through the SDK.
//===========================================================================

/** The plugin each variant is: the flag its registration passes. */
const VARIANTS = {
  effect: { flag: 1, name: 'SW Gauge', id: 'GU01' },
  source: { flag: 0, name: 'SW Gauge Leader', id: 'GU02' },
};

function declarations(flag) {
  const h = core._gu_new(flag);
  const list = [];
  const n = core._gu_param_count(h);
  for (let i = 0; i < n; i += 1) {
    const type = core._gu_param_type(h, i);
    const d = { index: i, name: text(core._gu_param_name(h, i)), type, group: text(core._gu_param_group(h, i)) };
    if (type === FF_TYPE_TEXT) d.default = text(core._gu_param_default_text(h, i));
    else d.default = core._gu_param_default(h, i);
    if (type === FF_TYPE_OPTION) {
      d.elements = [];
      d.values = [];
      for (let e = 0; e < core._gu_param_element_count(h, i); e += 1) {
        d.elements.push(text(core._gu_param_element_name(h, i, e)));
        d.values.push(core._gu_param_element_value(h, i, e));
      }
    }
    if (type === FF_TYPE_BUFFER) {
      d.bins = core._gu_param_element_count(h, i);
      d.usage = core._gu_param_usage(h, i);
    }
    list.push(d);
  }
  const aboutFirst = core._gu_about_first(h);
  const maxInputs = core._gu_max_inputs(h);
  core._gu_delete(h);
  return { list, aboutFirst, maxInputs };
}

const DECLARED = {
  effect: declarations(VARIANTS.effect.flag),
  source: declarations(VARIANTS.source.flag),
};
if (DECLARED.effect.maxInputs !== 1 || DECLARED.source.maxInputs !== 0) {
  throw new Error(`the effect takes ${DECLARED.effect.maxInputs} inputs and the source ${DECLARED.source.maxInputs}; expected 1 and 0`);
}

// The About block: the address each button opens, from the plugin's own table
// (StoatworksAboutLinks.h, compiled in). Both plugins carry the same block. A
// label that does not match the declaration is a page bug, not something to
// paper over.
const ABOUT_LINKS = new Map();
for (const variant of Object.keys(DECLARED)) {
  const { list, aboutFirst } = DECLARED[variant];
  for (const d of list) {
    if (d.index <= aboutFirst || d.type !== FF_TYPE_EVENT) continue;
    const button = d.index - aboutFirst - 1;
    const label = text(core._gu_about_button_label(button));
    if (label !== d.name) throw new Error(`About button ${button} is "${label}" in the plugin's table and "${d.name}" in ${variant}'s declaration`);
    ABOUT_LINKS.set(d.name, text(core._gu_about_button_url(button)));
  }
}

/**
 * The two lists merged in order. Each plugin's list must come back out of the
 * merge unchanged, and a name both declare must be the same control in both:
 * same type, group and default, one element list a prefix of the other.
 */
function merge(a, b) {
  const out = [];
  let i = 0;
  let j = 0;
  const later = (list, from, name) => list.slice(from).some((d) => d.name === name);
  while (i < a.length || j < b.length) {
    if (i < a.length && j < b.length && a[i].name === b[j].name) {
      out.push({ effect: a[i], source: b[j] });
      i += 1;
      j += 1;
    } else if (j < b.length && (i >= a.length || !later(a, i, b[j].name))) {
      out.push({ effect: null, source: b[j] });
      j += 1;
    } else {
      out.push({ effect: a[i], source: null });
      i += 1;
    }
  }
  for (const row of out) {
    if (!row.effect || !row.source) continue;
    const [x, y] = [row.effect, row.source];
    const sameList = (p, q) => p.every((v, k) => q[k] === v);
    const shorter = (x.elements?.length ?? 0) <= (y.elements?.length ?? 0) ? x : y;
    const longer = shorter === x ? y : x;
    if (x.type !== y.type || x.group !== y.group || (x.type !== FF_TYPE_TEXT && x.default !== y.default)
      || (x.elements && !(sameList(shorter.elements, longer.elements) && sameList(shorter.values, longer.values)))) {
      throw new Error(`"${x.name}" is declared differently by SW Gauge and SW Gauge Leader`);
    }
  }
  const back = (side) => out.filter((r) => r[side]).map((r) => r[side].index).join(',');
  if (back('effect') !== a.map((d) => d.index).join(',') || back('source') !== b.map((d) => d.index).join(',')) {
    throw new Error('the two declaration lists do not merge in order');
  }
  return out;
}

const MERGED = merge(DECLARED.effect.list, DECLARED.source.list);

//===========================================================================
// The read-outs: Controls.cpp's number, this page's unit.
//===========================================================================

const fixed = (n, d) => n.toFixed(d);
const signed = (n, d) => `${n >= 0 ? '+' : '−'}${Math.abs(n).toFixed(d)}`;
const UNITS = {
  Rack: (x) => `${signed(x, 3)} frame`,
  Drift: (x) => `${signed(x, 3)} fr/s`,
  Size: (x) => `${fixed(100 * x, 1)}% high`,
  Angle: (x) => `${signed(x, 1)}°`,
  Position: (x) => `${signed(x, 2)}`,
  Travel: (x) => `${signed(x, Math.abs(x) < 1 ? 3 : 2)} fr/s`,
  Shutter: (x) => `${fixed(x, 0)}°`,
  'Track Level': (x) => `×${fixed(x, 2)}`,
};

/**
 * One instance of each plugin, never initialised, kept for the read-outs:
 * gu_convert asks the instance which control a host index is (the same
 * control has a different index in each plugin) and applies Controls.cpp's
 * conversion for it, which is the same function in both.
 */
const CONVERTERS = { effect: core._gu_new(1), source: core._gu_new(0) };

/** The read-out for a standard control, through whichever plugin declares it. */
function readout(name, row) {
  const variant = row.effect ? 'effect' : 'source';
  const index = row[variant].index;
  return (v) => {
    const x = core._gu_convert(CONVERTERS[variant], index, Math.fround(v));
    if (Number.isNaN(x)) return fixed(v, 2);
    return (UNITS[name] ?? ((y) => fixed(y, 3)))(x);
  };
}

/** Tooltips: this page's words, from the README's and the user guide's account of each control. */
const HINTS = {
  Format: 'The gauge and the print: 35 mm Academy (a sound release print), Scope (2× anamorphic, all four soundtracks), Full (silent), Still (the 135 roll, eight holes a frame), 16 mm (double perf), 16 mm Sound, Super 16, Super 8. Changing it starts new film: every held picture is forgotten.',
  Stock: 'Colour Print: a black rebate, light edge print, a cyan-dye track. B&W Print: the picture in luma. Clear Base: unexposed print film on a light box. The stock codes on the edge are made up.',
  Squeeze: 'The anamorphic lens the picture is exposed through. Format uses the format’s own (2× on Scope, none elsewhere). Something round in the clip is printed 1/squeeze as wide as it is tall — try the Geometry card.',
  Fit: 'How the clip fills the picture’s shape as seen: Crop fills and trims, Letterbox shows it all with black bars (which print black), Stretch stretches it.',
  'Edge Print': 'The stock’s code on every frame in the margin, and a four-digit key number once a foot (every 16 frames on 35 mm, 20 on 16 mm). Scope and Super 8 have none.',
  Soundtracks: 'The analogue track, and on Scope DTS, Dolby Digital and SDDS, drawn as data with no logos. Academy has mono analogue and DTS, 16 mm Sound one analogue track; the other formats are silent.',
  'Track Level': 'The analogue track’s gain on the audio, 0 to 2. There is no audio on this page, so the track is a band of constant width whatever this says.',
  Audio: 'Resolume’s audio input: an FFT buffer of 64 bins (FF_TYPE_BUFFER) the host fills every frame. A browser has no Resolume FFT, so this page never writes it: the bins stay zero and the optical track does not wiggle.',
  Mode: 'Cell looks at the frame in the gate, the way a scanner would. Strip lays the film across the output as a moving strip, starting with the gate’s frame in the middle.',
  Framing: 'Cell: from the picture filling the output (0) to the film’s whole width and a quarter again (1), on a log scale. At 1 the frames above and below the gate are in view.',
  Rack: 'Cell: moves the view along the film by up to half a frame either way. Towards 0 the frame above (the previous picture) comes down; towards 1 the frame below (the clip as it is now).',
  Drift: 'Cell: rolls the frame line through the view, up to 4 frames a second either way. Cubic: 0.75 is half a frame a second.',
  Size: 'Strip: the film’s width as a share of the output’s height, 5% to 100%, on a log scale.',
  Angle: 'Strip: turns the strip, −180° to +180°. At 0° the film runs left to right, head on the left, the guided edge along the bottom; +90° stands it up with its frames upright.',
  Position: 'Strip: moves the film’s centre line across the strip, from the top edge of the output (0) to the bottom (1), at Angle 0°.',
  Travel: 'Strip: how fast the film runs, −24 to +24 frames a second; cubic, so the middle of the slider is the slow end. Positive runs it head first, new frames coming in at the right.',
  Shutter: 'Motion blur on moving film, 0° (sharp) to 360°: up to 16 positions of the film averaged, so it shows only when the film moves more than a pixel a frame.',
  Rate: 'The film’s speed in Cell: how often the gate pulls down, how long the countdown takes (192 frames: 8 s at 24 fps), how long a flash lasts. Beat is 24 frames to a beat of the tempo — here the Host BPM under the picture.',
  Frames: 'Held: each frame keeps the picture it was exposed with (in Cell the gate’s frame is the clip as it is now, the frames above it the past). Live: every frame shows the clip as it is now.',
  Loop: 'The source only. On: the leader goes round and round. Off: it plays once, then After.',
  'Roll Leader': 'Splices the countdown onto the film at the next frame, or at the beat or bar Start On waits for. On the effect the clip comes back 192 frames after PICTURE START. FF_TYPE_EVENT: the button sends the press and releases it on the next frame.',
  'Start On': 'Press takes the next frame. Beat and Bar wait for the next beat or bar of the tempo (here the page’s Host BPM clock) and land on the frame that starts there. For Roll Leader and Flash.',
  'Pop Frame': 'Number draws the 2; Flash makes the 2 a white frame. Either way the 2 carries the pop on the soundtrack (drawn, not heard).',
  Flash: 'One film frame fully exposed, which prints white: one film frame long wherever it lands. FF_TYPE_EVENT: the button sends the press and releases it on the next frame.',
  'Auto Flash': 'A flash on every beat or bar of the tempo (here the page’s Host BPM clock). Photosensitivity: on Beat it flashes a whole frame white once a beat, 2 Hz at 120 BPM.',
  After: 'The source only, after a leader that does not loop: Black film, or Clear, which cuts the picture area out of the film.',
  Holes: 'Light prints them white, Clear makes them transparent (the backdrop shows through), Black fills them.',
  Surround: 'What is beyond the film’s edges: Clear, Black, Light, or on the effect only, Clip — the clip itself full screen behind the film.',
  Mix: 'The effect only: the film over the clip, from the clip alone (0) to the film (1).',
  About: 'The plugin’s own About line (FF_TYPE_TEXT, display only).',
};

//===========================================================================
// Kit parameters from the merged declarations. The id is the name as Arena
// addresses it (lower case, spaces removed).
//===========================================================================

const KIT_TYPES = {
  [FF_TYPE_STANDARD]: 'standard',
  [FF_TYPE_OPTION]: 'option',
  [FF_TYPE_BOOLEAN]: 'boolean',
  [FF_TYPE_EVENT]: 'boolean',
  [FF_TYPE_INTEGER]: 'text',
  [FF_TYPE_TEXT]: 'text',
  [FF_TYPE_BUFFER]: 'text',
};

/**
 * The kit reads `embed`, `size`, `clip` and `bg` from the query string itself,
 * and this page reads `bpm` and `plugin`; a parameter id equal to one would be
 * set by it. Gauge's Size is the case: its id is `param-size`.
 */
const RESERVED_QUERY = new Set(['embed', 'size', 'clip', 'bg', 'bpm', 'plugin']);
const addressOf = (name) => {
  const address = name.toLowerCase().replace(/[^a-z0-9]+/g, '');
  return RESERVED_QUERY.has(address) ? `param-${address}` : address;
};

function kitDefault(d) {
  switch (d.type) {
    case FF_TYPE_OPTION: {
      const i = d.values.findIndex((v) => v === d.default);
      return i >= 0 ? i : Math.round(d.default);
    }
    case FF_TYPE_BOOLEAN:
    case FF_TYPE_EVENT: return d.default > 0.5 ? 1 : 0;
    case FF_TYPE_BUFFER: return '';
    default: return d.default;
  }
}

const PARAMS = MERGED.map((row) => {
  const d = row.effect ?? row.source;
  const longer = row.effect && row.source && (row.source.elements?.length ?? 0) > (row.effect.elements?.length ?? 0) ? row.source : d;
  const p = {
    id: addressOf(d.name), name: d.name, type: KIT_TYPES[d.type], group: d.group, hint: HINTS[d.name],
    ff: { effect: row.effect, source: row.source }, kind: d.type, default: kitDefault(d),
  };
  if (p.type === undefined) throw new Error(`no kit control for FFGL type ${d.type} (${d.name})`);
  if (d.type === FF_TYPE_OPTION) p.elements = longer.elements;
  if (d.type === FF_TYPE_STANDARD) p.display = readout(d.name, row);
  if (d.type === FF_TYPE_BUFFER) p.placeholder = 'none: no audio in a browser';
  if (ABOUT_LINKS.has(d.name) && d.type === FF_TYPE_EVENT) p.hint = `Opens ${ABOUT_LINKS.get(d.name)} — the address the plugin’s own button opens.`;
  return p;
});
const PARAM = Object.fromEntries(PARAMS.map((p) => [p.name, p]));
if (new Set(PARAMS.map((p) => p.id)).size !== PARAMS.length) throw new Error('two parameters share an address');

/** The host index of a kit parameter in this variant's plugin, or -1. */
const hostIndex = (p, variant) => p.ff[variant]?.index ?? -1;
const isAbout = (p, variant) => hostIndex(p, variant) >= DECLARED[variant].aboutFirst;

//===========================================================================
// The presets: the user guide's Start here, as settings. An option is its
// element's index.
//===========================================================================

const element = (name, label) => {
  const i = PARAM[name].elements.indexOf(label);
  if (i < 0) throw new Error(`${name} has no element "${label}"`);
  return i;
};
const id = (name) => PARAM[name].id;

const PRESETS = {
  'The film’s whole width (Framing 1)': { [id('Framing')]: 1 },
  '35 mm Scope: the squeeze and four soundtracks': { [id('Format')]: element('Format', '35 mm Scope') },
  '35 mm Scope, the whole width': { [id('Format')]: element('Format', '35 mm Scope'), [id('Framing')]: 1 },
  'Super 8, the whole width': { [id('Format')]: element('Format', 'Super 8'), [id('Framing')]: 1 },
  '16 mm Sound, the whole width': { [id('Format')]: element('Format', '16 mm Sound'), [id('Framing')]: 1 },
  'The still roll (35 mm Still)': { [id('Format')]: element('Format', '35 mm Still'), [id('Framing')]: 1 },
  'A strip in motion': { [id('Mode')]: element('Mode', 'Strip') },
  'SW Gauge: a strip of the clip’s past over the clip': {
    [id('Mode')]: element('Mode', 'Strip'), [id('Surround')]: element('Surround', 'Clip'), [id('Holes')]: element('Holes', 'Clear'),
  },
  'A strip standing up (Angle +90°)': { [id('Mode')]: element('Mode', 'Strip'), [id('Angle')]: 0.75 },
  'The countdown on the bar (Rate Beat, Start On Bar)': {
    [id('Rate')]: element('Rate', 'Beat'), [id('Start On')]: element('Start On', 'Bar'),
  },
};

const query = new URLSearchParams(window.location.search);
const initialVariant = query.get('plugin') === 'leader' ? 'source' : 'effect';

//===========================================================================
// The transport: the page's BPM clock, standing in for Resolume's.
//===========================================================================

const BPM_LOW = 20;
const BPM_HIGH = 300;
const queryBpm = Number(query.get('bpm'));
const transport = {
  bpm: Number.isFinite(queryBpm) && queryBpm >= BPM_LOW && queryBpm <= BPM_HIGH ? queryBpm : 120,
  bars: 0,///< bars since the top, accumulated from the page's seconds
  last: null,///< the page's seconds at the last frame
  /** The bar phase at the page's `seconds`: advanced at the BPM, four beats to the bar. */
  advance(seconds) {
    if (this.last === null || seconds < this.last) this.bars = 0;// a start, or Restart
    else this.bars += ((seconds - this.last) * this.bpm) / 240;
    this.last = seconds;
    return this.bars - Math.floor(this.bars);
  },
  reset() {
    this.bars = 0;
    this.last = null;
  },
};

//===========================================================================
// The renderer: one plugin instance, driven as a host drives it.
//===========================================================================

/** The value the host would send for a kit control, or null for none. */
function hostValue(p, v, variant) {
  const d = p.ff[variant];
  if (!d || isAbout(p, variant)) return null;// not this plugin's, or the About block
  switch (d.type) {
    case FF_TYPE_OPTION: return Math.fround(d.values[Math.min(Math.round(v), d.values.length - 1)] ?? 0);
    case FF_TYPE_BOOLEAN: return v > 0.5 ? 1 : 0;
    case FF_TYPE_EVENT: return null;
    case FF_TYPE_BUFFER: return null;// the page has no audio to write into it
    default: return Math.fround(v);
  }
}

const CONTENT = ['the clip, live', 'a held picture', 'leader', 'a sync flash', 'black film', 'clear'];

function createRenderer(gl) {
  // emscripten's GL library, pointed at the kit's context. Nothing is turned
  // on behind the kit's back: no extensions are enabled on its behalf.
  const handle = core.GL.registerContext(gl, { majorVersion: 2, minorVersion: 0, enableExtensionsByDefault: false });
  core.GL.makeContextCurrent(handle);

  let instance = 0;
  let instanceVariant = null;
  let pushed = new Map();
  let releases = [];
  let clipName = 0;
  let clipTexture = null;
  let frames = 0;
  let barPhase = 0;

  /** A kit texture under a GL name the plugin can bind, in emscripten's tables. */
  function glName(texture) {
    if (texture === clipTexture) return clipName;
    if (clipName) core.GL.textures[clipName] = null;
    clipName = core.GL.getNewId(core.GL.textures);
    core.GL.textures[clipName] = texture;
    texture.name = clipName;
    clipTexture = texture;
    return clipName;
  }

  function start(variant, width, height) {
    if (instance) core._gu_delete(instance);
    instance = core._gu_new(VARIANTS[variant].flag);
    instanceVariant = variant;
    pushed = new Map();
    releases = [];
    frames = 0;
    transport.reset();
    if (!core._gu_init_gl(instance, width, height)) {
      const why = shaderCheck.refused
        ? `the ${shaderCheck.refused.stage} shader text the compiled plugin handed to GL (${shaderCheck.refused.length} characters) is not this page’s checked copy of source/Shaders.cpp, so it was not compiled`
        : 'the plugin’s InitGL failed — its log line under the picture says which pass';
      throw new GLError(`${VARIANTS[variant].name} could not start: ${why}.`);
    }
  }

  function push(params, variant) {
    for (const p of PARAMS) {
      const value = hostValue(p, params.get(p.id), variant);
      if (value === null || pushed.get(p.id) === value) continue;
      pushed.set(p.id, value);
      core._gu_set_float(instance, hostIndex(p, variant), value);
    }
  }

  return {
    render({ input, params, width, height, time, variant }) {
      core.GL.makeContextCurrent(handle);
      if (!instance || instanceVariant !== variant) start(variant, width, height);
      push(params, variant);
      barPhase = transport.advance(time);
      const effect = variant === 'effect';
      const clip = effect ? glName(input.texture) : 0;
      if (!core._gu_process(instance, time, Math.fround(transport.bpm), Math.fround(barPhase), clip,
        effect ? input.width : width, effect ? input.height : height, 0)) {
        throw new GLError('The plugin’s ProcessOpenGL failed.');
      }
      // A press is held through exactly one frame: Resolume sends 1 on one
      // frame and 0 on the next.
      for (const index of releases) core._gu_set_float(instance, index, 0);
      releases = [];
      // For anything checking the page from outside: frames this instance drew.
      frames += 1;
      gl.canvas.dataset.frames = String(frames);
    },
    /** Roll Leader or Flash: 1 now, 0 after the next frame. */
    press(name) {
      if (!instance) return false;
      const index = hostIndex(PARAM[name], instanceVariant);
      if (index < 0) return false;
      core._gu_set_float(instance, index, 1);
      releases.push(index);
      return true;
    },
    /** An About button: the press and the release at once (the plugin acts on the press). */
    click(name) {
      if (!instance) return;
      const index = hostIndex(PARAM[name], instanceVariant);
      if (index < 0) return;
      core._gu_set_float(instance, index, 1);
      core._gu_set_float(instance, index, 0);
    },
    get barPhase() { return barPhase; },
    get variant() { return instanceVariant; },
    /** The film's state, through the harness's accessors. */
    state() {
      if (!instance || frames === 0) return null;
      const s = (k) => core._gu_state(instance, k);
      const describe = (frame) => {
        const c = core._gu_content(instance, frame);
        if (c < 0) return null;
        const kind = Math.floor(c / 1000);
        const value = c - kind * 1000;
        if (kind !== 2) return CONTENT[kind] ?? `kind ${kind}`;
        const n = core._gu_leader_number(value);
        return `leader frame ${value} (${n === 0 ? 'PICTURE START' : n < 0 ? 'black' : `the ${n}`})`;
      };
      const gate = s(1);
      const lo = s(5);
      const hi = s(6);
      return {
        film: s(0), gate, rate: s(2), strip: s(3), leaderStart: s(4), lo, hi,
        held: [s(7), s(8), s(9)], mmPerPixel: s(10),
        inGate: describe(gate),
        middle: describe(Math.floor(s(3))),
        frames,
      };
    },
    log() {
      try {
        return core.FS.readFile(text(core._gu_log_path()), { encoding: 'utf8' });
      } catch {
        return '';
      }
    },
  };
}

//===========================================================================
// The page.
//===========================================================================

let renderer = null;

const mounted = mountDemo({
  name: 'Gauge',
  pluginId: 'GU01 · GU02',
  kind: ['effect', 'source'],
  tagline:
    'The film strip itself, for Resolume. SW Gauge prints your clip onto 35 mm, 16 mm or Super 8, with the perforations, frame lines and soundtracks where the standards put them; SW Gauge Leader runs the countdown. One frame filling the screen, or a strip of film running across it in which every frame keeps the picture it was exposed with, so a moving strip is a contact strip of the clip’s past. An anamorphic squeeze, a countdown leader and a sync flash. None of it is drawn at a place on the screen: the strip is built in film millimetres from the published standards, and a camera looks at it.',
  repo: 'https://github.com/stoatworks-labs/gauge',
  page: 'https://stoatworks-labs.com/software/gauge/',

  blurb:
    'It is the plugin’s own C++ — the one class both plugins are made from, with the strip, the camera, the held frames, the leader’s lettering and the parameter lists — compiled unmodified to WebAssembly, drawing with its own GLSL in WebGL2. The host is this page: its tempo is a BPM clock the page keeps, there is no audio, the clock is the browser’s and the clip is generated in the page.',

  // The scene is footage to print; the geometry card shows the squeeze (its
  // rings come out tall and thin on Scope) and a strip's frames holding their
  // own moment; bars and lights on black are easy to follow along a strip; the
  // shape on transparency prints as black film, because film is opaque.
  sources: ['scene', 'grid', 'bars', 'spot', 'ramp', 'alpha'],

  params: PARAMS,
  presets: PRESETS,
  // Clear holes and a Clear surround (the default) are real alpha.
  showBackdrop: true,

  variants: {
    label: 'Plugin',
    default: initialVariant,
    options: [
      { id: 'effect', name: 'SW Gauge (effect)', hint: 'GU01, FF_EFFECT: the clip printed onto the strip, frame by frame.' },
      { id: 'source', name: 'SW Gauge Leader (source)', hint: 'GU02, FF_SOURCE: the countdown leader on its own film, looping. Reads no clip.' },
    ],
  },

  differences: [
    'This page runs the plugin rather than a port of it. gauge-core.wasm is source/Gauge.cpp — the class both plugins are made from: its constructor and both parameter lists, its clock and unit vote, its transport, the presses and the beats they wait for, the leader and the flashes, the camera, the held frames, the captures, every uniform of the composite — and everything it calls (Strip, Camera, HeldFrames, Controls, Font, Lettering, Shaders, Diag), with the FFGL SDK’s CFFGLPlugin, parameter bookkeeping, FFGLShader, FFGLScreenQuad and scoped bindings, all compiled unmodified by emscripten (demo/tools/build-wasm.sh). The leader’s numerals and lettering are the plugin’s own stroked face, and its distance-field atlas is built in this page at start-up by the plugin’s own code, as it is in Resolume. Of source/ only EffectPlugin.cpp and SourcePlugin.cpp, the two registrations, are left out, and with them the SDK’s plugMain and CFFGLPluginInfo: the page constructs the class itself with the flag each registration passes, as the plugin’s harness does. The panel is read back from both plugins’ own declarations through the SDK’s host getters, and the numbers beside the sliders are Controls.cpp’s conversions, compiled in; their units, every tooltip and the presets are this page’s.',
    'The GLSL is the plugin’s: demo/shaders.js is spliced from source/Shaders.cpp, with the order its stage functions join the pieces in, and the repository’s verify script fails if a character differs or if the WebAssembly was built from sources that have since changed. At start-up the page also requires each text the compiled plugin hands to glShaderSource to equal its own assembly of that copy, and refuses to compile anything else. The kit’s port() then changes the version line and adds the ES precision defaults. GL is emscripten’s WebGL2 library on this page’s context, not a GL 4.1 driver; one entry point is the page’s (demo/wasm/gl_shim.cpp), glShaderSource, for that check and port. Every texture the plugin makes — the held frames’ RGBA8 texture array, the RGBA32I content table, the R32F spectrum, the RGBA32F glyph metrics, the R8 font and atlas — is a format WebGL2 takes as it is, so nothing else is changed and no extension is needed.',
    'The tempo is simulated. Resolume hands the plugin its BPM and bar phase every frame (SetBeatInfo); a browser has no transport, so this page keeps a BPM clock of its own — the Host BPM field under the picture, 120 at first — and hands the plugin that BPM and a bar phase advanced from the page’s own seconds, as the plugin’s harness does. Rate Beat, Start On Beat and Bar and Auto Flash run on it. It is not the composition’s tempo and not anything you can hear. Pause stops it with the clock; a changed BPM runs from then on without a jump.',
    'There is no audio. SW Gauge declares Audio, an FFT buffer of 64 bins that Resolume fills every frame; it is shown on the panel where the plugin declares it, but nothing on this page writes it, so every bin is zero, the analogue track is a band of constant width and Track Level has nothing to scale. The leader’s pop on the 2 is not audio in: the composite draws it on the track, and it is there.',
    'The clock is the browser’s: SetTime is handed seconds summed from the display’s frames (a frame capped at 0.1 s), and the plugin’s own vote settles on seconds within four frames, running on its wall clock (here performance.now()) until it does. The gate pulls down at the film’s rate against those seconds, so a browser drawing fewer frames a second than Resolume would still runs the film at 24 fps — in bigger steps. Pause stops the host clock and the plugin sees no time pass. Step advances 1/60 s. Restart is a backward jump, which the plugin takes as no time passing: the film carries on where it was. Switching the Plugin dropdown is a new instance, as it would be in Resolume: the film starts again and every control goes back to its default.',
    'Roll Leader, Flash and the About buttons are FF_TYPE_EVENT: Roll Leader and Flash send the press and release it on the next frame, as Resolume sends an event (a press while paused takes effect on that frame). The About block (a text line and four buttons) is the plugin’s and is on the panel because Resolume shows it; a button sends the press to the plugin, whose handler asks the operating system to open the link (std::system), which a browser does not have — so the page opens the same address itself, read from the plugin’s own About table. SW Gauge Leader declares Surround with three elements to SW Gauge’s four (no Clip), and the page hides the fourth when it is chosen.',
    'The clip is the kit’s generated one, premultiplied, or your own image or video, handed to the plugin unpadded (Width = HardwareWidth); a host may pad it. Each frame on the strip is exposed from the clip the page hands over on the frame it is exposed, and held as an 8-bit picture, as in Resolume. The output is the page’s 8-bit canvas, and where the holes or the surround are Clear the backdrop chosen under the picture shows through.',
    'Frames drawn here were compared with the plugin’s own harness, gutest, rendering the same input frames on the same clock at the same settings: see the repository’s demo/tools/crosscheck for the numbers and how to rerun it. WebAssembly has no fused multiply-add and WebGL2 is GLSL ES 3.00 on whatever GPU you have, so the film clock and the camera can differ from a Mac’s in the last bits and a pixel here is not evidence about a pixel there; the models and every constant are the same code.',
    'The plugin itself has never been loaded into Resolume on macOS; on Windows it has been gated in Resolume Arena 7.27.1 with software rendering and no sound device (its README’s Status says so). Its harness, gutest, drives the same class headlessly and measures each claim out of the picture — every format’s holes, pictures and frame lines against the published dimensions, the squeeze from a disc’s moments, a moving strip’s held frames, the countdown frame by frame, a flash one film frame long, the track at the film’s speed — with ten deliberately wrong models all caught, and that harness, not this page, is the reason to believe it.',
  ],

  createRenderer: (gl) => {
    renderer = createRenderer(gl);
    return renderer;
  },
});

//===========================================================================
// The controls the kit has no type for, which rows belong to which plugin,
// the About block, the transport's BPM, and the plugin's state and log.
//
// By inline style, not the `hidden` attribute: kit.css gives these elements a
// `display` of their own, which beats the attribute's user-agent rule.
//===========================================================================

if (mounted) {
  const params = mounted.params;
  const embed = query.has('embed') && query.get('embed') !== '0';
  let shownVariant = mounted.state.variant;

  // Copy link carries the plugin, since the two declare different controls.
  const toQuery = params.toQuery.bind(params);
  params.toQuery = () => {
    const q = toQuery();
    if (mounted.state.variant === 'source') q.set('plugin', 'leader');
    if (transport.bpm !== 120) q.set('bpm', String(transport.bpm));
    return q;
  };

  // For anything checking the page from outside (a headless browser reading
  // the plugin's own accessors): the kit's params and state, and the renderer.
  window.gaugeDemo = { params, state: mounted.state, redraw: mounted.redraw, transport, shaderCheck, PARAMS, get renderer() { return renderer; } };

  const show = (node, on) => { if (node) node.style.display = on ? '' : 'none'; };
  const rowOf = (p) => document.getElementById(`p-${p.id}`)?.closest('.prow')
    ?? [...document.querySelectorAll('.prow')].find((row) => row.querySelector('.prow__name')?.textContent === p.name);

  // A preset or a query string may ask the source for an element it does not
  // declare (Surround Clip): put it on the source's last, as its own option()
  // clamps it.
  function clampToVariant(variant) {
    for (const p of PARAMS) {
      const d = p.ff[variant];
      if (!d || d.type !== FF_TYPE_OPTION) continue;
      const last = d.elements.length - 1;
      if (params.get(p.id) > last) params.set(p.id, last);
    }
  }

  function showForVariant(variant) {
    for (const p of PARAMS) {
      const row = rowOf(p);
      show(row, hostIndex(p, variant) >= 0);
      const d = p.ff[variant];
      if (row && d?.type === FF_TYPE_OPTION) {
        const select = row.querySelector('select');
        [...(select?.options ?? [])].forEach((option, i) => {
          option.hidden = i >= d.elements.length;
          option.disabled = i >= d.elements.length;
        });
      }
    }
    const effect = variant === 'effect';
    for (const field of document.querySelectorAll('.transport__field')) {
      if (field.querySelector('.transport__label')?.textContent === 'Clip') show(field, effect);
    }
    show(document.querySelector('.transport__file'), effect);
  }

  params.addEventListener('change', () => clampToVariant(mounted.state.variant));

  document.addEventListener('demo:state', () => {
    const variant = mounted.state.variant;
    if (variant !== shownVariant) {
      shownVariant = variant;
      // A new instance: the constructor's defaults (the same for both).
      params.reset();
    }
    if (!embed) showForVariant(variant);
  });
  clampToVariant(shownVariant);

  if (!embed) {
    showForVariant(shownVariant);

    const button = (label, title) => {
      const b = document.createElement('button');
      b.type = 'button';
      b.className = 'btn';
      b.textContent = label;
      if (title) b.title = title;
      return b;
    };

    for (const p of PARAMS) {
      const row = rowOf(p);
      if (!row) continue;

      // FF_TYPE_EVENT: Roll Leader and Flash are a press held through one
      // frame. An About button sends its press and opens the address the
      // plugin's own press would have opened.
      if (p.kind === FF_TYPE_EVENT) {
        const toggle = row.querySelector('.prow__toggle');
        const url = ABOUT_LINKS.get(p.name);
        let press;
        if (url) {
          press = document.createElement('a');
          press.href = url;
          press.target = '_blank';
          press.rel = 'noopener';
          press.textContent = 'Open';
          press.title = p.hint;
          press.addEventListener('click', () => renderer?.click(p.name));
        } else {
          press = button(p.name, p.hint);
          press.addEventListener('click', () => {
            if (renderer?.press(p.name)) mounted.redraw();
          });
        }
        press.className = 'prow__toggle';
        press.dataset.press = p.id;
        press.style.textAlign = 'center';
        press.style.textDecoration = 'none';
        toggle?.replaceWith(press);
        continue;
      }

      // The About line and Audio: the plugin's, display only.
      const field = row.querySelector('.prow__text');
      if (field && (p.kind === FF_TYPE_TEXT || p.kind === FF_TYPE_BUFFER)) {
        field.readOnly = true;
        field.tabIndex = -1;
        if (p.kind === FF_TYPE_BUFFER) field.title = p.hint;
      }
    }

    const stage = document.querySelector('.stage');
    const bar = document.querySelector('.transport');

    //-----------------------------------------------------------------------
    // The transport's BPM: what Resolume's would hand the plugin.
    //-----------------------------------------------------------------------
    if (bar) {
      const label = document.createElement('label');
      label.className = 'transport__field';
      const span = document.createElement('span');
      span.className = 'transport__label';
      span.textContent = 'Host BPM';
      const bpm = document.createElement('input');
      bpm.type = 'number';
      bpm.className = 'prow__text';
      bpm.min = String(BPM_LOW);
      bpm.max = String(BPM_HIGH);
      bpm.step = '1';
      bpm.value = String(transport.bpm);
      bpm.inputMode = 'numeric';
      bpm.style.width = '6em';
      bpm.style.flex = '0 0 auto';
      bpm.id = 'gauge-bpm';
      label.title = 'A simulated transport: the BPM, and a bar phase from this page’s clock, handed to the plugin every frame (SetBeatInfo) as Resolume hands over the composition’s. Rate Beat, Start On Beat and Bar and Auto Flash run on it. Not anything you can hear.';
      bpm.addEventListener('change', () => {
        const v = Number(bpm.value);
        if (Number.isFinite(v) && v >= BPM_LOW && v <= BPM_HIGH) {
          transport.bpm = v;
          if (!mounted.state.playing) mounted.redraw();
        } else {
          bpm.value = String(transport.bpm);
        }
      });
      label.append(span, bpm);
      bar.append(label);
    }

    //-----------------------------------------------------------------------
    // The two event controls again, under the picture: the plugin's own Roll
    // Leader and Flash (they are also on the panel, in its Leader group).
    //-----------------------------------------------------------------------
    if (stage && bar) {
      const presses = document.createElement('div');
      presses.className = 'transport';
      presses.setAttribute('role', 'group');
      presses.setAttribute('aria-label', 'Leader');
      const label = document.createElement('span');
      label.className = 'transport__label';
      label.textContent = 'Leader';
      label.title = 'The plugin’s Roll Leader and Flash, which are also on the panel in its Leader group';
      presses.append(label);
      for (const name of ['Roll Leader', 'Flash']) {
        const b = button(name, PARAM[name].hint);
        b.dataset.pressBar = PARAM[name].id;
        b.addEventListener('click', () => {
          if (renderer?.press(name)) mounted.redraw();
        });
        presses.append(b);
      }
      bar.after(presses);
    }

    //-----------------------------------------------------------------------
    // What the film is doing (the harness's accessors), and the plugin's own
    // log (Diag.cpp). Both report; neither measures.
    //-----------------------------------------------------------------------
    if (stage) {
      const status = document.createElement('p');
      status.className = 'stage__status';
      status.id = 'gauge-state';
      status.setAttribute('aria-live', 'off');
      const log = document.createElement('p');
      log.className = 'stage__status';
      log.id = 'gauge-log';
      log.setAttribute('aria-live', 'off');
      log.style.fontFamily = 'ui-monospace, SFMono-Regular, Menlo, monospace';
      log.style.fontSize = '12px';
      log.style.whiteSpace = 'pre-wrap';
      stage.append(status, log);
      let lastLog = '';
      setInterval(() => {
        const s = renderer?.state();
        if (s) {
          const strip = Math.round(params.get(PARAM.Mode.id)) === PARAM.Mode.elements.indexOf('Strip');
          const leader = Number.isFinite(s.leaderStart) ? ` · the leader’s PICTURE START on frame ${s.leaderStart}` : '';
          const where = strip
            ? `frames ${s.lo} to ${s.hi} in view, ${s.middle ?? 'nothing'} mid-strip`
            : `frame ${s.gate} in the gate: ${s.inGate ?? 'nothing'}`;
          const bars = transport.bars;
          const beat = Math.floor((bars - Math.floor(bars)) * 4) + 1;
          status.textContent =
            `The plugin reports: film clock at ${fixed(s.film, 2)} frames, running at ${fixed(s.rate, 2)} fps · ${where}${leader}`
            + ` · held frames ${s.held[0]}×${s.held[1]} × ${s.held[2]} · ${fixed(s.mmPerPixel * 1000, 1)} µm of film a pixel. `
            + `The page’s transport: ${transport.bpm} BPM, bar ${Math.floor(bars) + 1} beat ${beat}.`;
        }
        const all = renderer?.log() ?? '';
        if (all === lastLog) return;
        lastLog = all;
        const lines = all.trim().split('\n').filter(Boolean).map((l) => l.replace(/^\S+ (INFO |WARN |ERROR) gauge: /, '$1 '));
        log.textContent = lines.length
          ? `The plugin’s own log (Diag.cpp, in this page’s memory):\n${lines.slice(-2).join('\n')}`
          : '';
      }, 250);
    }
  }
}
