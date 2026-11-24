// The web editor's timeline: a ruler with seconds and beats, the song's waveform, and one row per animated
// parameter with its keyframes. Click or drag the ruler to move the playhead; drag a keyframe to move it (it snaps
// to beats and markers); double-click a row to add a keyframe; right-click one for its curve or to delete it.
// Mouse wheel zooms, Shift + wheel scrolls.
import { keysOf, moveKey, deleteKey, setKey, snapTime, beatGrid, trackLabel, removeTrack } from '../editor-ops.js';
import { evaluateTrack } from '../timeline.js';
import { parsePath } from '../params.js';
import { el } from './ui.js';

const RULER = 24, AUDIO_H = 44, ROW_H = 22, LABEL_W = 190;
const C = { bg: '#141518', row: '#1a1b1f', rowAlt: '#17181c', line: '#2a2b31', text: '#c9c9cf', dim: '#7c7c86', accent: '#26c6d0',
            key: '#f2c14e', keySel: '#ffffff', clip: 'rgba(38,198,208,0.10)', wave: 'rgba(132,196,205,0.85)', beat: 'rgba(255,255,255,0.06)', bar: 'rgba(255,255,255,0.13)', marker: '#e46cd3' };

export class TimelineView {
  constructor(ed, canvas) {
    this.ed = ed; this.canvas = canvas; this.ctx = canvas.getContext('2d');
    this.pps = 60;                 // pixels per second
    this.scroll = 0;               // seconds at the left edge
    this.vscroll = 0;              // rows scrolled
    this.drag = null;
    this.peaks = new Map();        // asset id -> { min, max } per 1/200 s
    canvas.addEventListener('pointerdown', e => this.down(e));
    canvas.addEventListener('pointermove', e => this.move(e));
    canvas.addEventListener('pointerup', () => this.up());
    canvas.addEventListener('dblclick', e => this.dbl(e));
    canvas.addEventListener('contextmenu', e => this.context(e));
    canvas.addEventListener('wheel', e => this.wheel(e), { passive: false });
  }

  rows() {
    const tl = this.ed.show.timeline, out = [];
    let y = RULER;
    for (const t of tl.tracks.filter(t => t.kind === 'audio')) { out.push({ track: t, y, h: AUDIO_H }); y += AUDIO_H; }
    const params = tl.tracks.filter(t => t.kind === 'param');
    params.slice(this.vscroll).forEach(t => { out.push({ track: t, y, h: ROW_H }); y += ROW_H; });
    return out;
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

    const rows = this.rows();
    rows.forEach((r, i) => { c.fillStyle = i % 2 ? C.rowAlt : C.row; c.fillRect(0, r.y, W, r.h); });

    // beat grid and seconds
    const grid = beatGrid(show);
    c.save(); c.beginPath(); c.rect(LABEL_W, 0, W - LABEL_W, H); c.clip();
    if (grid && grid.beat * this.pps > 6) {
      const first = Math.ceil((t0 - grid.start) / grid.beat);
      for (let b = first; grid.start + b * grid.beat < t1; b++) {
        const x = this.xOf(grid.start + b * grid.beat);
        c.fillStyle = b % 4 === 0 ? C.bar : C.beat; c.fillRect(Math.round(x), RULER, 1, H - RULER);
      }
    }
    // clips and keyframes
    for (const r of rows) {
      const t = r.track;
      if (t.kind === 'audio') {
        for (const clip of t.audioClips) {
          const x0 = this.xOf(clip.start), x1 = this.xOf(clip.start + clip.duration);
          c.fillStyle = 'rgba(132,196,205,0.12)'; c.fillRect(x0, r.y + 2, x1 - x0, r.h - 4);
          const p = this.peaksFor(clip.asset), mid = r.y + r.h / 2, amp = r.h / 2 - 4;
          if (p) {
            c.fillStyle = C.wave;
            for (let x = Math.max(x0, LABEL_W); x < Math.min(x1, W); x++) {
              const ta = (this.tOf(x) - clip.start + clip.offset) * p.rate, tb = (this.tOf(x + 1) - clip.start + clip.offset) * p.rate;
              let lo = 1, hi = -1;
              for (let k = Math.floor(ta); k <= Math.floor(tb) && k < p.min.length; k++) if (k >= 0) { lo = Math.min(lo, p.min[k] * clip.gain); hi = Math.max(hi, p.max[k] * clip.gain); }
              if (hi >= lo) c.fillRect(x, mid - hi * amp, 1, Math.max(1, (hi - lo) * amp));
            }
          } else { c.fillStyle = C.dim; c.font = '11px system-ui'; c.fillText('(press Play to load the sound)', x0 + 6, mid + 4); }
        }
        continue;
      }
      for (const clip of t.clips) { const x0 = this.xOf(clip.start), x1 = this.xOf(clip.start + clip.duration); c.fillStyle = C.clip; c.fillRect(x0, r.y + 3, Math.max(2, x1 - x0), r.h - 6); }
      for (const k of keysOf(t)) {
        const x = this.xOf(k.time), y = r.y + r.h / 2, sel = this.isSelected(t, k);
        c.beginPath(); c.moveTo(x, y - 6); c.lineTo(x + 6, y); c.lineTo(x, y + 6); c.lineTo(x - 6, y); c.closePath();
        c.fillStyle = t.muted ? C.dim : sel ? C.keySel : C.key; c.fill();
        if (k.key.interp === 'hold') { c.fillStyle = C.bg; c.fillRect(x - 1.5, y - 1.5, 3, 3); }
      }
    }
    // markers and playhead
    for (const m of tl.markers) { const x = this.xOf(m.time); c.fillStyle = C.marker; c.fillRect(Math.round(x), RULER - 8, 1, H); }
    c.restore();

    // ruler
    c.fillStyle = '#1e1f24'; c.fillRect(0, 0, W, RULER); c.fillStyle = C.line; c.fillRect(0, RULER - 1, W, 1);
    const stepS = [0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60].find(s => s * this.pps >= 70) || 60;
    c.font = '11px ui-monospace, Consolas, monospace'; c.fillStyle = C.dim;
    for (let s = Math.ceil(t0 / stepS) * stepS; s < t1; s += stepS) {
      const x = this.xOf(s);
      if (x < LABEL_W) continue;
      c.fillRect(Math.round(x), RULER - 7, 1, 7);
      c.fillText(`${Math.floor(s / 60)}:${(s % 60).toFixed(stepS < 1 ? 2 : 0).padStart(stepS < 1 ? 5 : 2, '0')}`, x + 3, 14);
    }
    const px = this.xOf(now);
    if (px >= LABEL_W) { c.fillStyle = C.accent; c.fillRect(Math.round(px), 0, 2, H); c.beginPath(); c.moveTo(px - 5, 0); c.lineTo(px + 7, 0); c.lineTo(px + 1, 8); c.fill(); }

    // labels
    c.fillStyle = '#1c1d21'; c.fillRect(0, 0, LABEL_W, H); c.fillStyle = C.line; c.fillRect(LABEL_W - 1, 0, 1, H);
    c.font = '12px system-ui, Segoe UI, sans-serif';
    c.fillStyle = C.dim; c.fillText(grid ? `${grid.bpm.toFixed(1)} BPM` : 'Tracks', 8, 16);
    for (const r of rows) {
      const t = r.track, selSurface = this.surfaceOf(t) === ed.sel.surface;
      c.fillStyle = t.muted ? C.dim : selSurface ? C.accent : C.text;
      c.fillText(ellipsis(c, trackLabel(show, t), LABEL_W - 46), 8, r.y + r.h / 2 + 4);
      c.fillStyle = t.muted ? '#e0a040' : C.dim; c.fillText('M', LABEL_W - 36, r.y + r.h / 2 + 4);
      if (t.kind === 'param') { c.fillStyle = C.dim; c.fillText('×', LABEL_W - 18, r.y + r.h / 2 + 4); }
    }
    if (!rows.some(r => r.track.kind === 'param'))
      { c.fillStyle = C.dim; c.fillText('No animation yet: press ◆ next to any setting to set a keyframe at the playhead.', LABEL_W + 12, (rows.length ? rows[rows.length - 1].y + rows[rows.length - 1].h : RULER) + 18); }
  }

