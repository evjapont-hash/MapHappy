// The media pool in the browser: the built-in test pattern, generators, videos, and stand-ins for missing files.
// Every source has .tex (a WebGL texture, row 0 = bottom), .w, .h and update(dt).
import { compileProgram, makeTarget, setLinearClamp, solidTexture } from './gl.js';
import * as S from './shaders.gen.js';

// What each generator type's parameters are (as in the desktop app's GeneratorDefs.h): only what the player needs.
const CHOICES = { font: ['sans', 'pixel'], align: ['left', 'center', 'right'], style: ['linear', 'radial'] };
const BODIES = { testcard: S.kGenTestCardFS, grid: S.kGenGridFS, text: S.kGenTextFS, solid: S.kGenSolidFS, gradient: S.kGenGradientFS, strobe: S.kGenStrobeFS, lines: S.kGenLinesFS };
const TEXT_LINE = 96;                       // pixels per line of a text image (kTextLineHeight)

const programs = new WeakMap();             // gl -> Map(type -> program): each generator shader is compiled once
function generatorProgram(gl, type) {
  if (!programs.has(gl)) programs.set(gl, new Map());
  const m = programs.get(gl);
  if (!m.has(type)) m.set(type, compileProgram(gl, S.kFullscreenVS, S.kGenCommonFS + BODIES[type], `generator ${type}`));
  return m.get(type);
}

// Text to a coverage image (white on transparent, read back as one channel), like TextRaster.cpp.
function rasterizeText(text, font, align) {
  if (!text) return null;
  const lines = text.replace(/\r/g, '').replace(/\t/g, ' ').split('\n');
  const c = document.createElement('canvas'), ctx = c.getContext('2d', { willReadFrequently: true });
  const css = font === 'pixel' ? `${Math.round(TEXT_LINE * 0.8)}px monospace` : `${Math.round(TEXT_LINE * 0.78)}px system-ui, "Segoe UI", Arial, sans-serif`;
  ctx.font = css;
  const widths = lines.map(l => ctx.measureText(l).width), maxW = Math.max(1, Math.ceil(Math.max(...widths)));
  c.width = maxW; c.height = lines.length * TEXT_LINE;
  ctx.font = css; ctx.fillStyle = '#fff'; ctx.textBaseline = 'middle';
  lines.forEach((l, i) => {
    const x = align === 'left' ? 0 : align === 'right' ? maxW - widths[i] : (maxW - widths[i]) / 2;
    ctx.fillText(l, x, i * TEXT_LINE + TEXT_LINE / 2);
  });
  const rgba = ctx.getImageData(0, 0, c.width, c.height).data, a = new Uint8Array(c.width * c.height);
  for (let i = 0; i < a.length; i++) a[i] = rgba[i * 4 + 3];
  return { w: c.width, h: c.height, alpha: a };
}

class Generator {
  constructor(gl, entry) {
    this.gl = gl; this.type = entry.type; this.time = 0; this.playing = entry.playing !== false;
    this.params = entry.params || {};
    this.w = Math.max(16, Math.min(8192, entry.width | 0 || 1920)); this.h = Math.max(16, Math.min(8192, entry.height | 0 || 1080));
    this.prog = generatorProgram(gl, this.type);
    this.target = makeTarget(gl, this.w, this.h);
    this.tex = this.target.tex;
    this.textTex = null; this.textW = 0; this.textH = 0;
    this.makeText();
    this.render();
  }
  num(key, def = 0) {
    const v = this.params[key];
    if (typeof v === 'number') return v;
    if (typeof v === 'boolean') return v ? 1 : 0;
    if (typeof v === 'string' && CHOICES[key]) return Math.max(0, CHOICES[key].indexOf(v));
    return def;
  }
  makeText() {
    let text = '';
    if (this.type === 'text') text = String(this.params.text ?? '');
    if (this.type === 'testcard') {
      text = String(this.params.label ?? '');
      if (this.params.showResolution !== false) text += (text ? '\n' : '') + `${this.w} x ${this.h}`;
    }
    const img = (this.type === 'text' || this.type === 'testcard') ? rasterizeText(text, this.type === 'text' ? this.params.font : 'sans', this.type === 'text' ? this.params.align : 'center') : null;
    if (!img) return;
    const gl = this.gl;
    this.textTex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, this.textTex);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.R8, img.w, img.h, 0, gl.RED, gl.UNSIGNED_BYTE, img.alpha);   // row 0 = top line, as the shader expects
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 4);
    setLinearClamp(gl);
    this.textW = img.w; this.textH = img.h;
  }
  update(dt) { if (this.playing) this.time += dt; this.render(); }
  render() {
    const gl = this.gl, p = this.prog, t = this.target;
    gl.bindFramebuffer(gl.FRAMEBUFFER, t.fbo);
    gl.viewport(0, 0, t.w, t.h);
    gl.disable(gl.BLEND);
    gl.useProgram(p);
    gl.uniform2f(p.u('uRes'), t.w, t.h);
    gl.uniform1f(p.u('uTime'), this.time);
    const x = this.time * this.num('rate', 4);
    gl.uniform1f(p.u('uPhase'), x - Math.floor(x));
    gl.uniform2f(p.u('uTextSize'), this.textTex ? this.textW : 0, this.textTex ? this.textH : 0);
    gl.uniform1i(p.u('uText'), 0);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.textTex);
    for (const [key, v] of Object.entries(this.params)) {
      const loc = p.u('u_' + key);
      if (!loc) continue;
      if (Array.isArray(v)) gl.uniform4f(loc, v[0] ?? 0, v[1] ?? 0, v[2] ?? 0, v[3] ?? 1);
      else gl.uniform1f(loc, this.num(key));
    }
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  }
}

