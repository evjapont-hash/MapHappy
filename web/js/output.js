// The projector output window (output.html, opened from the editor's Output button): only the stage, full screen.
// It runs its own renderer (windows can't share WebGL textures) and follows the editor over a BroadcastChannel:
//   { type: 'show', json }   the show, whenever it changes        { type: 'frame', ... }  the live state, every frame
// The files you added in the editor come from the same browser storage (IndexedDB).
import { makeTarget, compileProgram } from './gl.js';
import { readShow } from './show.js';
import { createSource, buildMedia } from './media.js';
import { Renderer } from './renderer.js';
import { FileStore } from './files.js';
import { PARAMS } from './params.js';
import { kFullscreenVS } from './shaders.gen.js';

export const CHANNEL = 'maphappy-output';
export const PARAM_KEYS = [...PARAMS.keys()];             // the order of the values in a 'frame' message

const BLIT_FS = `#version 300 es
precision highp float;
uniform sampler2D uTex;
uniform vec4 uRect;
out vec4 frag;
void main() {
  vec2 uv = (gl_FragCoord.xy - uRect.xy) / uRect.zw;
  frag = (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) ? vec4(0.0, 0.0, 0.0, 1.0) : vec4(texture(uTex, uv).rgb, 1.0);
}`;

export class Output {
  constructor(canvas) {
    this.canvas = canvas;
    this.gl = canvas.getContext('webgl2', { antialias: false, alpha: false, preserveDrawingBuffer: true });
    this.renderer = new Renderer(this.gl);
    this.blit = compileProgram(this.gl, kFullscreenVS, BLIT_FS, 'output');
    this.files = new FileStore(new URL('show/show.maphappy', location.href).href);
    this.show = null; this.frameState = null; this.stretch = false; this.blackout = false; this.lastJson = '';
  }

  async setShow(json) {
    const text = JSON.stringify(json);
    if (text === this.lastJson) return;
    this.lastJson = text;
    const wanted = [...(json.media || []).filter(m => m.kind === 'video').map(m => m.pathRel || m.path), ...(json.surfaces || []).filter(s => s.mesh === 'file').map(s => s.meshPath)];
    if (wanted.some(p => p && !this.files.urls.has(p))) await this.files.init();          // a file just added in the editor
    const old = this.show, show = readShow(json, this.files.resolve), warn = () => {};
    if (old) {                                                                              // keep media whose settings didn't change
      show.media = new Map();
      for (const m of show.mediaList) {
        const prev = old.media.get(m.id), prevEntry = old.mediaList.find(x => x.id === m.id);
        if (prev && (m.id === 1 || (prevEntry && JSON.stringify(prevEntry) === JSON.stringify(m)))) { show.media.set(m.id, prev); if (prev.refresh) prev.refresh(m); }
        else { const src = createSource(this.gl, m, this.files.resolve, warn); if (src) { show.media.set(m.id, src); src.start?.(); } }
      }
    } else { show.media = buildMedia(this.gl, show.mediaList, this.files.resolve, warn); for (const m of show.media.values()) m.start?.(); }
    if (!this.stage || this.stage.w !== show.outW || this.stage.h !== show.outH) this.stage = makeTarget(this.gl, show.outW, show.outH);
    this.show = show;
  }

  // The editor's live values: the playhead, every surface's settings (timeline, presets, sliders), folders.
  applyFrame(f) {
    const show = this.show;
    if (!show || !f) return;
    for (const [id, media, values] of f.surfaces) {
      const s = show.surfaces.find(x => x.id === id);
      if (!s) continue;
      if (show.media.has(media)) s.media = media;
      PARAM_KEYS.forEach((k, i) => { if (values[i] !== null && values[i] !== undefined) s.p[k] = values[i]; });
      s.clock = f.t;
    }
    for (const [id, visible] of f.folders) { const fo = show.folders.get(id); if (fo) fo.visible = visible; }
    this.blackoutFromEditor = !!f.blackout;
  }

  draw(dt) {
    const gl = this.gl, show = this.show;
    const dpr = devicePixelRatio || 1, cw = Math.round(this.canvas.clientWidth * dpr), ch = Math.round(this.canvas.clientHeight * dpr);
    if (this.canvas.width !== cw || this.canvas.height !== ch) { this.canvas.width = cw; this.canvas.height = ch; }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null); gl.viewport(0, 0, cw, ch);
    if (!show) { gl.clearColor(0, 0, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT); return; }
    this.applyFrame(this.frameState);
    for (const m of show.media.values()) m.update(dt);
    this.renderer.renderStage(this.stage, show);
    const k = Math.min(cw / show.outW, ch / show.outH);
    const w = this.stretch ? cw : show.outW * k, h = this.stretch ? ch : show.outH * k;
    gl.bindFramebuffer(gl.FRAMEBUFFER, null); gl.viewport(0, 0, cw, ch);
    gl.useProgram(this.blit);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.stage.tex);
    gl.uniform1i(this.blit.u('uTex'), 0);
    const dark = this.blackout || this.blackoutFromEditor;
    gl.uniform4f(this.blit.u('uRect'), dark ? -1e6 : (cw - w) / 2, (ch - h) / 2, w, h);
    gl.bindVertexArray(this.renderer.vao);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }
}

// The editor side: what to send.
export function frameMessage(show, t, blackout) {
  return {
    type: 'frame', t, blackout,
    surfaces: show.surfaces.map(s => [s.id, s.media, PARAM_KEYS.map(k => s.p[k])]),
    folders: [...show.folders.values()].map(f => [f.id, f.visible]),
  };
}
