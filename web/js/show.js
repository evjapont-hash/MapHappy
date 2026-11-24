// Reads a MapHappy project file (.maphappy, JSON) into what the player needs. Like the desktop loader it is
// forgiving: anything damaged falls back to a default and is reported in `warnings`.
import { defaultParams, cleanValue, parsePath, PARAMS } from './params.js';
import { readTimeline, evaluateTrack } from './timeline.js';
import { squareToQuad } from './math.js';

const SHAPES = ['quad', 'circle', 'triangle', 'line', 'freeform', 'object3d'];
const BLENDS = { over: 0, add: 1, multiply: 2, screen: 3 };

const pt = (a, def) => (Array.isArray(a) && a.length === 2 && a.every(Number.isFinite) ? { x: a[0], y: a[1] } : def);
const clamp01 = v => ({ x: Math.max(0, Math.min(1, v.x)), y: Math.max(0, Math.min(1, v.y)) });
function quad(a, def) {
  if (!Array.isArray(a) || a.length !== 4) return def;
  const q = a.map(p => pt(p, null));
  return q.every(Boolean) && squareToQuad(q) ? q : def;
}
const unitQuad = () => [{ x: 0, y: 0 }, { x: 1, y: 0 }, { x: 1, y: 1 }, { x: 0, y: 1 }];
const hexagon = () => [...Array(6)].map((_, i) => { const a = 2 * Math.PI * (i + 0.5) / 6; return { x: 0.5 + 0.5 * Math.cos(a), y: 0.5 + 0.5 * Math.sin(a) }; });

export function readShow(j, resolve) {
  const warnings = [];
  if (!j || j.format !== 'maphappy') throw new Error("This isn't a MapHappy project file");
  const show = {
    outW: Math.max(16, Math.min(16384, j.output?.width | 0 || 1920)),
    outH: Math.max(16, Math.min(16384, j.output?.height | 0 || 1080)),
    mediaList: j.media || [],
    surfaces: [], folders: new Map(), presets: new Array(10).fill(null),
    timeline: readTimeline(j.timeline),
    avOffset: Math.max(-1, Math.min(1, +j.transport?.avOffset || 0)),
    loop: !!j.transport?.loop,
    audioAssets: new Map(),
    warnings,
  };
  for (const f of Array.isArray(j.folders) ? j.folders : []) if (f && f.id > 0) show.folders.set(f.id, { id: f.id, parent: f.parent | 0, visible: f.visible !== false });

  for (const o of Array.isArray(j.surfaces) ? j.surfaces : []) {
    if (!o || !(o.id > 0)) continue;
    const s = {
      id: o.id, name: String(o.name ?? `Surface-${o.id}`), shape: SHAPES.includes(o.shape) ? o.shape : 'quad',
      media: o.media | 0 || 1, folder: o.folder | 0, flipH: !!o.flipH, flipV: !!o.flipV, blend: BLENDS[o.blend] ?? 0,
      p: defaultParams(),
      in: quad(o.in, unitQuad()), out: quad(o.out, [{ x: 0.36, y: 0.25 }, { x: 0.64, y: 0.25 }, { x: 0.64, y: 0.75 }, { x: 0.36, y: 0.75 }]),
      lineA: pt(o.lineA, { x: 0.3, y: 0.5 }), lineB: pt(o.lineB, { x: 0.7, y: 0.5 }),
      poly: Array.isArray(o.points) && o.points.length >= 3 ? o.points.map(p => clamp01(pt(p, { x: 0.5, y: 0.5 }))).slice(0, 64) : hexagon(),
      masks: [], mesh: o.mesh || 'cube', meshUrl: null,
    };
    for (const key of PARAMS.keys()) if (key in o) { const v = cleanValue(key, o[key]); if (v !== null) s.p[key] = v; }
    for (const m of Array.isArray(o.masks) ? o.masks.slice(0, 6) : []) {
      if (!m || typeof m !== 'object') continue;
      const shape = ['rectangle', 'ellipse', 'polygon'].indexOf(m.shape);
      const mask = { shape: Math.max(0, shape), mode: m.mode === 'showOnly' ? 1 : 0, enabled: m.enabled !== false, feather: Math.max(0, Math.min(1, +m.feather || 0)),
                     min: clamp01(pt(m.min, { x: 0.3, y: 0.3 })), max: clamp01(pt(m.max, { x: 0.7, y: 0.7 })), points: [] };
      if (mask.shape === 2) mask.points = (Array.isArray(m.points) ? m.points : []).map(p => clamp01(pt(p, { x: 0.5, y: 0.5 }))).slice(0, 16);
      s.masks.push(mask);
    }
    if (s.shape === 'object3d' && s.mesh === 'file') {
      s.meshUrl = resolve(o.meshPath, o.meshPathRel);
      if (!s.meshUrl) { warnings.push(`Can't find the model used by "${s.name}"; it shows a cube`); s.mesh = 'cube'; }
    }
    show.surfaces.push(s);
  }

  for (const a of Array.isArray(j.audioAssets) ? j.audioAssets : []) {
    if (!a || !(a.id > 0)) continue;
    const url = resolve(a.path, a.pathRel);
    if (url) show.audioAssets.set(a.id, { id: a.id, name: a.name, url });
    else warnings.push(`Can't find the song "${a.name}"`);
  }
  for (const c of show.timeline.audioClips) show.timeline.length = Math.max(show.timeline.length, c.start + c.duration);

  for (const pr of Array.isArray(j.presets) ? j.presets : []) {
    if (!pr || !(pr.slot >= 0 && pr.slot < 10)) continue;
    show.presets[pr.slot] = {
      name: String(pr.name ?? ''), fade: Math.max(0, Math.min(30, +pr.fade || 0)),
      surfaces: (Array.isArray(pr.surfaces) ? pr.surfaces : []).map(ps => ({ id: ps.id, media: ps.media, values: ps.values || {} })),
      folders: (Array.isArray(pr.folders) ? pr.folders : []).map(f => ({ id: f.id, visible: f.visible !== false })),
    };
  }
  return show;
}

