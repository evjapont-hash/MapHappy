// A MapHappy project (.maphappy, JSON) in memory, for the web player and the web editor: readShow() and writeShow()
// use the desktop app's file format, so a show can move freely between the desktop app, the web editor and the player.
// Like the desktop loader it is forgiving: anything damaged falls back to a default and is reported in `warnings`.
import { defaultParams, cleanValue, parsePath, PARAMS } from './params.js';
import { readTimeline, writeTimeline, evaluateTrack, paramTracks, audioClips } from './timeline.js';
import { squareToQuad } from './math.js';

export const SHAPES = ['quad', 'circle', 'triangle', 'line', 'freeform', 'object3d'];
export const BLENDS = ['over', 'add', 'multiply', 'screen'];
const MASK_SHAPES = ['rectangle', 'ellipse', 'polygon'];

const pt = (a, def) => (Array.isArray(a) && a.length === 2 && a.every(Number.isFinite) ? { x: a[0], y: a[1] } : def);
const arr = p => [p.x, p.y];
const clamp01 = v => ({ x: Math.max(0, Math.min(1, v.x)), y: Math.max(0, Math.min(1, v.y)) });
function quad(a, def) {
  if (!Array.isArray(a) || a.length !== 4) return def;
  const q = a.map(p => pt(p, null));
  return q.every(Boolean) && squareToQuad(q) ? q : def;
}
export const unitQuad = () => [{ x: 0, y: 0 }, { x: 1, y: 0 }, { x: 1, y: 1 }, { x: 0, y: 1 }];
export const defaultOut = () => [{ x: 0.36, y: 0.25 }, { x: 0.64, y: 0.25 }, { x: 0.64, y: 0.75 }, { x: 0.36, y: 0.75 }];
export const hexagon = () => [...Array(6)].map((_, i) => { const a = 2 * Math.PI * (i + 0.5) / 6; return { x: 0.5 + 0.5 * Math.cos(a), y: 0.5 + 0.5 * Math.sin(a) }; });

// A new surface with every default, as the desktop app makes it.
export function newSurface(id, shape, name) {
  return { id, name, shape, media: 1, folder: 0, flipH: false, flipV: false, blend: 0, p: defaultParams(),
           in: unitQuad(), out: defaultOut(), lineA: { x: 0.3, y: 0.5 }, lineB: { x: 0.7, y: 0.5 }, poly: hexagon(),
           masks: [], mesh: 'cube', meshPath: '', meshUrl: null };
}

// An empty show.
export function emptyShow() {
  return readShow({ format: 'maphappy', version: 1, output: { width: 1920, height: 1080 },
                    media: [{ id: 1, name: 'Test Pattern', kind: 'generator', type: 'pattern', playing: true, loop: true }] }, () => null);
}

export function readShow(j, resolve) {
  const warnings = [];
  if (!j || j.format !== 'maphappy') throw new Error("This isn't a MapHappy project file");
  const show = {
    outW: Math.max(16, Math.min(16384, j.output?.width | 0 || 1920)),
    outH: Math.max(16, Math.min(16384, j.output?.height | 0 || 1080)),
    mediaList: Array.isArray(j.media) ? j.media.filter(m => m && typeof m === 'object' && m.id > 0) : [],
    surfaces: [], folders: new Map(), presets: new Array(10).fill(null),
    timeline: readTimeline(j.timeline),
    avOffset: Math.max(-1, Math.min(1, +j.transport?.avOffset || 0)),
    loop: !!j.transport?.loop,
    audioAssets: new Map(),
    counters: { surface: 1, asset: 1, folder: 1, shape: [0, 0, 0, 0, 0, 0], ...(j.counters || {}) },
    selectedSurface: j.selectedSurface | 0,
    warnings,
  };
  if (!show.mediaList.some(m => m.id === 1)) show.mediaList.unshift({ id: 1, name: 'Test Pattern', kind: 'generator', type: 'pattern', playing: true, loop: true });
  for (const f of Array.isArray(j.folders) ? j.folders : [])
    if (f && f.id > 0) show.folders.set(f.id, { id: f.id, name: String(f.name ?? `Folder ${f.id}`), parent: f.parent | 0, visible: f.visible !== false, open: f.open !== false });

  for (const o of Array.isArray(j.surfaces) ? j.surfaces : []) {
    if (!o || !(o.id > 0) || show.surfaces.some(s => s.id === o.id)) continue;
    const s = newSurface(o.id, SHAPES.includes(o.shape) ? o.shape : 'quad', String(o.name ?? `Surface-${o.id}`));
    s.media = o.media | 0 || 1; s.folder = o.folder | 0; s.flipH = !!o.flipH; s.flipV = !!o.flipV;
    s.blend = Math.max(0, BLENDS.indexOf(o.blend));
    s.in = quad(o.in, unitQuad()); s.out = quad(o.out, defaultOut());
    s.lineA = pt(o.lineA, s.lineA); s.lineB = pt(o.lineB, s.lineB);
    if (Array.isArray(o.points) && o.points.length >= 3) s.poly = o.points.map(p => clamp01(pt(p, { x: 0.5, y: 0.5 }))).slice(0, 64);
    for (const key of PARAMS.keys()) if (key in o) { const v = cleanValue(key, o[key]); if (v !== null) s.p[key] = v; }
    for (const m of Array.isArray(o.masks) ? o.masks.slice(0, 6) : []) {
      if (!m || typeof m !== 'object') continue;
      const mask = { name: String(m.name ?? `Mask ${s.masks.length + 1}`), shape: Math.max(0, MASK_SHAPES.indexOf(m.shape)), mode: m.mode === 'showOnly' ? 1 : 0,
                     enabled: m.enabled !== false, feather: Math.max(0, Math.min(1, +m.feather || 0)),
                     min: clamp01(pt(m.min, { x: 0.3, y: 0.3 })), max: clamp01(pt(m.max, { x: 0.7, y: 0.7 })), points: [] };
      if (mask.shape === 2) mask.points = (Array.isArray(m.points) ? m.points : []).map(p => clamp01(pt(p, { x: 0.5, y: 0.5 }))).slice(0, 16);
      s.masks.push(mask);
    }
    if (s.shape === 'object3d') {
      s.mesh = ['cube', 'sphere', 'cylinder', 'plane', 'file'].includes(o.mesh) ? o.mesh : 'cube';
      if (s.mesh === 'file') {
        s.meshPath = String(o.meshPathRel || o.meshPath || '');
        s.meshUrl = resolve(o.meshPath, o.meshPathRel);
        if (!s.meshUrl) { warnings.push(`Can't find the model used by "${s.name}"; it shows a cube`); s.mesh = 'cube'; }
      }
    }
    show.surfaces.push(s);
  }

  for (const a of Array.isArray(j.audioAssets) ? j.audioAssets : []) {
    if (!a || !(a.id > 0)) continue;
    const url = resolve(a.path, a.pathRel);
    show.audioAssets.set(a.id, { id: a.id, name: String(a.name ?? 'song'), path: String(a.pathRel || a.path || ''), bpm: Math.max(0, +a.bpm || 0), beatOffset: Math.max(0, +a.beatOffset || 0), url });
    if (!url) warnings.push(`Can't find the song "${a.name}"`);
  }

  for (const pr of Array.isArray(j.presets) ? j.presets : []) {
    if (!pr || !(pr.slot >= 0 && pr.slot < 10) || show.presets[pr.slot]) continue;
    show.presets[pr.slot] = {
      name: String(pr.name ?? ''), fade: Math.max(0, Math.min(30, +pr.fade || 0)),
      surfaces: (Array.isArray(pr.surfaces) ? pr.surfaces : []).map(ps => ({ id: ps.id, media: ps.media, values: ps.values || {} })),
      folders: (Array.isArray(pr.folders) ? pr.folders : []).map(f => ({ id: f.id, visible: f.visible !== false })),
    };
  }
  return show;
}

