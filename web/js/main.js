// The MapHappy web player: loads a show (a .maphappy project and its files) and plays it in the page.
import { compileProgram, makeTarget } from './gl.js';
import { readShow, applyTimeline, PresetPlayer } from './show.js';
import { buildMedia } from './media.js';
import { Renderer, LIGHT_MODELS } from './renderer.js';
import { ShowClock } from './clock.js';
import { kFullscreenVS } from './shaders.gen.js';

const $ = id => document.getElementById(id);
const canvas = $('stage'), status = $('status'), hud = $('hud'), startBtn = $('start');
const params = new URLSearchParams(location.search);
const SHOW_URL = new URL(params.get('show') || 'show/show.maphappy', location.href).href;

// Shows the stage texture in the canvas, letterboxed to keep the show's shape.
const BLIT_FS = `#version 300 es
precision highp float;
uniform sampler2D uTex;
uniform vec4 uRect;      // where the picture sits in the canvas, in pixels: x, y, w, h
out vec4 frag;
void main() {
  vec2 uv = (gl_FragCoord.xy - uRect.xy) / uRect.zw;
  frag = (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) ? vec4(0.0, 0.0, 0.0, 1.0) : vec4(texture(uTex, uv).rgb, 1.0);
}`;

function fail(msg) { status.textContent = msg; status.classList.add('error'); console.error(msg); }

async function loadProject() {
  // Files dropped onto the page win; otherwise the show next to the page (web/show/show.maphappy)
  if (dropped) return dropped;
  const r = await fetch(SHOW_URL, { cache: 'no-cache' });
  if (!r.ok) throw new Error(`No show found at ${SHOW_URL} (${r.status}). Save your MapHappy project as web/show/show.maphappy, with its media in the same folder, or drop the project and its files onto this page.`);
  const base = SHOW_URL;
  const resolve = (path, rel) => {
    if (rel) return new URL(rel, base).href;                     // stored relative to the project: the normal case
    if (path) return new URL(path.split(/[\\/]/).pop(), base).href;   // else look for the file next to the project
    return null;
  };
  return { json: await r.json(), resolve, name: decodeURIComponent(SHOW_URL.split('/').pop()) };
}

// ---- drag and drop: a .maphappy file plus the files it uses (handy for trying a show without a web server)
let dropped = null;
addEventListener('dragover', e => e.preventDefault());
addEventListener('drop', async e => {
  e.preventDefault();
  const files = [...e.dataTransfer.files];
  const project = files.find(f => f.name.toLowerCase().endsWith('.maphappy'));
  if (!project) { fail('Drop a .maphappy project (and the media, songs and models it uses) onto the page.'); return; }
  const byName = new Map(files.map(f => [f.name.toLowerCase(), URL.createObjectURL(f)]));
  const resolve = (path, rel) => { const n = (rel || path || '').split(/[\\/]/).pop().toLowerCase(); return byName.get(n) || null; };
  dropped = { json: JSON.parse(await project.text()), resolve, name: project.name };
  start(true);
});

let running = null;      // the show being played (replaced when another one is dropped)

async function start(fromDrop = false) {
  try {
    const gl = canvas.getContext('webgl2', { antialias: false, alpha: false, preserveDrawingBuffer: false });
    if (!gl) throw new Error('This browser has no WebGL 2. Try a current Chrome, Edge, Firefox or Safari.');
    const proj = await loadProject();
    const show = readShow(proj.json, proj.resolve);
    show.media = buildMedia(gl, show.mediaList, proj.resolve, w => show.warnings.push(w));
    const renderer = new Renderer(gl);
    const stage = makeTarget(gl, show.outW, show.outH);
    const blit = compileProgram(gl, kFullscreenVS, BLIT_FS, 'blit');
    const clock = new ShowClock(show);
    const player = new PresetPlayer();
    if (running) running.stop();
    running = { show, renderer, clock, player, stage, blit, gl, blackout: false, dirty: true, stop() { clock.pause(); this.stopped = true; } };
    $('showname').textContent = proj.name.replace(/\.maphappy$/i, '');
    status.textContent = show.warnings.length ? `Loaded with notes: ${show.warnings.join(' · ')}` : 'Ready.';
    startBtn.hidden = false;
    startBtn.onclick = async () => {
      startBtn.hidden = true;
      status.textContent = 'Loading the music...';
      await clock.load();
      for (const m of show.media.values()) if (m.start) m.start();
      status.textContent = clock.loadErrors.length ? clock.loadErrors.join(' · ') : 'Playing.';
      clock.play();
    };
    if (fromDrop) startBtn.click();
    requestAnimationFrame(t => frame(running, t, t));
  } catch (e) { fail(String(e.message || e)); }
}

