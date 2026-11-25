// The web editor's Stage view: the show as the projector will show it, with the surfaces' outlines and handles
// drawn on a 2D overlay. Drag a corner to warp, drag inside to move, drag a line's ends or a freeform's points;
// double-click a freeform edge to add a point, right-click a point to delete it.
// In "input" mode it shows the selected surface's picture instead, and the handles choose the part it shows.
import { compileProgram } from '../gl.js';
import { kFullscreenVS } from '../shaders.gen.js';
import { squareToQuad, invert3, mapPoint, quadCenter, effectiveOut, effectiveIn } from '../math.js';
import { isShown } from '../show.js';
import { isConvex, scaleFactor, scaleQuad } from '../editor-ops.js';

const BLIT_FS = `#version 300 es
precision highp float;
uniform sampler2D uTex;
uniform vec4 uRect;      // where the picture sits in the canvas, in pixels: x, y, w, h (y from the bottom)
out vec4 frag;
void main() {
  vec2 uv = (gl_FragCoord.xy - uRect.xy) / uRect.zw;
  frag = (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) ? vec4(0.06, 0.06, 0.07, 1.0) : vec4(texture(uTex, uv).rgb, 1.0);
}`;

const ACCENT = '#fff', DIM = 'rgba(255,255,255,0.45)', MASK = 'rgba(255,255,255,0.95)', DASH_MASK = [6, 4], DASH_ANIM = [2, 4];
const circle = [...Array(72)].map((_, i) => { const a = 2 * Math.PI * i / 72; return { x: 0.5 + 0.5 * Math.cos(a), y: 0.5 + 0.5 * Math.sin(a) }; });

// A surface's outline in its own unit square.
function outlineUnit(s) {
  if (s.shape === 'circle') return circle;
  if (s.shape === 'triangle') return [{ x: 0.5, y: 0 }, { x: 1, y: 1 }, { x: 0, y: 1 }];
  if (s.shape === 'freeform') return s.poly;
  return [{ x: 0, y: 0 }, { x: 1, y: 0 }, { x: 1, y: 1 }, { x: 0, y: 1 }];
}
function mapAll(quad, pts) { const H = squareToQuad(quad); return H ? pts.map(p => mapPoint(H, p)) : []; }
function inside(poly, p) {
  let r = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++)
    if ((poly[i].y > p.y) !== (poly[j].y > p.y) && p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x) r = !r;
  return r;
}
const clamp01 = v => ({ x: Math.max(0, Math.min(1, v.x)), y: Math.max(0, Math.min(1, v.y)) });

export class StageView {
  constructor(ed, glCanvas, overlay) {
    this.ed = ed; this.canvas = glCanvas; this.overlay = overlay; this.ctx = overlay.getContext('2d');
    this.blit = compileProgram(ed.gl, kFullscreenVS, BLIT_FS, 'stage view');
    this.mode = 'output';               // 'output' | 'input'
    this.drag = null;
    this.hideHandles = false;           // Present mode
    overlay.addEventListener('pointerdown', e => this.down(e));
    overlay.addEventListener('pointermove', e => this.move(e));
    overlay.addEventListener('pointerup', e => this.up(e));
    overlay.addEventListener('dblclick', e => this.dbl(e));
    overlay.addEventListener('contextmenu', e => this.context(e));
  }

  // What the view shows: the stage, or (input mode) the selected surface's picture.
  picture() {
    const ed = this.ed, s = ed.selected();
    if (this.mode === 'input' && s) {
      const src = ed.show.media.get(s.media);
      if (src) return { tex: src.tex, w: src.w, h: src.h, surface: s };
    }
    return { tex: ed.stageRT.tex, w: ed.show.outW, h: ed.show.outH, surface: null };
  }
  // Where the picture sits in the canvas (CSS pixels, y down).
  layout(pic) {
    const W = this.overlay.clientWidth, H = this.overlay.clientHeight, m = 12;
    const k = Math.min((W - 2 * m) / pic.w, (H - 2 * m) / pic.h), w = Math.max(1, pic.w * k), h = Math.max(1, pic.h * k);
    return { x: (W - w) / 2, y: (H - h) / 2, w, h };
  }
  toScreen(r, p) { return { x: r.x + p.x * r.w, y: r.y + p.y * r.h }; }
  toNorm(r, e) { const b = this.overlay.getBoundingClientRect(); return { x: (e.clientX - b.left - r.x) / r.w, y: (e.clientY - b.top - r.y) / r.h }; }