// The show as a project file the desktop app can open (and the player can play).
export function writeShow(show) {
  const surfaces = show.surfaces.map(s => {
    const o = { id: s.id, name: s.name, shape: s.shape, media: s.media, folder: s.folder, flipH: s.flipH, flipV: s.flipV, blend: BLENDS[s.blend] || 'over' };
    for (const d of PARAMS.values()) o[d.key] = d.isBool ? s.p[d.key] >= 0.5 : s.p[d.key];
    o.in = s.in.map(arr); o.out = s.out.map(arr);
    if (s.shape === 'line') { o.lineA = arr(s.lineA); o.lineB = arr(s.lineB); }
    if (s.shape === 'freeform') o.points = s.poly.map(arr);
    if (s.masks.length) o.masks = s.masks.map(m => {
      const mj = { name: m.name, shape: MASK_SHAPES[m.shape], mode: m.mode ? 'showOnly' : 'hide', enabled: m.enabled, feather: m.feather };
      if (m.shape === 2) mj.points = m.points.map(arr); else { mj.min = arr(m.min); mj.max = arr(m.max); }
      return mj;
    });
    if (s.shape === 'object3d') { o.mesh = s.mesh; if (s.mesh === 'file') { o.meshPath = s.meshPath; o.meshPathRel = s.meshPath; } }
    return o;
  });
  const presets = [];
  show.presets.forEach((pr, slot) => { if (pr) presets.push({ slot, key: slot === 9 ? 0 : slot + 1, name: pr.name, fade: pr.fade, surfaces: pr.surfaces, folders: pr.folders }); });
  const shapeCounts = [...(show.counters.shape || [])];
  while (shapeCounts.length < SHAPES.length) shapeCounts.push(0);
  return {
    format: 'maphappy', version: 1,
    output: { width: show.outW, height: show.outH },
    media: show.mediaList,
    surfaces,
    selectedSurface: show.selectedSurface | 0,
    folders: [...show.folders.values()].map(f => ({ id: f.id, name: f.name, parent: f.parent, visible: f.visible, open: f.open })),
    presets,
    audioAssets: [...show.audioAssets.values()].map(a => ({ id: a.id, name: a.name, bpm: a.bpm, beatOffset: a.beatOffset, path: a.path, pathRel: a.path })),
    timeline: writeTimeline(show.timeline),
    transport: { avOffset: show.avOffset, loop: show.loop },
    counters: {
      surface: Math.max(show.counters.surface | 0, ...show.surfaces.map(s => s.id + 1), 1),
      asset: Math.max(show.counters.asset | 0, ...[...show.audioAssets.keys()].map(id => id + 1), 1),
      folder: Math.max(show.counters.folder | 0, ...[...show.folders.keys()].map(id => id + 1), 1),
      shape: shapeCounts,
    },
  };
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
  for (const s of show.surfaces) s.clock = time;                     // spins follow the show clock
  for (const t of paramTracks(show.timeline)) {
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

export { audioClips };

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

// A preset capturing the current look (as the desktop app's capturePreset).
export function capturePreset(show, name, fade) {
  return {
    name, fade,
    surfaces: show.surfaces.map(s => {
      const values = {};
      for (const d of PARAMS.values()) if (d.applies(s)) values[d.key] = s.p[d.key];
      return { id: s.id, media: s.media, values };
    }),
    folders: [...show.folders.values()].map(f => ({ id: f.id, visible: f.visible })),
  };
}
