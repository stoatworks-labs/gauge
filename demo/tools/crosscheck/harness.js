/**
 * The browser half of the cross-check: the page's compiled plugin
 * (gauge-core.wasm, the page's shaders.js and the kit's port()), driven
 * exactly as tools/gutest's Session drives the native build, on the kit's
 * generated clips. crosscheck.mjs loads this page headlessly, asks it to run a
 * scenario, and reads back every input frame (for `gutest --pipe`) and every
 * output frame (to compare with what gutest writes).
 *
 * What it shares with demo/plugin.js: the WebAssembly, the shader text and
 * its check, the kit's context and clips. What it does not: plugin.js's host
 * plumbing (the panel, the BPM field, the presses' timing against the
 * display), which is page code and is checked by loading the page itself.
 * The clock and transport here are gutest's, not the page's: the host's
 * seconds are frame / fps, declared as seconds (gu_declare_seconds, gutest's
 * SetClockScaleForTest( 1.0 )), with a bar phase of seconds x bpm / 240 every
 * frame.
 *
 * Not served: demo/.assetsignore keeps tools/ off the site.
 */

import { createContext, Quad, port } from '../../vendor/gl.js';
import { SOURCES, SourceRenderer } from '../../vendor/sources.js';
import * as S from '../../shaders.js';
import createGauge from '../../gauge-core.js';

const ASSEMBLED = Object.fromEntries(
  Object.entries(S.ASSEMBLY).map(([stage, pieces]) => [stage, pieces.map((piece) => S[piece]).join('')]),
);
const PAGE_SHADERS = {
  vertex: [ASSEMBLED.vertex],
  fragment: Object.keys(ASSEMBLED).filter((s) => s !== 'vertex').map((s) => ASSEMBLED[s]),
};
const refused = [];
function shaderSource(stage, text) {
  if (!PAGE_SHADERS[stage].includes(text)) {
    refused.push(stage);
    return '';
  }
  return port(text, { stage });
}

const core = await createGauge({ gaugeShaderSource: shaderSource, print: () => {}, printErr: () => {} });
core._gu_prepare_log();
const text = (p) => core.UTF8ToString(p);

const FF_TYPE_OPTION = 11;

/** gutest's applySetting: an option by its element's name, or a number. */
function apply(instance, name, value) {
  const n = core._gu_param_count(instance);
  for (let i = 0; i < n; i += 1) {
    if (text(core._gu_param_name(instance, i)) !== name) continue;
    if (core._gu_param_type(instance, i) === FF_TYPE_OPTION) {
      for (let e = 0; e < core._gu_param_element_count(instance, i); e += 1) {
        if (text(core._gu_param_element_name(instance, i, e)) === value) {
          core._gu_set_float(instance, i, e);
          return i;
        }
      }
    }
    const v = Number(value);
    if (!Number.isFinite(v)) throw new Error(`${name}: '${value}' is not a number or an option`);
    core._gu_set_float(instance, i, Math.fround(v));
    return i;
  }
  throw new Error(`no parameter called '${name}'`);
}

const toBase64 = (bytes) => {
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return btoa(s);
};

/** Rows bottom-first (GL) to top-first (gutest's pipe). */
function flip(bytes, width, height) {
  const out = new Uint8Array(bytes.length);
  const stride = width * 4;
  for (let y = 0; y < height; y += 1) out.set(bytes.subarray((height - 1 - y) * stride, (height - y) * stride), y * stride);
  return out;
}

let inputs = [];
let outputs = [];

/**
 * @param {object} sc
 * @param {'effect'|'source'} sc.plugin
 * @param {number} sc.width
 * @param {number} sc.height
 * @param {number} sc.frames
 * @param {number} sc.fps
 * @param {number} [sc.bpm]
 * @param {string} [sc.clip]  a kit clip id (the effect's input)
 * @param {string[]} [sc.set] "Name=Value", as gutest's --set
 * @param {string[]} [sc.press] "Name@frame", as gutest's --press
 * @param {number} [sc.perturb] Gauge.h's Perturb bits, on the page's side only
 *   (the negative control); never set for a real comparison
 */
