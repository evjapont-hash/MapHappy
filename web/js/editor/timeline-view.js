// The web editor's timeline: a ruler with seconds and beats, the song's waveform, and one row per animated
// parameter with its keyframes. Click or drag the ruler to move the playhead; drag a keyframe to move it (it snaps
// to beats and markers); double-click a row to add a keyframe; right-click one for its curve or to delete it.
// Mouse wheel zooms, Shift + wheel scrolls; over the track names the wheel scrolls up and down. Drag a row's bottom
// edge to make it taller or shorter. A parameter row shows its value's curve, and its keyframes sit on it: drag one
// sideways to change its time and up or down to change its value (Shift: only the value; Ctrl: only the time). Scroll bars along the bottom and the right.
import { keysOf, moveKey, deleteKey, setKey, snapTime, beatGrid, trackLabel, removeTrack, moveAudioClip, trimAudioClip } from '../editor-ops.js';
import { evaluateTrack } from '../timeline.js';
import { parsePath, PARAMS, cleanValue, formatValue } from '../params.js';
import { el } from './ui.js';

const RULER = 24, AUDIO_H = 64, ROW_H = 56, LABEL_W = 190, SB = 12, EDGE = 4;
const HEIGHTS = 'maphappy-editor-row-heights';
const MIN_H = { audio: 32, param: 18 }, MAX_H = 480;
const C = { bg: '#000', row: '#0a0a0a', rowAlt: '#050505', line: '#1f1f1f', text: '#9a9a9a', dim: '#8a8a8a', accent: '#fff', ruler: '#0a0a0a', labels: '#0a0a0a',
            key: '#b8b8b8', keySel: '#fff', clip: 'rgba(255,255,255,0.07)', wave: 'rgba(255,255,255,0.75)', beat: 'rgba(255,255,255,0.06)', bar: 'rgba(255,255,255,0.14)', marker: '#8a8a8a' };

export class TimelineView {
  constructor(ed, canvas) {
    this.ed = ed; this.canvas = canvas; this.ctx = canvas.getContext('2d');
    this.pps = 60;                 // pixels per second
    this.scroll = 0;               // seconds at the left edge
    this.vy = 0;                   // pixels scrolled down
    try { this.heights = new Map(Object.entries(JSON.parse(localStorage.getItem(HEIGHTS)) || {})); } catch { this.heights = new Map(); }
    this.drag = null;
    this.peaks = new Map();        // asset id -> { min, max } per 1/200 s
    canvas.addEventListener('pointerdown', e => this.down(e));
    canvas.addEventListener('pointermove', e => this.move(e));
    canvas.addEventListener('pointerup', () => this.up());
    canvas.addEventListener('dblclick', e => this.dbl(e));
    canvas.addEventListener('contextmenu', e => this.context(e));
    canvas.addEventListener('wheel', e => this.wheel(e), { passive: false });
  }