// Is a surface drawn? Its own switch and every folder above it.
export function isShown(show, s) {
  if (s.p.visible < 0.5) return false;
  let id = s.folder;
  for (let guard = 0; id && guard <= show.folders.size; guard++) {
    const f = show.folders.get(id);
    if (!f) break;
    if (!f.visible) return false;
    id = f.parent;
  }
  return true;
}

// Writes every unmuted track's value at `time` into the surfaces (the timeline drives what it animates).
export function applyTimeline(show, time) {
  for (const t of show.timeline.paramTracks) {
    if (t.muted) continue;
    const target = parsePath(t.path);
    if (!target) continue;
    const s = show.surfaces.find(x => x.id === target.id);
    const v = evaluateTrack(t, time);
    if (!s || v === null) continue;
    const c = cleanValue(target.key, v);
    if (c !== null) s.p[target.key] = c;
  }
}

// Plays live presets: a cut, or a smoothstep cross-fade from wherever things are (as in the desktop app's Presets.h).
export class PresetPlayer {
  constructor() { this.active = -1; this.items = []; this.switches = []; this.t = 0; this.dur = 0; this.finished = true; }
  start(show, preset, slot) {
    this.items = []; this.switches = []; this.active = slot; this.t = 0; this.dur = Math.max(0, preset.fade);
    for (const ps of preset.surfaces) {
      const s = show.surfaces.find(x => x.id === ps.id);
      if (!s) continue;
      if (show.media?.has(ps.media)) s.media = ps.media;
      for (const [key, raw] of Object.entries(ps.values)) {
        const d = PARAMS.get(key);
        if (!d || !d.applies(s)) continue;
        const to = cleanValue(key, raw);
        if (to === null) continue;
        const from = s.p[key];
        if (d.isBool) { if (to >= 0.5) s.p[key] = 1; else if (from >= 0.5) this.switches.push({ s, key }); }
        else if (from !== to) this.items.push({ s, key, from, to });
      }
    }
    for (const fv of preset.folders) {
      const f = show.folders.get(fv.id);
      if (!f) continue;
      if (fv.visible) f.visible = true; else if (f.visible) this.switches.push({ folder: f });
    }
    this.finished = false;
    this.update(0);
  }
  update(dt) {
    if (this.active < 0 || this.finished) return;
    this.t += dt;
    const k = this.dur <= 0 ? 1 : Math.min(1, this.t / this.dur), e = k * k * (3 - 2 * k);
    for (const it of this.items) it.s.p[it.key] = it.from + (it.to - it.from) * e;
    if (k >= 1) {
      for (const sw of this.switches) { if (sw.folder) sw.folder.visible = false; else sw.s.p[sw.key] = 0; }
      this.items = []; this.switches = []; this.finished = true;
    }
  }
  get fading() { return this.active >= 0 && !this.finished; }
  get progress() { return this.dur <= 0 ? 1 : Math.min(1, this.t / this.dur); }
}