function run(sc) {
  const canvas = document.createElement('canvas');
  canvas.width = sc.width;
  canvas.height = sc.height;
  const gl = createContext(canvas);
  const quad = new Quad(gl);
  const clips = new SourceRenderer(gl, quad);
  const handle = core.GL.registerContext(gl, { majorVersion: 2, minorVersion: 0, enableExtensionsByDefault: false });
  core.GL.makeContextCurrent(handle);

  const effect = sc.plugin === 'effect';
  const instance = core._gu_new(effect ? 1 : 0);
  // The negative control's broken model (Gauge.h's Perturb bits), page side only.
  if (sc.perturb) core._gu_perturb(instance, sc.perturb);
  for (const s of sc.set ?? []) {
    const at = s.indexOf('=');
    apply(instance, s.slice(0, at), s.slice(at + 1));
  }
  const presses = (sc.press ?? []).map((p) => {
    const at = p.lastIndexOf('@');
    return { name: p.slice(0, at), frame: Number(p.slice(at + 1)) };
  });
  if (!core._gu_init_gl(instance, sc.width, sc.height)) throw new Error(`InitGL failed (refused: ${refused.join(',')})`);

  const bpm = sc.bpm ?? 120;
  const clip = SOURCES.find((s) => s.id === (sc.clip ?? 'scene'));
  let clipName = 0;
  let clipTexture = null;
  inputs = [];
  outputs = [];
  for (let frame = 0; frame < sc.frames; frame += 1) {
    const seconds = frame / sc.fps;
    let name = 0;
    if (effect) {
      // The clip at this frame's time, read back as the bytes gutest is piped.
      const input = clips.render(clip, sc.width, sc.height, seconds);
      const bytes = new Uint8Array(sc.width * sc.height * 4);
      gl.bindFramebuffer(gl.FRAMEBUFFER, clips.buffer.fbo);
      gl.readPixels(0, 0, sc.width, sc.height, gl.RGBA, gl.UNSIGNED_BYTE, bytes);
      inputs.push(toBase64(flip(bytes, sc.width, sc.height)));
      if (input.texture !== clipTexture) {
        if (clipName) core.GL.textures[clipName] = null;
        clipName = core.GL.getNewId(core.GL.textures);
        core.GL.textures[clipName] = input.texture;
        input.texture.name = clipName;
        clipTexture = input.texture;
      }
      name = clipName;
    }
    // gutest's pressAt: 1 on the frame, 0 on the next.
    for (const p of presses) {
      if (p.frame === frame) apply(instance, p.name, '1');
      else if (p.frame + 1 === frame) apply(instance, p.name, '0');
    }
    // gutest's renderAt: the clock declared as seconds, the time, the
    // transport, a cleared target, ProcessOpenGL.
    core._gu_declare_seconds(instance);
    const bars = (seconds * bpm) / 60 / 4;
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, sc.width, sc.height);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    if (!core._gu_process(instance, seconds, Math.fround(bpm), Math.fround(bars - Math.floor(bars)), name, sc.width, sc.height, 0)) {
      throw new Error(`ProcessOpenGL failed on frame ${frame}`);
    }
    const out = new Uint8Array(sc.width * sc.height * 4);
    gl.readPixels(0, 0, sc.width, sc.height, gl.RGBA, gl.UNSIGNED_BYTE, out);
    outputs.push(toBase64(flip(out, sc.width, sc.height)));
  }
  const state = {
    film: core._gu_state(instance, 0), gate: core._gu_state(instance, 1),
    leaderStart: core._gu_state(instance, 4), lo: core._gu_state(instance, 5), hi: core._gu_state(instance, 6),
    gateContent: core._gu_content(instance, core._gu_state(instance, 1)),
  };
  const renderer = gl.getParameter(gl.RENDERER);
  core._gu_delete(instance);
  clips.dispose();
  quad.dispose();
  gl.getExtension('WEBGL_lose_context')?.loseContext();
  return { frames: sc.frames, refused: [...refused], state, renderer };
}

window.crosscheck = {
  run,
  input: (i) => inputs[i],
  output: (i) => outputs[i],
  ready: true,
};
document.body.dataset.ready = '1';