  // Every row (the songs first), where it is drawn: y can be above the ruler (scrolled off) or below the view.
  rows() {
    const tl = this.ed.show.timeline, out = [];
    let y = RULER - this.vy;
    for (const t of [...tl.tracks.filter(t => t.kind === 'audio'), ...tl.tracks.filter(t => t.kind === 'param')]) { const h = this.heightOf(t); out.push({ track: t, y, h }); y += h; }
    return out;
  }
  rowKey(t) { return t.kind === 'audio' ? `audio:${t.id}` : t.path; }
  // A parameter row tall enough to show its curve: its parameter, and where a value is drawn. null for a short row (or an on/off one).
  curve(r) {
    const d = r.track.kind === 'param' ? PARAMS.get(parsePath(r.track.path)?.key) : null;
    if (!d || d.isBool || r.h < 40) return null;
    const span = (d.max - d.min) || 1, top = r.y + 8, h = r.h - 16;
    return { d, yOf: v => top + h - h * Math.max(0, Math.min(1, (v - d.min) / span)), perPx: span / h };
  }
  keyY(r, k) { const c = this.curve(r); return c ? c.yOf(k.key.v) : r.y + r.h / 2; }
  heightOf(t) { return this.heights.get(this.rowKey(t)) ?? (t.kind === 'audio' ? AUDIO_H : ROW_H); }
  setHeight(t, h) { this.heights.set(this.rowKey(t), Math.round(Math.max(MIN_H[t.kind], Math.min(MAX_H, h)))); }
  saveHeights() { try { localStorage.setItem(HEIGHTS, JSON.stringify(Object.fromEntries(this.heights))); } catch { /* storage blocked */ } }
  // The space the rows can use, and how far there is to scroll.
  view() { const W = this.canvas.clientWidth, H = this.canvas.clientHeight; return { W, H, rowsH: Math.max(1, H - RULER - SB), timeW: Math.max(1, W - LABEL_W - SB) }; }
  contentH() { let h = 0; for (const t of this.ed.show.timeline.tracks) h += this.heightOf(t); return h; }
  maxVy() { return Math.max(0, this.contentH() - this.view().rowsH); }
  // How long the timeline is, for the scroll bar: the show, or a bit past its last song, key or marker.
  totalT() {
    const tl = this.ed.show.timeline;
    let end = Math.max(tl.length || 0, this.ed.clock.time);
    for (const t of tl.tracks) { for (const c of t.audioClips || []) end = Math.max(end, c.start + c.duration); for (const c of t.clips || []) end = Math.max(end, c.start + c.duration); }
    for (const m of tl.markers) end = Math.max(end, m.time);
    return Math.max(end + 5, this.view().timeW / this.pps);
  }
  clampScroll() {
    const v = this.view();
    this.scroll = Math.max(0, Math.min(this.scroll, Math.max(0, this.totalT() - v.timeW / this.pps)));
    this.vy = Math.max(0, Math.min(this.vy, this.maxVy()));
  }
  // The scroll bars: where their thumbs are (canvas pixels).
  bars() {
    const v = this.view(), total = this.totalT(), span = v.timeW / this.pps, ch = this.contentH();
    const hw = Math.max(28, v.timeW * Math.min(1, span / total)), hx = LABEL_W + (v.timeW - hw) * (total > span ? this.scroll / (total - span) : 0);
    const vh = Math.max(28, v.rowsH * Math.min(1, v.rowsH / Math.max(1, ch))), mv = this.maxVy(), vyy = RULER + (v.rowsH - vh) * (mv > 0 ? this.vy / mv : 0);
    return { h: { x: hx, y: v.H - SB, w: hw, h: SB, track: [LABEL_W, v.timeW], range: total - span }, v: { x: v.W - SB, y: vyy, w: SB, h: vh, track: [RULER, v.rowsH], range: mv, on: ch > v.rowsH } };
  }
  // The row whose bottom edge is under the point (to drag its height), or null.
  edgeAt(pt) {
    const v = this.view();
    if (pt.y < RULER || pt.y > v.H - SB || pt.x > v.W - SB) return null;
    return this.rows().find(r => Math.abs(pt.y - (r.y + r.h)) <= EDGE && r.y + r.h > RULER) || null;
  }
  xOf(t) { return LABEL_W + (t - this.scroll) * this.pps; }
  tOf(x) { return Math.max(0, this.scroll + (x - LABEL_W) / this.pps); }
  local(e) { const b = this.canvas.getBoundingClientRect(); return { x: e.clientX - b.left, y: e.clientY - b.top }; }

  // min/max of the song per 1/200 s, for the waveform
  peaksFor(id) {
    if (this.peaks.has(id)) return this.peaks.get(id);
    const buf = this.ed.clock.buffers.get(id);
    if (!buf) return null;
    const data = buf.getChannelData(0), per = Math.max(1, Math.floor(buf.sampleRate / 200)), n = Math.ceil(data.length / per);
    const min = new Float32Array(n), max = new Float32Array(n);
    for (let i = 0; i < n; i++) { let lo = 1, hi = -1; for (let k = i * per, e = Math.min(data.length, k + per); k < e; k++) { const v = data[k]; if (v < lo) lo = v; if (v > hi) hi = v; } min[i] = lo; max[i] = hi; }
    const p = { min, max, rate: buf.sampleRate / per };
    this.peaks.set(id, p);
    return p;
  }