  surfaceOf(t) { const p = parsePath(t.path); return p ? p.id : 0; }
  isSelected(t, k) { const s = this.ed.sel.key; return s && s.track === t && s.clip === k.clip && s.index === k.index; }

  keyAt(pt) {
    for (const r of this.rows()) {
      if (r.track.kind !== 'param' || pt.y < r.y || pt.y >= r.y + r.h) continue;
      let best = null, bd = 8;
      for (const k of keysOf(r.track)) { const d = Math.abs(this.xOf(k.time) - pt.x); if (d < bd) { bd = d; best = k; } }
      return { row: r, key: best };
    }
    return { row: null, key: null };
  }

  down(e) {
    const p = this.local(e), ed = this.ed;
    this.canvas.setPointerCapture(e.pointerId);
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
    const { key } = this.keyAt(p);
    if (key && e.button === 0) {
      ed.selectKey({ track: this.keyAt(p).row.track, clip: key.clip, index: key.index });
      this.drag = { kind: 'key', grab: this.tOf(p.x) - key.time };
      return;
    }
    if (e.button === 0) { ed.selectKey(null); this.drag = { kind: 'scrub' }; ed.seek(this.tOf(p.x)); }
  }
  move(e) {
    if (!this.drag) return;
    const p = this.local(e), ed = this.ed;
    if (this.drag.kind === 'scrub') { ed.seek(this.tOf(Math.max(LABEL_W, p.x))); return; }
    const s = ed.sel.key;
    if (this.drag.kind === 'key' && s) {
      let t = this.tOf(p.x) - this.drag.grab;
      if (ed.snap && !e.altKey) t = snapTime(ed.show, t, 8 / this.pps);
      t = Math.max(s.clip.start, t);
      s.index = moveKey(ed.show, s.clip, s.index, t);
      this.drag.moved = true;
      ed.touched();
    }
  }
  up() { if (this.drag?.moved) this.ed.commit(); this.drag = null; }

  // Double-click a parameter row: a keyframe there, holding the value the track has at that time.
  dbl(e) {
    const p = this.local(e);
    if (p.x < LABEL_W || p.y < RULER) return;
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
    if (p.y > RULER && p.x < LABEL_W) { const n = this.ed.show.timeline.tracks.filter(t => t.kind === 'param').length; this.vscroll = Math.max(0, Math.min(n - 1, this.vscroll + Math.sign(e.deltaY))); return; }
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