function frame(r, now, last) {
  if (r.stopped) return;
  const { gl, show, renderer, clock, player, stage } = r;
  const dt = Math.min(0.1, (now - last) / 1000);
  clock.tick();
  const t = Math.max(0, clock.time);
  player.update(dt);                                          // a preset cross-fading in
  if (clock.playing || r.dirty) { applyTimeline(show, t); r.dirty = false; }   // ...but the timeline drives what it animates
  for (const m of show.media.values()) m.update(clock.playing ? dt : 0);
  renderer.renderStage(stage, show);

  // the stage, letterboxed into the canvas
  const dpr = devicePixelRatio || 1, cw = Math.round(canvas.clientWidth * dpr), ch = Math.round(canvas.clientHeight * dpr);
  if (canvas.width !== cw || canvas.height !== ch) { canvas.width = cw; canvas.height = ch; }
  const k = Math.min(cw / show.outW, ch / show.outH), w = show.outW * k, h = show.outH * k;
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  gl.viewport(0, 0, cw, ch);
  gl.useProgram(r.blit);
  gl.activeTexture(gl.TEXTURE0);
  gl.bindTexture(gl.TEXTURE_2D, r.blackout ? null : stage.tex);
  gl.uniform1i(r.blit.u('uTex'), 0);
  gl.uniform4f(r.blit.u('uRect'), r.blackout ? -1e6 : (cw - w) / 2, (ch - h) / 2, w, h);
  gl.bindVertexArray(renderer.vao);
  gl.drawArrays(gl.TRIANGLES, 0, 3);

  const mm = Math.floor(t / 60), ss = (t % 60).toFixed(2).padStart(5, '0');
  const preset = player.active >= 0 ? `preset ${player.active === 9 ? 0 : player.active + 1}${player.fading ? ' (fading)' : ''}` : 'no preset';
  const light = renderer.lightOverride >= 0 ? `${LIGHT_MODELS[renderer.lightOverride]} (forced)` : 'per object';
  hud.textContent = `${mm}:${ss} / ${(show.timeline.length / 60 | 0)}:${(show.timeline.length % 60).toFixed(0).padStart(2, '0')}   ${clock.playing ? 'playing' : 'paused'}   ${preset}   lighting: ${light}${r.blackout ? '   BLACKOUT' : ''}`;
  requestAnimationFrame(n => frame(r, n, now));
}

// ---- keys
addEventListener('keydown', e => {
  const r = running;
  if (!r || e.ctrlKey || e.metaKey || e.altKey || e.target.closest?.('input, textarea')) return;
  const { show, clock, player, renderer } = r;
  if (/^Digit\d$|^Numpad\d$/.test(e.code)) {                       // 1..9, 0: play that preset
    const d = Number(e.code.slice(-1)), slot = d === 0 ? 9 : d - 1, pr = show.presets[slot];
    if (pr) player.start(show, pr, slot); else status.textContent = `There is no preset on key ${d}.`;
  } else if (e.code === 'Space') { clock.toggle(); if (!clock.ctx && clock.playing) startBtn.hidden = true; }
  else if (e.code === 'ArrowRight') { clock.seek(clock.time + 5); r.dirty = true; }
  else if (e.code === 'ArrowLeft') { clock.seek(clock.time - 5); r.dirty = true; }
  else if (e.code === 'Home') { clock.seek(0); r.dirty = true; }
  else if (e.code === 'KeyL') renderer.lightOverride = renderer.lightOverride >= 3 ? -1 : renderer.lightOverride + 1;
  else if (e.code === 'KeyB') r.blackout = !r.blackout;
  else if (e.code === 'KeyF') (document.fullscreenElement ? document.exitFullscreen() : $('player').requestFullscreen()).catch(() => {});
  else if (e.code === 'KeyH') document.body.classList.toggle('hide-info');
  else return;
  e.preventDefault();
});

start();