  draw() {
    const ed = this.ed, show = ed.show, tl = show.timeline, c = this.ctx, dpr = devicePixelRatio || 1;
    const W = this.canvas.clientWidth, H = this.canvas.clientHeight;
    if (this.canvas.width !== Math.round(W * dpr) || this.canvas.height !== Math.round(H * dpr)) { this.canvas.width = Math.round(W * dpr); this.canvas.height = Math.round(H * dpr); }
    c.setTransform(dpr, 0, 0, dpr, 0, 0);
    c.fillStyle = C.bg; c.fillRect(0, 0, W, H);
    const t0 = this.scroll, t1 = this.tOf(W);
    const now = ed.clock.time;
    if (ed.clock.playing && (now > t1 - 0.5 || now < t0)) this.scroll = Math.max(0, now - (W - LABEL_W) / this.pps * 0.1);   // follow the playhead
    this.clampScroll();

    const rows = this.rows(), bottom = H - SB;
    c.save(); c.beginPath(); c.rect(0, RULER, W, bottom - RULER); c.clip();          // rows scroll under the ruler
    rows.forEach((r, i) => { c.fillStyle = i % 2 ? C.rowAlt : C.row; c.fillRect(0, r.y, W, r.h); c.fillStyle = C.line; c.fillRect(0, r.y + r.h - 1, W, 1); });
    c.restore();

    // beat grid and seconds
    const grid = beatGrid(show);
    c.save(); c.beginPath(); c.rect(LABEL_W, RULER, W - LABEL_W - SB, bottom - RULER); c.clip();
    if (grid && grid.beat * this.pps > 6) {
      const first = Math.ceil((t0 - grid.start) / grid.beat);
      for (let b = first; grid.start + b * grid.beat < t1; b++) {
        const x = this.xOf(grid.start + b * grid.beat);
        c.fillStyle = b % 4 === 0 ? C.bar : C.beat; c.fillRect(Math.round(x), RULER, 1, bottom - RULER);
      }
    }
    // clips and keyframes
    for (const r of rows) {
      const t = r.track;
      if (r.y > bottom || r.y + r.h < RULER) continue;
      if (t.kind === 'audio') {
        for (const clip of t.audioClips) {
          const x0 = this.xOf(clip.start), x1 = this.xOf(clip.start + clip.duration);
          c.fillStyle = 'rgba(255,255,255,0.08)'; c.fillRect(x0, r.y + 2, x1 - x0, r.h - 4);
          if (this.ed.sel.audio?.clip === clip) { c.strokeStyle = C.accent; c.lineWidth = 2; c.strokeRect(x0 + 1, r.y + 3, Math.max(2, x1 - x0 - 2), r.h - 6); }
          c.fillStyle = 'rgba(255,255,255,0.35)'; c.fillRect(x0, r.y + 2, 3, r.h - 4); c.fillRect(x1 - 3, r.y + 2, 3, r.h - 4);   // the trim handles
          const p = this.peaksFor(clip.asset), mid = r.y + r.h / 2, amp = r.h / 2 - 4;
          if (p) {
            c.fillStyle = C.wave;
            for (let x = Math.max(x0, LABEL_W); x < Math.min(x1, W); x++) {
              const ta = (this.tOf(x) - clip.start + clip.offset) * p.rate, tb = (this.tOf(x + 1) - clip.start + clip.offset) * p.rate;
              let lo = 1, hi = -1;
              for (let k = Math.floor(ta); k <= Math.floor(tb) && k < p.min.length; k++) if (k >= 0) { lo = Math.min(lo, p.min[k] * clip.gain); hi = Math.max(hi, p.max[k] * clip.gain); }
              if (hi >= lo) c.fillRect(x, mid - hi * amp, 1, Math.max(1, (hi - lo) * amp));
            }
          } else { c.fillStyle = C.dim; c.font = '11px Inter, system-ui, sans-serif'; c.fillText('(press Play to load the sound)', x0 + 6, mid + 4); }
        }
        continue;
      }
      for (const clip of t.clips) { const x0 = this.xOf(clip.start), x1 = this.xOf(clip.start + clip.duration); c.fillStyle = C.clip; c.fillRect(x0, r.y + 3, Math.max(2, x1 - x0), r.h - 6); }
      const cv = this.curve(r), tall = !!cv, yOf = cv?.yOf;
      if (tall) {                                                 // a tall row shows the value's curve, with the keys on it
        c.beginPath();
        for (let x = Math.max(LABEL_W, this.xOf(t.clips[0]?.start ?? 0)); x <= W - SB; x += 2) { const y = yOf(evaluateTrack(t, this.tOf(x))); x === Math.max(LABEL_W, this.xOf(t.clips[0]?.start ?? 0)) ? c.moveTo(x, y) : c.lineTo(x, y); }
        c.strokeStyle = t.muted ? 'rgba(255,255,255,0.2)' : 'rgba(255,255,255,0.55)'; c.lineWidth = 1.5; c.stroke();
      }
      for (const k of keysOf(t)) {
        const x = this.xOf(k.time), y = this.keyY(r, k), sel = this.isSelected(t, k);
        c.beginPath(); c.moveTo(x, y - 6); c.lineTo(x + 6, y); c.lineTo(x, y + 6); c.lineTo(x - 6, y); c.closePath();
        c.fillStyle = t.muted ? C.dim : sel ? C.keySel : C.key; c.fill();
        if (sel) { c.beginPath(); c.moveTo(x, y - 9.5); c.lineTo(x + 9.5, y); c.lineTo(x, y + 9.5); c.lineTo(x - 9.5, y); c.closePath(); c.strokeStyle = C.keySel; c.lineWidth = 1.5; c.stroke(); }   // selected: a ring
        if (k.key.interp === 'hold') { c.fillStyle = C.bg; c.fillRect(x - 1.5, y - 1.5, 3, 3); }
        if (sel && this.drag?.kind === 'key' && tall) {          // while dragging: its value
          const txt = formatValue(cv.d, k.key.v); c.font = '11px Inter, system-ui, sans-serif';
          const w = c.measureText(txt).width + 8, ty = y - 14 < r.y + 2 ? y + 12 : y - 22;
          c.fillStyle = '#fff'; c.fillRect(x + 10, ty, w, 16); c.fillStyle = '#000'; c.fillText(txt, x + 14, ty + 12);
        }
      }
    }
    // markers and playhead
    for (const m of tl.markers) { const x = this.xOf(m.time); c.fillStyle = C.marker; c.fillRect(Math.round(x), RULER, 1, bottom - RULER); }
    c.restore();
    for (const m of tl.markers) { const x = this.xOf(m.time); if (x >= LABEL_W && x < W - SB) { c.fillStyle = C.marker; c.fillRect(Math.round(x), RULER - 8, 1, 8); } }

    // ruler
    c.fillStyle = C.ruler; c.fillRect(0, 0, W, RULER); c.fillStyle = C.line; c.fillRect(0, RULER - 1, W, 1);
    const stepS = [0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60].find(s => s * this.pps >= 70) || 60;
    c.font = '11px ui-monospace, Consolas, monospace'; c.fillStyle = C.dim;
    for (let s = Math.ceil(t0 / stepS) * stepS; s < t1; s += stepS) {
      const x = this.xOf(s);
      if (x < LABEL_W) continue;
      c.fillRect(Math.round(x), RULER - 7, 1, 7);
      c.fillText(`${Math.floor(s / 60)}:${(s % 60).toFixed(stepS < 1 ? 2 : 0).padStart(stepS < 1 ? 5 : 2, '0')}`, x + 3, 14);
    }
    const px = this.xOf(now);
    if (px >= LABEL_W && px < W - SB) { c.fillStyle = C.accent; c.fillRect(Math.round(px), 0, 2, bottom); c.beginPath(); c.moveTo(px - 5, 0); c.lineTo(px + 7, 0); c.lineTo(px + 1, 8); c.fill(); }

    // labels
    c.fillStyle = C.labels; c.fillRect(0, 0, LABEL_W, H); c.fillStyle = C.line; c.fillRect(LABEL_W - 1, 0, 1, H);
    c.font = '12px Inter, system-ui, Segoe UI, sans-serif';
    c.fillStyle = C.dim; c.fillText(grid ? `${grid.bpm.toFixed(1)} BPM` : 'Tracks', 8, 16);
    c.save(); c.beginPath(); c.rect(0, RULER, LABEL_W, bottom - RULER); c.clip();
    for (const r of rows) {
      c.fillStyle = C.line; c.fillRect(0, r.y + r.h - 1, LABEL_W, 1);
      const t = r.track, selSurface = this.surfaceOf(t) === ed.sel.surface;
      c.fillStyle = t.muted ? C.dim : selSurface ? C.accent : C.text;
      c.fillText(ellipsis(c, trackLabel(show, t), LABEL_W - 58), 8, r.y + r.h / 2 + 4);
      c.fillStyle = t.muted ? '#fff' : C.dim; c.fillText('M', LABEL_W - 36, r.y + r.h / 2 + 4);
      if (t.kind === 'param') { c.fillStyle = C.dim; c.fillText('×', LABEL_W - 18, r.y + r.h / 2 + 4); }
    }
    c.restore();
    if (!rows.some(r => r.track.kind === 'param'))
      { c.fillStyle = C.dim; c.fillText('No animation yet: press the keyframe button next to any setting to set a keyframe at the playhead.', LABEL_W + 12, (rows.length ? rows[rows.length - 1].y + rows[rows.length - 1].h : RULER) + 18); }

    // the scroll bars
    const b = this.bars(), hot = this.drag?.kind;
    c.fillStyle = C.ruler; c.fillRect(0, bottom, W, SB); c.fillRect(W - SB, RULER, SB, bottom - RULER);
    c.fillStyle = hot === 'hbar' ? '#9a9a9a' : '#4a4a4a'; c.fillRect(b.h.x + 2, b.h.y + 3, b.h.w - 4, b.h.h - 6);
    if (b.v.on) { c.fillStyle = hot === 'vbar' ? '#9a9a9a' : '#4a4a4a'; c.fillRect(b.v.x + 3, b.v.y + 2, b.v.w - 6, b.v.h - 4); }
  }