  draw() {
    const gl = this.ed.gl, dpr = devicePixelRatio || 1;
    const cw = Math.round(this.canvas.clientWidth * dpr), ch = Math.round(this.canvas.clientHeight * dpr);
    if (this.canvas.width !== cw || this.canvas.height !== ch) { this.canvas.width = cw; this.canvas.height = ch; }
    if (this.overlay.width !== cw || this.overlay.height !== ch) { this.overlay.width = cw; this.overlay.height = ch; }
    const pic = this.picture(), r = this.layout(pic);
    this.rect = r; this.pic = pic;
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, cw, ch);
    gl.useProgram(this.blit);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.ed.blackout ? null : pic.tex);
    gl.uniform1i(this.blit.u('uTex'), 0);
    gl.uniform4f(this.blit.u('uRect'), r.x * dpr, ch - (r.y + r.h) * dpr, r.w * dpr, r.h * dpr);
    gl.bindVertexArray(this.ed.renderer.vao);
    gl.drawArrays(gl.TRIANGLES, 0, 3);

    const c = this.ctx;
    c.setTransform(dpr, 0, 0, dpr, 0, 0);
    c.clearRect(0, 0, cw, ch);
    if (this.hideHandles) return;
    c.strokeStyle = '#333'; c.lineWidth = 1; c.strokeRect(r.x - 0.5, r.y - 0.5, r.w + 1, r.h + 1);
    if (pic.surface) this.drawInput(r, pic.surface); else this.drawOutput(r);
  }

  poly(r, pts, color, width = 1, close = true, dash = []) {
    const c = this.ctx;
    if (pts.length < 2) return;
    c.beginPath();
    pts.forEach((p, i) => { const q = this.toScreen(r, p); i ? c.lineTo(q.x, q.y) : c.moveTo(q.x, q.y); });
    if (close) c.closePath();
    c.strokeStyle = 'rgba(0,0,0,0.5)'; c.lineWidth = width + 2; c.stroke();      // a dark edge, so white shows on any picture
    c.setLineDash(dash); c.strokeStyle = color; c.lineWidth = width; c.stroke(); c.setLineDash([]);
  }
  handle(r, p, kind = 'corner', color = ACCENT) {
    const c = this.ctx, q = this.toScreen(r, p);
    c.beginPath();
    if (kind === 'square') c.rect(q.x - 5, q.y - 5, 10, 10); else c.arc(q.x, q.y, kind === 'small' ? 5 : 7, 0, Math.PI * 2);
    const hollow = color === MASK;                                // surfaces: white handles; masks: black ones
    c.lineWidth = 4; c.strokeStyle = '#000'; c.stroke();
    c.fillStyle = hollow ? '#000' : color; c.fill();
    if (hollow) { c.strokeStyle = color; c.lineWidth = 2; c.stroke(); }
  }
  cross(r, p) {
    const c = this.ctx, q = this.toScreen(r, p);
    c.strokeStyle = ACCENT; c.lineWidth = 1.5;
    c.beginPath(); c.moveTo(q.x - 9, q.y); c.lineTo(q.x + 9, q.y); c.moveTo(q.x, q.y - 9); c.lineTo(q.x, q.y + 9); c.stroke();
    c.beginPath(); c.arc(q.x, q.y, 6, 0, Math.PI * 2); c.stroke();
  }

  drawOutput(r) {
    const ed = this.ed, show = ed.show, aspect = show.outW / show.outH, sel = ed.selected();
    for (const s of show.surfaces) {
      if (!isShown(show, s) || s === sel) continue;
      this.poly(r, mapAll(effectiveOut(s, aspect), outlineUnit(s)), DIM);
    }
    if (!sel) return;
    if (sel.shape === 'line') {
      this.poly(r, effectiveOut(sel, aspect), ACCENT, 1.5);
      this.poly(r, [sel.lineA, sel.lineB], 'rgba(255,255,255,0.4)', 1, false);
      this.handle(r, sel.lineA); this.handle(r, sel.lineB);
      this.cross(r, { x: (sel.lineA.x + sel.lineB.x) / 2, y: (sel.lineA.y + sel.lineB.y) / 2 });
      return;
    }
    const eo = effectiveOut(sel, aspect);
    if (eo.some((p, i) => Math.abs(p.x - sel.out[i].x) + Math.abs(p.y - sel.out[i].y) > 1e-5)) this.poly(r, mapAll(eo, outlineUnit(sel)), ACCENT, 1.5, true, DASH_ANIM);   // where animation puts it
    this.poly(r, sel.out, ACCENT, 1.5);
    if (sel.shape !== 'quad' && sel.shape !== 'object3d') this.poly(r, mapAll(sel.out, outlineUnit(sel)), ACCENT, 2);
    this.poly(r, [sel.out[0], sel.out[2]], 'rgba(255,255,255,0.3)', 1, false); this.poly(r, [sel.out[1], sel.out[3]], 'rgba(255,255,255,0.3)', 1, false);
    sel.masks.forEach((m, i) => this.poly(r, mapAll(sel.out, maskOutline(m)), m.enabled ? MASK : 'rgba(255,255,255,0.35)', i === this.ed.selMask ? 2.5 : 1.5, true, DASH_MASK));
    const mk = this.mask(sel);
    if (mk) { mapAll(sel.out, maskHandles(mk)).forEach(p => this.handle(r, p, 'square', MASK)); return; }       // the selected mask is what you edit
    const free = sel.shape === 'freeform';
    sel.out.forEach(p => this.handle(r, p, free ? 'small' : 'corner'));
    if (free) mapAll(sel.out, sel.poly).forEach(p => this.handle(r, p, 'square'));
    this.cross(r, quadCenter(sel.out));
  }

  drawInput(r, s) {
    const ei = effectiveIn(s, this.pic.w / Math.max(1, this.pic.h));
    if (ei.some((p, i) => Math.abs(p.x - s.in[i].x) + Math.abs(p.y - s.in[i].y) > 1e-5)) this.poly(r, mapAll(ei, outlineUnit(s)), ACCENT, 1.5, true, DASH_ANIM);
    this.poly(r, s.in, ACCENT, 1.5);
    if (s.shape !== 'quad' && s.shape !== 'object3d' && s.shape !== 'line') this.poly(r, mapAll(s.in, outlineUnit(s)), ACCENT, 2);
    s.in.forEach(p => this.handle(r, p));
    this.cross(r, quadCenter(s.in));
  }

  // ---------------------------------------------------------------- mouse
  screenDist(a, e) { const b = this.overlay.getBoundingClientRect(), q = this.toScreen(this.rect, a); return Math.hypot(q.x - (e.clientX - b.left), q.y - (e.clientY - b.top)); }

  hit(e) {
    const ed = this.ed, s = ed.selected(), p = this.toNorm(this.rect, e);
    if (this.pic.surface) {                                     // input mode: the input quad's corners, or inside it
      const i = s.in.findIndex(q => this.screenDist(q, e) < 12);
      if (i >= 0) return { kind: 'inCorner', i };
      if (inside(s.in, p)) return { kind: 'inMove' };
      return null;
    }
    const aspect = ed.show.outW / ed.show.outH;
    const mk = s && this.mask(s);
    if (mk) {
      const hs = mapAll(s.out, maskHandles(mk)), i = hs.findIndex(q => this.screenDist(q, e) < 10);
      if (i >= 0) return { kind: mk.shape === 2 ? 'maskPoint' : 'maskCorner', i };
      if (inside(mapAll(s.out, maskOutline(mk)), p)) return { kind: 'maskMove' };
    }
    if (s) {
      if (s.shape === 'line') {
        if (this.screenDist(s.lineA, e) < 12) return { kind: 'lineA' };
        if (this.screenDist(s.lineB, e) < 12) return { kind: 'lineB' };
      } else {
        if (s.shape === 'freeform') {
          const pts = mapAll(s.out, s.poly), i = pts.findIndex(q => this.screenDist(q, e) < 9);
          if (i >= 0) return { kind: 'point', i };
        }
        const i = s.out.findIndex(q => this.screenDist(q, e) < 12);
        if (i >= 0) return { kind: 'corner', i };
      }
      const base = s.shape === 'line' ? effectiveOut(s, aspect) : s.out;
      if (inside(mapAll(base, outlineUnit(s)), p) || inside(mapAll(effectiveOut(s, aspect), outlineUnit(s)), p)) return { kind: 'move' };
    }
    for (const o of ed.show.surfaces)                           // the top surface you can SEE under the mouse
      if (isShown(ed.show, o) && inside(mapAll(effectiveOut(o, aspect), outlineUnit(o)), p)) return { kind: 'move', select: o.id };
    return { kind: 'none' };
  }

  down(e) {
    if (e.button !== 0) return;
    const h = this.hit(e);
    if (!h) return;
    if (h.kind === 'none') { this.ed.deselect(); return; }        // clicked off every surface
    if (h.select) this.ed.select(h.select);
    else { this.ed.sel.key = null; this.ed.sel.audio = null; }     // (Delete now means the surface)
    this.drag = { ...h, last: this.toNorm(this.rect, e) };
    this.overlay.setPointerCapture(e.pointerId);
  }

  move(e) {
    if (!this.drag) {                                            // hover feedback
      const h = this.rect && this.hit(e);
      this.overlay.style.cursor = !h || h.kind === 'none' ? 'default' : h.kind.endsWith('ove') ? 'move' : 'pointer';
      return;
    }
    const ed = this.ed, s = ed.selected(), p = this.toNorm(this.rect, e), d = this.drag;
    if (!s) return;
    const dx = p.x - d.last.x, dy = p.y - d.last.y;
    if (d.kind === 'corner' || d.kind === 'inCorner') {
      const q = d.kind === 'corner' ? s.out : s.in, old = q[d.i];
      if (e.shiftKey) {                                          // Shift: the whole shape grows or shrinks, keeping its proportions
        const anchor = { ...q[(d.i + 2) % 4] };
        scaleQuad(q, anchor, scaleFactor(anchor, old, p, this.rect.w / this.rect.h));
      } else q[d.i] = { x: p.x, y: p.y };
      if (!isConvex(q)) q[d.i] = old;                            // a twisted quad has no valid projection
    } else if (d.kind === 'move') {
      for (const q of s.out) { q.x += dx; q.y += dy; }
      if (s.shape === 'line') { s.lineA.x += dx; s.lineA.y += dy; s.lineB.x += dx; s.lineB.y += dy; }
    } else if (d.kind === 'inMove') {
      for (const q of s.in) { q.x += dx; q.y += dy; }
    } else if (d.kind === 'lineA' || d.kind === 'lineB') {
      const other = d.kind === 'lineA' ? s.lineB : s.lineA;
      if (e.shiftKey) {                                          // Shift: the line keeps its angle
        const k = scaleFactor(other, s[d.kind], p, this.rect.w / this.rect.h);
        s[d.kind] = { x: other.x + (s[d.kind].x - other.x) * k, y: other.y + (s[d.kind].y - other.y) * k };
      } else if (Math.hypot(p.x - other.x, p.y - other.y) > 0.003) s[d.kind] = { x: p.x, y: p.y };
    } else if (d.kind.startsWith('mask')) {
      const mk = this.mask(s), u = this.unit(s, p), u0 = this.unit(s, d.last);
      if (!mk || !u || !u0) return;
      if (d.kind === 'maskPoint') mk.points[d.i] = clamp01(u);
      else if (d.kind === 'maskMove') {
        let mx = u.x - u0.x, my = u.y - u0.y;
        if (mk.shape === 2) mk.points = mk.points.map(q => clamp01({ x: q.x + mx, y: q.y + my }));
        else {                                                    // the box keeps its size and stays inside the surface
          mx = Math.max(-mk.min.x, Math.min(1 - mk.max.x, mx)); my = Math.max(-mk.min.y, Math.min(1 - mk.max.y, my));
          mk.min = { x: mk.min.x + mx, y: mk.min.y + my }; mk.max = { x: mk.max.x + mx, y: mk.max.y + my };
        }
      } else {                                                    // a box corner: the opposite corner stays put
        const c = clamp01(u), o = maskHandles(mk)[(d.i + 2) % 4];
        if (Math.abs(c.x - o.x) > 0.01 && Math.abs(c.y - o.y) > 0.01) {
          mk.min = { x: Math.min(c.x, o.x), y: Math.min(c.y, o.y) }; mk.max = { x: Math.max(c.x, o.x), y: Math.max(c.y, o.y) };
          d.i = (c.y > o.y ? (c.x > o.x ? 2 : 3) : (c.x > o.x ? 1 : 0));   // dragged past the other side: it is another corner now
        }
      }
    } else if (d.kind === 'point') {
      const H = squareToQuad(s.out), inv = H && invert3(H);
      if (e.shiftKey) {                                          // Shift: the whole outline scales, about the point furthest away
        const pts = mapAll(s.out, s.poly), from = pts[d.i], far = q => Math.hypot((q.x - from.x) * this.rect.w, (q.y - from.y) * this.rect.h);
        const anchor = pts.reduce((a, b) => (far(b) > far(a) ? b : a));
        scaleQuad(s.out, anchor, scaleFactor(anchor, from, p, this.rect.w / this.rect.h));
      } else if (inv) s.poly[d.i] = clamp01(mapPoint(inv, p));         // points stay inside the quad, where the picture is
    }
    d.last = p;
    ed.touched();
  }

  up() { if (this.drag) { this.drag = null; this.ed.commit(); } }

  // The selected surface's selected mask (masks are edited on the stage while one is selected), or null.
  mask(s) { const i = this.ed.selMask; return this.pic?.surface ? null : (i >= 0 && s.masks[i]) || null; }
  // A stage point in a surface's own unit square.
  unit(s, p) { const H = squareToQuad(s.out), inv = H && invert3(H); return inv ? mapPoint(inv, p) : null; }

  // Freeform: double-click an edge to add a point there.
  dbl(e) {
    const s = this.ed.selected(), mk = s && this.mask(s);
    if (mk && mk.shape === 2 && !this.pic.surface) { this.addPointOnEdge(e, s, mk.points, 16); return; }
    if (!s || s.shape !== 'freeform' || this.pic.surface) return;
    this.addPointOnEdge(e, s, s.poly, 64);
  }

  // Adds a point to an outline (in the surface's unit square) where the mouse is on one of its edges.
  addPointOnEdge(e, s, outline, max) {
    if (outline.length >= max) return;
    const pts = mapAll(s.out, outline), b = this.overlay.getBoundingClientRect(), mx = e.clientX - b.left, my = e.clientY - b.top;
    let best = -1, bd = 10, at = null;
    pts.forEach((a, i) => {
      const A = this.toScreen(this.rect, a), B = this.toScreen(this.rect, pts[(i + 1) % pts.length]);
      const ex = B.x - A.x, ey = B.y - A.y, t = Math.max(0, Math.min(1, ((mx - A.x) * ex + (my - A.y) * ey) / Math.max(1e-6, ex * ex + ey * ey)));
      const dd = Math.hypot(A.x + ex * t - mx, A.y + ey * t - my);
      if (dd < bd) { bd = dd; best = i; at = { x: (A.x + ex * t - this.rect.x) / this.rect.w, y: (A.y + ey * t - this.rect.y) / this.rect.h }; }
    });
    const inv = squareToQuad(s.out) && invert3(squareToQuad(s.out));
    if (best < 0 || !inv) return;
    outline.splice(best + 1, 0, clamp01(mapPoint(inv, at)));
    this.ed.commit();
  }

  // Freeform: right-click a point to delete it (three stay).
  context(e) {
    e.preventDefault();
    const s = this.ed.selected(), mk = s && this.mask(s);
    if (mk && mk.shape === 2 && !this.pic.surface) {
      const i = mapAll(s.out, mk.points).findIndex(q => this.screenDist(q, e) < 10);
      if (i >= 0 && mk.points.length > 3) { mk.points.splice(i, 1); this.ed.commit(); }
      return;
    }
    if (!s || s.shape !== 'freeform' || this.pic.surface || s.poly.length <= 3) return;
    const i = mapAll(s.out, s.poly).findIndex(q => this.screenDist(q, e) < 10);
    if (i >= 0) { s.poly.splice(i, 1); this.ed.commit(); }
  }
}

// What you drag on a mask: a polygon's points, or a box's four corners (top-left, top-right, bottom-right, bottom-left).
function maskHandles(m) { return m.shape === 2 ? m.points : [m.min, { x: m.max.x, y: m.min.y }, m.max, { x: m.min.x, y: m.max.y }]; }

function maskOutline(m) {
  if (m.shape === 2) return m.points;
  if (m.shape === 0) return [m.min, { x: m.max.x, y: m.min.y }, m.max, { x: m.min.x, y: m.max.y }];
  const c = { x: (m.min.x + m.max.x) / 2, y: (m.min.y + m.max.y) / 2 }, rx = (m.max.x - m.min.x) / 2, ry = (m.max.y - m.min.y) / 2;
  return [...Array(48)].map((_, i) => { const a = 2 * Math.PI * i / 48; return { x: c.x + rx * Math.cos(a), y: c.y + ry * Math.sin(a) }; });
}