class Pattern {                              // the built-in test pattern (media id 1)
  constructor(gl, playing) {
    this.gl = gl; this.time = 0; this.playing = playing; this.w = 1280; this.h = 720;
    this.prog = compileProgram(gl, S.kFullscreenVS, S.kPatternFS, 'test pattern');
    this.target = makeTarget(gl, this.w, this.h); this.tex = this.target.tex;
    this.update(0);
  }
  update(dt) {
    if (this.playing) this.time += dt;
    const gl = this.gl;
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.target.fbo);
    gl.viewport(0, 0, this.w, this.h);
    gl.disable(gl.BLEND);
    gl.useProgram(this.prog);
    gl.uniform2f(this.prog.u('uRes'), this.w, this.h);
    gl.uniform1f(this.prog.u('uTime'), this.time);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  }
}

class Video {
  constructor(gl, url, loop, playing) {
    this.gl = gl; this.w = 16; this.h = 9; this.ready = false; this.wantPlay = playing;
    this.tex = solidTexture(gl, 0, 0, 0);
    const v = this.video = document.createElement('video');
    v.src = url; v.muted = true; v.loop = loop; v.playsInline = true; v.crossOrigin = 'anonymous'; v.preload = 'auto';
    v.addEventListener('loadeddata', () => { this.ready = true; this.w = v.videoWidth; this.h = v.videoHeight; });
    v.addEventListener('error', () => { this.failed = true; });
  }
  start() { if (this.wantPlay) this.video.play().catch(() => {}); }
  update() {
    if (!this.ready || this.video.readyState < 2) return;
    const gl = this.gl;
    gl.bindTexture(gl.TEXTURE_2D, this.tex);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);              // row 0 = bottom, like every other source
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, this.video);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
  }
}

class Missing { constructor(gl) { this.tex = solidTexture(gl, 70, 28, 28); this.w = 64; this.h = 36; } update() {} }

// Builds the pool from a project file's "media" list. `resolve(path, rel)` turns stored paths into URLs.
export function buildMedia(gl, list, resolve, warn) {
  const pool = new Map();
  let patternPlaying = true;
  for (const m of Array.isArray(list) ? list : []) if (m && m.id === 1 && m.type === 'pattern') patternPlaying = m.playing !== false;
  pool.set(1, new Pattern(gl, patternPlaying));
  for (const m of Array.isArray(list) ? list : []) {
    if (!m || typeof m !== 'object' || !(m.id > 1) || pool.has(m.id)) continue;
    try {
      if (m.kind === 'video') {
        const url = resolve(m.path, m.pathRel);
        if (!url) { warn(`Can't find the video "${m.name}"`); pool.set(m.id, new Missing(gl)); continue; }
        pool.set(m.id, new Video(gl, url, m.loop !== false, m.playing !== false));
      } else if (BODIES[m.type]) {
        pool.set(m.id, new Generator(gl, m));
      } else if (m.type !== 'pattern') {
        warn(`Skipped "${m.name}", a generator this player doesn't know`);
      }
    } catch (e) { warn(String(e.message || e)); pool.set(m.id, new Missing(gl)); }
  }
  return pool;
}