  surfaceOf(t) { const p = parsePath(t.path); return p ? p.id : 0; }
  isSelected(t, k) { const s = this.ed.sel.key; return s && s.track === t && s.clip === k.clip && s.index === k.index; }

  // A song clip under the point, and which part: 'start' / 'end' edge (trim) or 'body' (move).
  audioAt(pt) {
    if (pt.y < RULER || pt.y > this.view().H - SB) return null;
    for (const r of this.rows()) {
      if (r.track.kind !== 'audio' || pt.y < r.y || pt.y >= r.y + r.h) continue;
      for (const clip of r.track.audioClips) {
        const x0 = this.xOf(clip.start), x1 = this.xOf(clip.start + clip.duration);
        if (pt.x < x0 - 4 || pt.x > x1 + 4) continue;
        return { track: r.track, clip, part: pt.x < x0 + 6 ? 'start' : pt.x > x1 - 6 ? 'end' : 'body' };
      }
    }
    return null;
  }

  keyAt(pt) {
    if (pt.y < RULER || pt.y > this.view().H - SB) return { row: null, key: null };
    for (const r of this.rows()) {
      if (r.track.kind !== 'param' || pt.y < r.y || pt.y >= r.y + r.h) continue;
      let best = null, bd = 8;
      const tall = !!this.curve(r);
      for (const k of keysOf(r.track)) {
        const d = Math.abs(this.xOf(k.time) - pt.x) + (tall ? Math.max(0, Math.abs(this.keyY(r, k) - pt.y) - 6) : 0);
        if (d < bd) { bd = d; best = k; }
      }
      return { row: r, key: best };
    }
    return { row: null, key: null };
  }

