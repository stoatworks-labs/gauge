// The browser demo against the plugin's own harness, frame for frame.
//
//     node demo/tools/crosscheck/crosscheck.mjs [--gutest build/gutest] [--url http://127.0.0.1:PORT/]
//                                               [--port N] [--gl metal|swiftshader] [--out DIR] [--only NAME]
//                                               [--negative]
//
// For each scenario below, headless Chrome loads harness.js (the page's
// gauge-core.wasm, shaders.js and the kit's port(), driven exactly as gutest's
// Session drives the native build: the same settings by name, the same
// presses, the host's seconds = frame / fps declared as seconds, a bar phase
// every frame) and renders every frame of the kit's generated clip through it.
// The same input bytes are piped through `gutest --pipe` with the same
// --set, --press, --fps and --bpm, and every output frame of the two is
// compared, channel by channel, in 8-bit levels.
//
// What a difference means: the WebAssembly is the plugin's C++ and the GLSL
// is the plugin's text (check_shaders.py), so a difference is the platform --
// WebGL2 (ANGLE over Metal, or SwiftShader) against Apple's GL 4.1, GLSL ES
// 3.00 against 4.10, float filtering and rounding, WebAssembly's lack of a
// fused multiply-add -- or a fault in the page's port() or GL shim, which is
// what this exists to catch. A difference fails (exit 1) when more than 0.1%
// of a frame's pixels are off by more than 4 levels in any channel.
//
// --negative is the comparer's own negative control: every scenario again,
// with one of the plugin's broken models (Gauge.h's Perturb bits, the ones
// gutest --negative uses) switched on in the PAGE's instance only. Every
// scenario must then fail; one that passes means this comparison cannot see
// that model, and the run exits 1.
//
// Needs node 22+ (its WebSocket), Google Chrome, and a built gutest
// (cmake -B build -DGAUGE_BUILD_TOOLS=ON && cmake --build build). It serves
// demo/ itself on --port (default: any free port) unless --url names a server
// already serving it.

import { spawn } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { deflateSync } from 'node:zlib';

const HERE = dirname(fileURLToPath(import.meta.url));
const DEMO = resolve(HERE, '..', '..');
const REPO = resolve(DEMO, '..');