  down(e) {
    const p = this.local(e), ed = this.ed, v = this.view(), b = this.bars();
    this.canvas.setPointerCapture(e.pointerId);
    if (p.y >= v.H - SB && p.x >= LABEL_W) {                         // the bottom scroll bar: drag its thumb, or click beside it to jump there
      if (p.x < b.h.x || p.x > b.h.x + b.h.w) this.scroll = Math.max(0, (p.x - LABEL_W - b.h.w / 2) / Math.max(1, v.timeW - b.h.w) * b.h.range);
      this.drag = { kind: 'hbar', grab: p.x - this.bars().h.x };
      return;
    }
    if (p.x >= v.W - SB && p.y >= RULER) {                           // the right one
      if (b.v.on && (p.y < b.v.y || p.y > b.v.y + b.v.h)) this.vy = (p.y - RULER - b.v.h / 2) / Math.max(1, v.rowsH - b.v.h) * b.v.range;
      this.clampScroll();
      this.drag = { kind: 'vbar', grab: p.y - this.bars().v.y };
      return;
    }
    const edge = this.edgeAt(p);
    if (edge && e.button === 0) { this.drag = { kind: 'height', track: edge.track, y: p.y, h: edge.h }; return; }   // a row's bottom edge: its height
    if (p.y < RULER && p.x >= LABEL_W) { this.drag = { kind: 'scrub' }; ed.seek(this.tOf(p.x)); return; }
    if (p.x < LABEL_W) {                                              // labels: mute (M), delete (x), or select the surface
      const r = this.rows().find(r => p.y >= r.y && p.y < r.y + r.h);
      if (!r) return;
      if (p.x > LABEL_W - 42 && p.x < LABEL_W - 26) { r.track.muted = !r.track.muted; ed.commit(); return; }
      if (r.track.kind === 'param' && p.x >= LABEL_W - 24) { removeTrack(ed.show, r.track); ed.sel.key = null; ed.commit(); ed.refreshPanels(); return; }
      const sid = this.surfaceOf(r.track);
      if (sid) ed.select(sid);
      return;
    }
    const a = this.audioAt(p);
    if (a && e.button === 0) {
      ed.sel.audio = { track: a.track, clip: a.clip }; ed.selectKey(null);
      this.drag = { kind: a.part === 'body' ? 'audioMove' : `trim-${a.part}`, grab: this.tOf(p.x) - a.clip.start };
      return;
    }
    const { key } = this.keyAt(p);
    if (key && e.button === 0) {
      const row = this.keyAt(p).row;
      ed.selectKey({ track: row.track, clip: key.clip, index: key.index });
      this.drag = { kind: 'key', grab: this.tOf(p.x) - key.time, time: key.time, y: p.y, v: key.key.v, curve: this.curve(row), path: parsePath(row.track.path) };
      return;
    }
    if (e.button === 0) { ed.selectKey(null); ed.sel.audio = null; this.drag = { kind: 'scrub' }; ed.seek(this.tOf(p.x)); }
  }
  move(e) {
    const p = this.local(e), ed = this.ed;
    if (!this.drag) { const a = this.audioAt(p); this.canvas.style.cursor = this.edgeAt(p) ? 'row-resize' : a ? (a.part === 'body' ? 'grab' : 'ew-resize') : 'default'; return; }
    const v = this.view();
    if (this.drag.kind === 'hbar') { const w = this.bars().h.w; this.scroll = (p.x - this.drag.grab - LABEL_W) / Math.max(1, v.timeW - w) * this.bars().h.range; this.clampScroll(); return; }
    if (this.drag.kind === 'vbar') { const h = this.bars().v.h; this.vy = (p.y - this.drag.grab - RULER) / Math.max(1, v.rowsH - h) * this.maxVy(); this.clampScroll(); return; }
    if (this.drag.kind === 'height') { this.setHeight(this.drag.track, this.drag.h + p.y - this.drag.y); return; }
    const sa = ed.sel.audio;
    if (sa && (this.drag.kind === 'audioMove' || this.drag.kind.startsWith('trim-'))) {
      let t = this.tOf(p.x) - (this.drag.kind === 'audioMove' ? this.drag.grab : 0);
      if (ed.snap && !e.altKey) t = snapTime(ed.show, t, 8 / this.pps);
      if (this.drag.kind === 'audioMove') moveAudioClip(ed.show, sa.track, sa.clip, t);
      else trimAudioClip(ed.show, sa.track, sa.clip, this.drag.kind.slice(5), t, ed.clock.buffers.get(sa.clip.asset)?.duration ?? Infinity);
      this.drag.moved = true; ed.touched();
      return;
    }
    if (this.drag.kind === 'scrub') { ed.seek(this.tOf(Math.max(LABEL_W, p.x))); return; }
    const s = ed.sel.key;
    if (this.drag.kind === 'key' && s) {
      const d = this.drag;
      let t = e.shiftKey ? d.time : this.tOf(p.x) - d.grab;         // Shift: keep its time (change only the value)
      if (ed.snap && !e.altKey && !e.shiftKey) t = snapTime(ed.show, t, 8 / this.pps);
      t = Math.max(s.clip.start, t);
      s.index = moveKey(ed.show, s.clip, s.index, t);
      if (d.curve && !e.ctrlKey && !e.metaKey) {                    // up and down: its value (Ctrl: keep the value)
        let v = d.v + (d.y - p.y) * d.curve.perPx;
        if (d.curve.d.format === 'choice') v = Math.round(v);
        const c = cleanValue(d.path.key, v);
        if (c !== null) s.clip.keys[s.index].v = c;
      }
      this.drag.moved = true;
      ed.touched();
    }
  }
  up() {
    if (this.drag?.kind === 'height') this.saveHeights();
    if (this.drag?.moved) {
      this.ed.commit();
      if ((this.drag.kind === 'audioMove' || this.drag.kind.startsWith('trim-')) && this.ed.clock.playing) { this.ed.clock.pause(); this.ed.clock.play(); }   // the sound follows the edit
    }
    this.drag = null;
  }

  // Double-click a parameter row: a keyframe there, holding the value the track has at that time.
  dbl(e) {
    const p = this.local(e);
    if (p.x < LABEL_W || p.y < RULER) return;
    const edge = this.edgeAt(p);
    if (edge) { this.heights.delete(this.rowKey(edge.track)); this.saveHeights(); return; }        // double-click a row's edge: its standard height
    const { row, key } = this.keyAt(p);
    if (!row || key) return;
    const path = parsePath(row.track.path);
    let t = this.tOf(p.x);
    if (this.ed.snap) t = snapTime(this.ed.show, t, 8 / this.pps);
    setKey(this.ed.show, path.id, path.key, t, evaluateTrack(row.track, t));
    this.ed.commit();
  }

  // Right-click a keyframe: its curve, or delete it.
  context(e) {
    e.preventDefault();
    const p = this.local(e), { row, key } = this.keyAt(p);
    if (!key) return;
    const ed = this.ed;
    ed.selectKey({ track: row.track, clip: key.clip, index: key.index });
    const k = key.key;
    const menu = el('div', { class: 'menu', style: { left: `${e.clientX}px`, top: `${e.clientY}px` } },
      ...[['smooth', 'Smooth'], ['linear', 'Linear'], ['hold', 'Hold (jump)']].map(([v, l]) =>
        el('button', { text: (k.interp === v ? '● ' : '○ ') + l, on: { click: () => { k.interp = v; ed.commit(); close(); } } })),
      el('button', { text: 'Delete keyframe', on: { click: () => { deleteKey(ed.show, row.track, key.clip, key.index); ed.selectKey(null); ed.commit(); ed.refreshPanels(); close(); } } }));
    const close = () => { menu.remove(); removeEventListener('pointerdown', outside, true); };
    const outside = ev => { if (!menu.contains(ev.target)) close(); };
    document.body.append(menu);
    setTimeout(() => addEventListener('pointerdown', outside, true));
  }

  wheel(e) {
    e.preventDefault();
    const p = this.local(e);
    if (e.shiftKey) { this.scroll = Math.max(0, this.scroll + e.deltaY / this.pps); return; }
    if ((p.y > RULER && p.x < LABEL_W) || p.x >= this.view().W - SB) { this.vy += e.deltaMode === 1 ? e.deltaY * 24 : e.deltaY; this.clampScroll(); return; }   // over the names: up and down
    const at = this.tOf(p.x);
    this.pps = Math.max(5, Math.min(800, this.pps * (e.deltaY < 0 ? 1.15 : 1 / 1.15)));
    this.scroll = Math.max(0, at - (p.x - LABEL_W) / this.pps);
  }
}

function ellipsis(c, s, w) {
  if (c.measureText(s).width <= w) return s;
  while (s.length > 1 && c.measureText(s + '…').width > w) s = s.slice(0, -1);
  return s + '…';
}