const args = process.argv.slice(2);
const opt = (name, fallback) => {
  const i = args.indexOf(name);
  return i >= 0 ? args[i + 1] : fallback;
};
const GUTEST = resolve(REPO, opt('--gutest', 'build/gutest'));
const CHROME = opt('--chrome', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome');
const GL = opt('--gl', 'metal');
const OUT = opt('--out', null);
const ONLY = opt('--only', null);
const NEGATIVE = args.includes('--negative');
/** The broken model each scenario is run with under --negative (Gauge.h). */
const kPerturbHoleSwap = 1;
const kPerturbLeaderCount = 16;
const kPerturbImageCentred = 512;

// ---------------------------------------------------------------------------
// The scenarios: every format, both modes, both plugins, the leader, a flash,
// held and live frames, the four stocks' looks and the surrounds.
// ---------------------------------------------------------------------------
const W = 640;
const H = 360;
const SCENARIOS = [
  { name: 'effect-defaults', plugin: 'effect', clip: 'scene', frames: 48, fps: 60, negative: kPerturbImageCentred },
  { name: 'effect-scope-wide', plugin: 'effect', clip: 'grid', frames: 48, fps: 60, set: ['Format=35 mm Scope', 'Framing=1'] },
  { name: 'effect-strip-over-clip', plugin: 'effect', clip: 'scene', frames: 60, fps: 60,
    set: ['Mode=Strip', 'Surround=Clip', 'Holes=Clear', 'Travel=0.9', 'Angle=0.45'] },
  { name: 'effect-super8-leader', plugin: 'effect', clip: 'bars', frames: 60, fps: 15,
    set: ['Format=Super 8', 'Framing=1'], press: ['Roll Leader@2'], negative: kPerturbLeaderCount },
  { name: 'effect-still-strip-bw', plugin: 'effect', clip: 'grid', frames: 40, fps: 30,
    set: ['Format=35 mm Still', 'Stock=B&W Print', 'Mode=Strip', 'Size=0.85', 'Angle=0.75', 'Travel=0.8'] },
  { name: 'effect-16mm-live-flash', plugin: 'effect', clip: 'spot', frames: 30, fps: 60,
    set: ['Format=16 mm', 'Frames=Live', 'Stock=Clear Base', 'Holes=Black', 'Framing=0.9'], press: ['Flash@5'] },
  { name: 'effect-academy-drift-letterbox', plugin: 'effect', clip: 'ramp', frames: 48, fps: 60,
    set: ['Fit=Letterbox', 'Drift=0.85', 'Rack=0.3', 'Squeeze=1.5x', 'Mix=0.75'] },
  { name: 'source-defaults', plugin: 'source', frames: 40, fps: 10, negative: kPerturbLeaderCount },
  { name: 'source-scope-the-2', plugin: 'source', frames: 37, fps: 6, set: ['Format=35 mm Scope', 'Framing=0.4'] },
  { name: 'source-16mm-sound-strip', plugin: 'source', frames: 60, fps: 30,
    set: ['Format=16 mm Sound', 'Mode=Strip', 'Travel=0.85', 'Surround=Light'] },
  { name: 'source-super16-once', plugin: 'source', frames: 50, fps: 4, set: ['Format=Super 16', 'Loop=0', 'After=Clear'] },
];

// ---------------------------------------------------------------------------
// A static server for demo/, unless one was named.
// ---------------------------------------------------------------------------
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.css': 'text/css' };
async function serve() {
  const named = opt('--url', null);
  if (named) return { base: named.replace(/\/?$/, '/'), close: () => {} };
  const server = createServer((req, res) => {
    const path = resolve(DEMO, `.${decodeURIComponent(new URL(req.url, 'http://x').pathname)}`);
    if (!path.startsWith(DEMO) || !existsSync(path)) {
      res.writeHead(404).end();
      return;
    }
    res.writeHead(200, { 'Content-Type': TYPES[extname(path)] ?? 'application/octet-stream' }).end(readFileSync(path));
  });
  await new Promise((ok) => server.listen(Number(opt('--port', '0')), '127.0.0.1', ok));
  return { base: `http://127.0.0.1:${server.address().port}/`, close: () => server.close() };
}

// ---------------------------------------------------------------------------
// Chrome over the DevTools protocol.
// ---------------------------------------------------------------------------
async function chrome() {
  const profile = mkdtempSync(join(tmpdir(), 'gauge-crosscheck-'));
  const gl = GL === 'swiftshader'
    ? ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader']
    : ['--use-gl=angle', '--use-angle=metal', '--enable-gpu'];
  const proc = spawn(CHROME, ['--headless=new', '--no-first-run', '--no-default-browser-check', ...gl,
    `--user-data-dir=${profile}`, '--remote-debugging-port=0', 'about:blank'], { stdio: 'ignore' });
  let port = null;
  for (let i = 0; i < 100 && !port; i += 1) {
    await new Promise((ok) => setTimeout(ok, 100));
    try { port = readFileSync(join(profile, 'DevToolsActivePort'), 'utf8').split('\n')[0]; } catch { /* not yet */ }
  }
  if (!port) throw new Error('Chrome never wrote DevToolsActivePort');
  let target = null;
  for (let i = 0; i < 50 && !target; i += 1) {
    try {
      const list = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
      target = list.find((t) => t.type === 'page');
    } catch { /* not yet */ }
    if (!target) await new Promise((ok) => setTimeout(ok, 100));
  }
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((ok, fail) => { ws.onopen = ok; ws.onerror = fail; });
  let id = 0;
  const waiting = new Map();
  const errors = [];
  ws.onmessage = (event) => {
    const msg = JSON.parse(event.data);
    if (msg.id && waiting.has(msg.id)) {
      waiting.get(msg.id)(msg);
      waiting.delete(msg.id);
    } else if (msg.method === 'Runtime.exceptionThrown') {
      errors.push(msg.params.exceptionDetails?.exception?.description ?? msg.params.exceptionDetails?.text);
    } else if (msg.method === 'Runtime.consoleAPICalled' && msg.params.type === 'error') {
      errors.push(msg.params.args.map((a) => a.value ?? a.description).join(' '));
    }
  };
  const call = (method, params = {}) => new Promise((ok, fail) => {
    id += 1;
    waiting.set(id, (msg) => (msg.error ? fail(new Error(`${method}: ${msg.error.message}`)) : ok(msg.result)));
    ws.send(JSON.stringify({ id, method, params }));
  });
  const evaluate = async (expression) => {
    const r = await call('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
    if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description ?? r.exceptionDetails.text);
    return r.result.value;
  };
  await call('Runtime.enable');
  return {
    call, evaluate, errors,
    close() {
      ws.close();
      proc.kill();
      try { rmSync(profile, { recursive: true, force: true }); } catch { /* Chrome may still hold it */ }
    },
  };
}

// ---------------------------------------------------------------------------
// gutest --pipe, fed the page's input frames.
// ---------------------------------------------------------------------------
function gutestArgs(sc) {
  const a = ['--pipe', '--plugin', sc.plugin, '--size', `${W}x${H}`, '--fps', String(sc.fps), '--bpm', String(sc.bpm ?? 120)];
  if (sc.plugin === 'source') a.push('--frames', String(sc.frames));
  for (const s of sc.set ?? []) a.push('--set', s);
  for (const p of sc.press ?? []) a.push('--press', p);
  return a;
}

function runGutest(sc, inputs) {
  return new Promise((ok, fail) => {
    const child = spawn(GUTEST, gutestArgs(sc), { stdio: ['pipe', 'pipe', 'pipe'] });
    const chunks = [];
    let err = '';
    child.stdout.on('data', (c) => chunks.push(c));
    child.stderr.on('data', (c) => { err += c; });
    child.on('error', fail);
    child.on('close', (code) => (code === 0 ? ok(Buffer.concat(chunks)) : fail(new Error(`gutest exited ${code}: ${err}`))));
    for (const frame of inputs) child.stdin.write(frame);
    child.stdin.end();
  });
}

// ---------------------------------------------------------------------------
// A PNG writer, for --out: page | gutest | difference x16.
// ---------------------------------------------------------------------------
function crc32(buf) {
  let c;
  const table = crc32.table ?? (crc32.table = Array.from({ length: 256 }, (_, n) => {
    c = n;
    for (let k = 0; k < 8; k += 1) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    return c >>> 0;
  }));
  let crc = 0xffffffff;
  for (const b of buf) crc = table[(crc ^ b) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}
function png(width, height, rgba) {
  const chunk = (type, data) => {
    const head = Buffer.alloc(8);
    head.writeUInt32BE(data.length, 0);
    head.write(type, 4, 'latin1');
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(crc32(Buffer.concat([head.subarray(4), data])), 0);
    return Buffer.concat([head, data, crc]);
  };
  const raw = Buffer.alloc(height * (1 + width * 4));
  for (let y = 0; y < height; y += 1) {
    raw[y * (1 + width * 4)] = 0;
    rgba.copy(raw, y * (1 + width * 4) + 1, y * width * 4, (y + 1) * width * 4);
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8; ihdr[9] = 6;
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}
/** Alpha composited over mid grey, so clear parts read in the picture. */
function flat(rgba, x, y) {
  const i = (y * W + x) * 4;
  const a = rgba[i + 3] / 255;
  return [0, 1, 2].map((k) => Math.round(rgba[i + k] + 128 * (1 - a)));
}
function triptych(page, ref) {
  const out = Buffer.alloc(W * 3 * H * 4);
  for (let y = 0; y < H; y += 1) {
    for (let x = 0; x < W; x += 1) {
      const p = flat(page, x, y);
      const r = flat(ref, x, y);
      const i = (y * W + x) * 4;
      const d = Math.max(...[0, 1, 2, 3].map((k) => Math.abs(page[i + k] - ref[i + k])));
      const rows = [p, r, [Math.min(255, d * 16), Math.min(255, d * 16), Math.min(255, d * 16)]];
      rows.forEach((c, panel) => {
        const o = (y * W * 3 + panel * W + x) * 4;
        out[o] = c[0]; out[o + 1] = c[1]; out[o + 2] = c[2]; out[o + 3] = 255;
      });
    }
  }
  return png(W * 3, H, out);
}

// ---------------------------------------------------------------------------
function compare(a, b) {
  let over1 = 0;
  let over4 = 0;
  let max = 0;
  const pixels = a.length / 4;
  for (let i = 0; i < a.length; i += 4) {
    let d = 0;
    for (let k = 0; k < 4; k += 1) d = Math.max(d, Math.abs(a[i + k] - b[i + k]));
    if (d > 1) over1 += 1;
    if (d > 4) over4 += 1;
    if (d > max) max = d;
  }
  return { pixels, over1, over4, max };
}

const server = await serve();
const browser = await chrome();
let failed = 0;
let ran = 0;
let framesRan = 0;
try {
  await browser.call('Page.navigate', { url: `${server.base}tools/crosscheck/page.html` });
  for (let i = 0; i < 200; i += 1) {
    const ready = await browser.evaluate("document.body?.dataset.ready === '1'").catch(() => false);
    if (ready) break;
    await new Promise((ok) => setTimeout(ok, 100));
  }
  if (OUT) mkdirSync(OUT, { recursive: true });
  console.log(`${'scenario'.padEnd(32)} frames  identical±1   >4 levels (worst frame)  max   final frame ±1`);
  for (const sc of SCENARIOS) {
    if (ONLY && sc.name !== ONLY) continue;
    ran += 1;
    framesRan += sc.frames;
    const perturb = NEGATIVE ? (sc.negative ?? kPerturbHoleSwap) : 0;
    const info = await browser.evaluate(`crosscheck.run(${JSON.stringify({ ...sc, width: W, height: H, perturb })})`);
    if (info.refused.length) throw new Error(`the page refused shader text: ${info.refused}`);
    const inputs = [];
    const outputs = [];
    for (let i = 0; i < sc.frames; i += 1) {
      if (sc.plugin === 'effect') inputs.push(Buffer.from(await browser.evaluate(`crosscheck.input(${i})`), 'base64'));
      outputs.push(Buffer.from(await browser.evaluate(`crosscheck.output(${i})`), 'base64'));
    }
    const reference = await runGutest(sc, inputs);
    const size = W * H * 4;
    if (reference.length !== size * sc.frames) throw new Error(`${sc.name}: gutest wrote ${reference.length / size} frames, not ${sc.frames}`);
    let worst = { over4: -1 };
    let sumOver1 = 0;
    let max = 0;
    let last = null;
    for (let i = 0; i < sc.frames; i += 1) {
      const c = compare(outputs[i], reference.subarray(i * size, (i + 1) * size));
      c.frame = i;
      sumOver1 += c.over1;
      max = Math.max(max, c.max);
      if (c.over4 > worst.over4) worst = c;
      last = c;
    }
    const total = (W * H) * sc.frames;
    const pct = (n, of) => `${(100 * (1 - n / of)).toFixed(3)}%`;
    const bad = worst.over4 > 0.001 * W * H;
    if (bad !== NEGATIVE) failed += 1;
    const verdict = NEGATIVE ? (bad ? `   caught (perturb ${perturb})` : `   NOT CAUGHT (perturb ${perturb})`) : (bad ? '   FAIL' : '');
    console.log(`${sc.name.padEnd(32)} ${String(sc.frames).padStart(6)}  ${pct(sumOver1, total).padStart(10)}   ${String(worst.over4).padStart(6)} (frame ${String(worst.frame).padStart(2)})        ${String(max).padStart(4)}   ${pct(last.over1, W * H).padStart(9)}${verdict}`);
    if (OUT) {
      const n = sc.frames - 1;
      writeFileSync(join(OUT, `${sc.name}.png`), triptych(outputs[n], reference.subarray(n * size, (n + 1) * size)));
      if (worst.frame !== n) writeFileSync(join(OUT, `${sc.name}-worst.png`), triptych(outputs[worst.frame], reference.subarray(worst.frame * size, (worst.frame + 1) * size)));
    }
  }
  const renderer = await browser.evaluate("(() => { const g = document.createElement('canvas').getContext('webgl2'); if (!g) return 'none'; const x = g.getExtension('WEBGL_debug_renderer_info'); return x ? g.getParameter(x.UNMASKED_RENDERER_WEBGL) : g.getParameter(g.RENDERER); })()");
  console.log(`\nbrowser GL: ${renderer}`);
  if (browser.errors.length) {
    console.log(`page errors:\n  ${browser.errors.join('\n  ')}`);
    failed += 1;
  }
} finally {
  browser.close();
  server.close();
}
const counted = `${ran} scenarios, ${framesRan} frames`;
if (NEGATIVE) console.log(failed ? `${failed} broken model(s) NOT caught: this comparison is blind to them (${counted})` : `every broken model caught (${counted})`);
else console.log(failed ? `${failed} scenario(s) FAILED (${counted})` : `every scenario within tolerance (${counted})`);
process.exit(failed ? 1 : 0);
