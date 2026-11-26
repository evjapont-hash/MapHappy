// Editing a show: surfaces, shapes, keyframes, songs, markers. Plain functions on the show (no DOM, no WebGL), so
// they are unit-tested with Node, and behave like the desktop app's Project / Timeline code.
import { newSurface, SHAPES } from './show.js';
import { PARAMS, cleanValue, parsePath } from './params.js';
import { evaluateTrack, paramTracks } from './timeline.js';
import { squareToQuad } from './math.js';

const NAMES = { quad: 'Quad', circle: 'Circle', triangle: 'Triangle', line: 'Line', freeform: 'Freeform', object3d: '3D Object' };
export const shapeName = shape => NAMES[shape] || 'Quad';

// ---------------------------------------------------------------- surfaces
// Adds a surface at the front (like the desktop app: named "Quad-3", staggered so it doesn't land on the last one).
export function addSurface(show, shape, mediaId = 1, folder = 0) {
  const id = Math.max(show.counters.surface | 0, 1, ...show.surfaces.map(s => s.id + 1));
  show.counters.surface = id + 1;
  const si = SHAPES.indexOf(shape);
  show.counters.shape = show.counters.shape || [];
  show.counters.shape[si] = (show.counters.shape[si] | 0) + 1;
  const s = newSurface(id, shape, `${shapeName(shape)}-${show.counters.shape[si]}`);
  s.media = mediaId; s.folder = folder;
  const shift = 0.04 * (show.surfaces.length % 5) - 0.08;
  for (const q of s.out) { q.x += shift; q.y += shift; }
  s.lineA.x += shift; s.lineA.y += shift; s.lineB.x += shift; s.lineB.y += shift;
  resetInput(show, s);
  show.surfaces.unshift(s);
  return s;
}

export function removeSurface(show, id) {
  show.surfaces = show.surfaces.filter(s => s.id !== id);
  show.timeline.tracks = show.timeline.tracks.filter(t => !(t.path || '').startsWith(`surface/${id}/`));
}

// Puts a copy of a surface (a plain copy of its data) in front, `offset` away from where the original was.
export function pasteSurface(show, data, offset = 0.03) {
  const c = addSurface(show, data.shape, show.mediaList.some(m => m.id === data.media) ? data.media : 1, show.folders.has(data.folder) ? data.folder : 0);
  const { id, media, folder } = c, base = `${data.name.replace(/ copy( \d+)?$/, '')} copy`;
  let name = base;
  for (let n = 2; show.surfaces.some(s => s !== c && s.name === name); n++) name = `${base} ${n}`;
  Object.assign(c, JSON.parse(JSON.stringify({ ...data, meshUrl: null })), { id, media, folder, name, meshUrl: data.meshUrl });
  for (const q of c.out) { q.x += offset; q.y += offset; }
  if (c.shape === 'line') for (const q of [c.lineA, c.lineB]) { q.x += offset; q.y += offset; }
  return c;
}

// Shift-dragging a point: how much to grow the shape about `anchor`, so the point at `from` follows the mouse (`to`) as
// far as it can without changing the shape's proportions. aspect = the picture's width / height.
export function scaleFactor(anchor, from, to, aspect) {
  const vx = (from.x - anchor.x) * aspect, vy = from.y - anchor.y, len2 = vx * vx + vy * vy;
  if (len2 < 1e-10) return 1;
  const k = ((to.x - anchor.x) * aspect * vx + (to.y - anchor.y) * vy) / len2;
  return Math.max(k, Math.min(1, 0.01 / Math.sqrt(len2)));       // it never collapses or turns inside out
}
export function scaleQuad(q, anchor, k) { for (const v of q) { v.x = anchor.x + (v.x - anchor.x) * k; v.y = anchor.y + (v.y - anchor.y) * k; } }

// delta -1 = towards the front (drawn on top), +1 = towards the back
export function moveSurface(show, id, delta) {
  const i = show.surfaces.findIndex(s => s.id === id), j = i + delta;
  if (i < 0 || j < 0 || j >= show.surfaces.length) return;
  [show.surfaces[i], show.surfaces[j]] = [show.surfaces[j], show.surfaces[i]];
}

// The picture region a new surface shows: all of it for a quad or 3D object, a strip for a line, a centred
// square for the other shapes (so a circle samples a round area).
export function resetInput(show, s, mediaAspect = mediaAspectOf(show, s.media)) {
  let hx = 0.5, hy = 0.5;
  if (s.shape === 'line') hy = 0.05;
  else if (s.shape !== 'quad' && s.shape !== 'object3d') { if (mediaAspect >= 1) hx = 0.5 / mediaAspect; else hy = 0.5 * mediaAspect; }
  s.in = [{ x: 0.5 - hx, y: 0.5 - hy }, { x: 0.5 + hx, y: 0.5 - hy }, { x: 0.5 + hx, y: 0.5 + hy }, { x: 0.5 - hx, y: 0.5 + hy }];
}
function mediaAspectOf(show, id) {
  const m = show.media?.get(id);
  return m && m.w > 0 && m.h > 0 ? m.w / m.h : 16 / 9;
}

// Changes a surface's shape, with sensible starting points from its current quad (as the desktop app's setShape).
export function setShape(show, s, shape) {
  if (s.shape === shape || !SHAPES.includes(shape)) return;
  if (shape === 'line') {
    s.lineA = { x: (s.out[0].x + s.out[3].x) / 2, y: (s.out[0].y + s.out[3].y) / 2 };
    s.lineB = { x: (s.out[1].x + s.out[2].x) / 2, y: (s.out[1].y + s.out[2].y) / 2 };
  }
  if (s.shape === 'line') {
    const c = { x: (s.lineA.x + s.lineB.x) / 2, y: (s.lineA.y + s.lineB.y) / 2 }, h = 0.25, w = 0.25 * show.outH / show.outW;
    s.out = [{ x: c.x - w, y: c.y - h }, { x: c.x + w, y: c.y - h }, { x: c.x + w, y: c.y + h }, { x: c.x - w, y: c.y + h }];
  }
  s.shape = shape;
  resetInput(show, s);
}

// A quad that is twisted or dented has no valid projection; the editor refuses such drags.
export function isConvex(q) {
  let sign = 0;
  for (let i = 0; i < 4; i++) {
    const a = q[i], b = q[(i + 1) % 4], c = q[(i + 2) % 4];
    const cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
    if (Math.abs(cross) < 1e-9) return false;
    const sg = cross > 0 ? 1 : -1;
    if (!sign) sign = sg; else if (sg !== sign) return false;
  }
  return !!squareToQuad(q);
}

// ---------------------------------------------------------------- keyframes
export const trackPath = (surfaceId, key) => `surface/${surfaceId}/${key}`;
export const findTrack = (show, path) => show.timeline.tracks.find(t => t.kind === 'param' && t.path === path) || null;
const nextId = (tl, kind) => tl.counters[kind]++;

// The animated value of a parameter at a time, or null if nothing animates it.
export function animatedValue(show, surfaceId, key, time) {
  const t = findTrack(show, trackPath(surfaceId, key));
  return t && !t.muted ? evaluateTrack(t, time) : null;
}

// Sets a keyframe for a parameter at a time (making its track if needed). A key within half a frame of an existing
// one replaces it. Returns the track.
export function setKey(show, surfaceId, key, time, value, interp) {
  const tl = show.timeline, path = trackPath(surfaceId, key);
  const v = cleanValue(key, value);
  if (v === null) return null;
  time = Math.max(0, time);
  let t = findTrack(show, path);
  if (!t) {
    t = { id: nextId(tl, 'track'), kind: 'param', name: '', path, muted: false, clips: [] };
    tl.tracks.push(t);
  }
  const defInterp = PARAMS.get(key)?.isBool || key === 'lightModel' ? 'hold' : 'smooth';
  // the clip that holds this time: the last one starting at or before it; else a new one from here
  let clip = null;
  for (const c of t.clips) if (c.start <= time + 1e-9) clip = c;
  if (!clip) {
    // (a clip as long as its keys: after its last key the value holds; a clip LONGER than its keys would loop them)
    clip = { id: nextId(tl, 'clip'), name: `Clip-${tl.counters.clip}`, start: t.clips.length ? time : 0, duration: 1 / 60, ghost: false, keys: [] };
    t.clips.push(clip); t.clips.sort((a, b) => a.start - b.start);
  }
  const local = time - clip.start;
  const existing = clip.keys.find(k => Math.abs(k.t - local) < 0.5 / 60);
  if (existing) { existing.v = v; if (interp) existing.interp = interp; }
  else { clip.keys.push({ t: local, v, interp: interp || defInterp }); clip.keys.sort((a, b) => a.t - b.t); }
  clip.duration = Math.max(clip.duration, local, 1 / 60);
  tl.length = Math.max(tl.length, clip.start + clip.duration);
  return t;
}

// Every keyframe of a track, with its time on the timeline (for drawing and clicking).
export function keysOf(track) {
  const out = [];
  for (const c of track.clips) c.keys.forEach((k, i) => out.push({ clip: c, index: i, key: k, time: c.start + k.t }));
  return out;
}

// Whether a clip ends at its last key (rather than being stretched to loop its keys).
const endsAtLastKey = clip => !clip.keys.length || Math.abs(clip.duration - Math.max(1 / 60, clip.keys[clip.keys.length - 1].t)) < 1e-6;

// ---------------------------------------------------------------- clips (as in the desktop app's Timeline.h)
// A clip holds keyframes; clips on a track never overlap. A clip longer than its keyframes loops them. A ghost copy
// shares its keyframes with the original (editing one edits both); a duplicate gets its own.
function clipNeighbours(track, clip) {
  const list = [...track.clips].sort((a, b) => a.start - b.start), i = list.indexOf(clip);
  return { prevEnd: i > 0 ? list[i - 1].start + list[i - 1].duration : 0, nextStart: i + 1 < list.length ? list[i + 1].start : Infinity };
}
// The first start at or after `t` where a clip of `duration` fits.
function freeStart(track, t, duration, except = null) {
  let s = Math.max(0, t), moved = true;
  while (moved) {
    moved = false;
    for (const c of track.clips) if (c !== except && s < c.start + c.duration - 1e-9 && s + duration > c.start + 1e-9) { s = c.start + c.duration; moved = true; }
  }
  return s;
}
const sortClips = track => track.clips.sort((a, b) => a.start - b.start);
const grow = (show, end) => { show.timeline.length = Math.max(show.timeline.length, end); };

// A new clip at `start` (up to 2 s long, less if the next clip is closer), holding `value`. Null if there is no room.
export function addClip(show, track, start, value) {
  start = Math.max(0, start);
  if (track.clips.some(c => start >= c.start - 1e-9 && start < c.start + c.duration - 1e-9)) return null;
  const next = track.clips.filter(c => c.start > start).reduce((m, c) => Math.min(m, c.start), Infinity), duration = Math.min(2, next - start);
  if (duration < 1 / 60) return null;
  const tl = show.timeline, clip = { id: nextId(tl, 'clip'), name: `Clip-${tl.counters.clip}`, start, duration, ghost: false,
    keys: [{ t: 0, v: value, interp: 'smooth' }, { t: duration, v: value, interp: 'smooth' }] };
  track.clips.push(clip); sortClips(track); grow(show, start + duration);
  return clip;
}
// A copy in the next free spot after the clip (or at `at`). ghost = it shares the keyframes.
export function duplicateClip(show, track, clip, ghost, at = clip.start + clip.duration) {
  const tl = show.timeline, copy = { id: nextId(tl, 'clip'), name: `Clip-${tl.counters.clip}`, start: freeStart(track, at, clip.duration), duration: clip.duration,
    ghost, keys: ghost ? clip.keys : clip.keys.map(k => ({ ...k })) };
  track.clips.push(copy); sortClips(track); grow(show, copy.start + copy.duration);
  return copy;
}
export function removeClip(show, track, clip) {
  track.clips = track.clips.filter(c => c !== clip);
  if (!track.clips.length) show.timeline.tracks = show.timeline.tracks.filter(t => t !== track);
}
// Slides a clip (it stops at its neighbours).
export function moveClip(show, track, clip, start) {
  const { prevEnd, nextStart } = clipNeighbours(track, clip);
  clip.start = Math.max(prevEnd, Math.min(nextStart - clip.duration, start));
  grow(show, clip.start + clip.duration);
}
// Stretches a clip from its right edge: longer than its keyframes, they loop.
export function resizeClip(show, track, clip, duration) {
  const { nextStart } = clipNeighbours(track, clip);
  clip.duration = Math.max(1 / 60, Math.min(nextStart - clip.start, duration));
  grow(show, clip.start + clip.duration);
}

// Moves a clip's left edge to `start`, keeping its keyframes where they are on the timeline (so it never cuts one off).
// A ghost copy trimmed this way gets keyframes of its own (its copies would otherwise move too).
export function trimClipStart(show, track, clip, start) {
  const { prevEnd } = clipNeighbours(track, clip), end = clip.start + clip.duration, first = clip.keys.length ? clip.keys[0].t : 0;
  const s = Math.max(prevEnd, 0, Math.min(start, clip.start + first, end - 1 / 60));
  if (Math.abs(s - clip.start) < 1e-9) return;
  const shared = show.timeline.tracks.some(t => (t.clips || []).some(c => c !== clip && c.keys === clip.keys));
  if (shared) { clip.keys = clip.keys.map(k => ({ ...k })); clip.ghost = false; }
  for (const k of clip.keys) k.t += clip.start - s;
  clip.start = s; clip.duration = end - s;
}

// The track that animates a parameter of a surface, made if there isn't one. Null if the surface is gone.
export function trackFor(show, surfaceId, key) {
  if (!show.surfaces.some(s => s.id === surfaceId) || !PARAMS.has(key)) return null;
  const tl = show.timeline, path = trackPath(surfaceId, key);
  let t = findTrack(show, path);
  if (!t) { t = { id: nextId(tl, 'track'), kind: 'param', name: '', path, muted: false, clips: [] }; tl.tracks.push(t); }
  return t;
}
// A path moved onto another surface: "surface/3/opacity" -> "surface/7/opacity".
const onSurface = (path, surfaceId) => { const p = parsePath(path); return p ? trackPath(surfaceId, p.key) : path; };

// A clip as it is now, for the clipboard (a copy: later edits don't change it).
export function copyClip(track, clip) {
  return { kind: 'clip', path: track.path, start: clip.start, duration: clip.duration, keys: clip.keys.map(k => ({ ...k })) };
}
// Pastes a copied clip at `at` (or the next free spot after it) on its own track, on `target` (a track path), or on
// the same parameter of `surface`. Returns { track, clip } or null.
export function pasteClip(show, copied, at, { target = null, surface = 0 } = {}) {
  const p = parsePath(target || (surface ? onSurface(copied.path, surface) : copied.path));
  const track = p && trackFor(show, p.id, p.key);
  if (!track) return null;
  const tl = show.timeline, keys = copied.keys.map(k => ({ ...k, v: cleanValue(p.key, k.v) ?? k.v }));
  const clip = { id: nextId(tl, 'clip'), name: `Clip-${tl.counters.clip}`, start: freeStart(track, at, copied.duration), duration: copied.duration, ghost: false, keys };
  track.clips.push(clip); sortClips(track); grow(show, clip.start + clip.duration);
  return { track, clip };
}

// ---------------------------------------------------------------- copying and pasting keyframes
// items: [{ track, clip, key }] -> what is kept on the clipboard (times relative to the earliest key).
export function copyKeys(items) {
  if (!items.length) return null;
  const abs = items.map(i => ({ path: i.track.path, t: i.clip.start + i.key.t, v: i.key.v, interp: i.key.interp }));
  const t0 = Math.min(...abs.map(a => a.t));
  return { kind: 'keys', start: t0, items: abs.map(a => ({ ...a, t: a.t - t0 })) };
}
// Pastes them with the earliest at `at`. Keys copied from ONE track can go onto another (`target`, a track path);
// keys copied from one surface can go onto another `surface` (each onto the same parameter there).
// Returns the new keys as [{ track, clip, key }].
export function pasteKeys(show, copied, at, target = null, surface = 0) {
  const oneTrack = new Set(copied.items.map(i => i.path)).size === 1, out = [];
  for (const i of copied.items) {
    const path = parsePath(oneTrack && target ? target : surface ? onSurface(i.path, surface) : i.path);
    if (!path || !show.surfaces.some(s => s.id === path.id)) continue;           // its surface was deleted
    const time = at + i.t, track = setKey(show, path.id, path.key, time, i.v, i.interp);
    if (!track) continue;
    const k = keysOf(track).find(x => Math.abs(x.time - time) < 0.5 / 60);
    if (k) out.push({ track, clip: k.clip, key: k.key });
  }
  return out;
}
// Deletes several keyframes (found by object, since indices shift as they go).
export function deleteKeys(show, items) {
  for (const i of items) { const idx = i.clip.keys.indexOf(i.key); if (idx >= 0) deleteKey(show, i.track, i.clip, idx); }
}
// Moves keyframes by `dt` seconds (each stays inside its clip, at or after the clip's start). `from` = their times
// before the move, as returned by keyTimes(); calling it again with another dt starts from those times again.
export const keyTimes = items => items.map(i => ({ ...i, t0: i.key.t, exact: endsAtLastKey(i.clip) }));
export function shiftKeys(show, moving, dt) {
  for (const m of moving) m.key.t = Math.max(0, m.t0 + dt);
  for (const clip of new Set(moving.map(m => m.clip))) {
    clip.keys.sort((a, b) => a.t - b.t);
    const exact = moving.find(m => m.clip === clip).exact, last = clip.keys[clip.keys.length - 1].t;
    clip.duration = exact ? Math.max(1 / 60, last) : Math.max(clip.duration, last);
    grow(show, clip.start + clip.duration);
  }
}

export function deleteKey(show, track, clip, index) {
  const exact = endsAtLastKey(clip);
  clip.keys.splice(index, 1);
  if (exact && clip.keys.length) clip.duration = Math.max(1 / 60, clip.keys[clip.keys.length - 1].t);
  if (!clip.keys.length) track.clips = track.clips.filter(c => c !== clip);
  if (!track.clips.length) show.timeline.tracks = show.timeline.tracks.filter(t => t !== track);
}

// Moves a key to a new time on the timeline (within its clip; the clip grows if needed). Returns its new index.
export function moveKey(show, clip, index, time) {
  const k = clip.keys[index], exact = endsAtLastKey(clip);
  k.t = Math.max(0, time - clip.start);
  clip.keys.sort((a, b) => a.t - b.t);
  clip.duration = exact ? Math.max(1 / 60, clip.keys[clip.keys.length - 1].t) : Math.max(clip.duration, k.t);
  show.timeline.length = Math.max(show.timeline.length, clip.start + clip.duration);
  return clip.keys.indexOf(k);
}

export function removeTrack(show, track) { show.timeline.tracks = show.timeline.tracks.filter(t => t !== track); }

// A readable name for a parameter track: "Cube · Turn Y".
export function trackLabel(show, track) {
  if (track.kind === 'audio') return track.name || 'Audio';
  const p = parsePath(track.path), s = p && show.surfaces.find(x => x.id === p.id), d = p && PARAMS.get(p.key);
  const what = d ? (['Sun', 'Point light', 'Spotlight', 'Material'].includes(d.group) ? `${d.group} ${d.label}` : d.label) : p ? p.key : track.path;
  return `${s ? s.name : '?'} · ${what}`;                         // e.g. "Cube · Point light X", "Cube · Turn Y"
}

// ---------------------------------------------------------------- songs and markers
// Adds a song as an audio asset and puts it on the first audio track, after whatever is already there.
export function addSong(show, name, path, duration) {
  const tl = show.timeline;
  const id = Math.max(show.counters.asset | 0, 1, ...[...show.audioAssets.keys()].map(k => k + 1));
  show.counters.asset = id + 1;
  show.audioAssets.set(id, { id, name, path, bpm: 0, beatOffset: 0, url: null });
  let track = tl.tracks.find(t => t.kind === 'audio');
  if (!track) {
    tl.counters.audioTrack = (tl.counters.audioTrack | 0) + 1;
    track = { id: nextId(tl, 'track'), kind: 'audio', name: `Audio-${tl.counters.audioTrack}`, path: `audio/${tl.counters.audioTrack}`, muted: false, audioClips: [] };
    tl.tracks.unshift(track);
  }
  const start = track.audioClips.reduce((e, c) => Math.max(e, c.start + c.duration), 0);
  track.audioClips.push({ id: nextId(tl, 'clip'), name, asset: id, start, duration, offset: 0, gain: 1 });
  tl.length = Math.max(tl.length, start + duration);
  return id;
}

export function addMarker(show, time) {
  const tl = show.timeline;
  if (tl.markers.some(m => Math.abs(m.time - time) < 1 / 60)) return;
  tl.markers.push({ id: nextId(tl, 'marker'), time: Math.max(0, time) });
  tl.markers.sort((a, b) => a.time - b.time);
}

// The nearest beat (from the first song with a tempo) or marker to t, if within `tolerance` seconds.
export function snapTime(show, t, tolerance) {
  let best = t, bd = tolerance;
  for (const m of show.timeline.markers) if (Math.abs(m.time - t) < bd) { bd = Math.abs(m.time - t); best = m.time; }
  const grid = beatGrid(show);
  if (grid) {
    const b = Math.round((t - grid.start) / grid.beat) * grid.beat + grid.start;
    if (Math.abs(b - t) < bd) best = b;
  }
  return Math.max(0, best);
}

// Where the beats fall on the timeline: from the first audio clip whose song has a tempo.
export function beatGrid(show) {
  for (const t of show.timeline.tracks) {
    if (t.kind !== 'audio') continue;
    for (const c of t.audioClips) {
      const a = show.audioAssets.get(c.asset);
      if (a && a.bpm > 0) { const beat = 60 / a.bpm; return { beat, start: c.start - c.offset + a.beatOffset, bpm: a.bpm }; }
    }
  }
  return null;
}

// ---------------------------------------------------------------- folders (as in the desktop app)
// A folder only organises: hiding one hides everything inside it; the drawing order stays the surfaces' order.
export function addFolder(show, name = '', parent = 0) {
  const id = Math.max(show.counters.folder | 0, 1, ...[...show.folders.keys()].map(k => k + 1));
  show.counters.folder = id + 1;
  show.folders.set(id, { id, name: name || `Folder ${id}`, parent: show.folders.has(parent) ? parent : 0, visible: true, open: true });
  return id;
}
// Is folder `inner` the folder `outer`, or inside it? (0 = the top level, which holds everything.)
export function folderWithin(show, inner, outer) {
  if (outer === 0) return true;
  for (let guard = 0; inner && guard <= show.folders.size; guard++) {
    if (inner === outer) return true;
    inner = show.folders.get(inner)?.parent || 0;
  }
  return false;
}
export function folderDepth(show, id) {
  let d = 0;
  for (let guard = 0; id && guard <= show.folders.size; guard++, d++) id = show.folders.get(id)?.parent || 0;
  return d;
}
// Moves a folder into another (0 = top level); refused if that would put it inside itself.
export function moveFolder(show, id, parent) {
  const f = show.folders.get(id);
  if (!f || (parent && !show.folders.has(parent)) || folderWithin(show, parent, id)) return false;
  f.parent = parent;
  return true;
}
export function moveSurfaceToFolder(show, surfaceId, folder) {
  const s = show.surfaces.find(x => x.id === surfaceId);
  if (!s || (folder && !show.folders.has(folder))) return false;
  s.folder = folder;
  return true;
}
// Deletes a folder but nothing in it: its surfaces and folders move up to its parent.
export function removeFolder(show, id) {
  const f = show.folders.get(id);
  if (!f) return;
  for (const o of show.folders.values()) if (o.parent === id) o.parent = f.parent;
  for (const s of show.surfaces) if (s.folder === id) s.folder = f.parent;
  show.folders.delete(id);
}

// ---------------------------------------------------------------- song clips (as the desktop app's audio clips)
// Clips on a track never overlap: each one can move or grow only up to its neighbours.
function audioNeighbours(track, clip) {
  const list = [...track.audioClips].sort((a, b) => a.start - b.start), i = list.indexOf(clip);
  return { prevEnd: i > 0 ? list[i - 1].start + list[i - 1].duration : 0, nextStart: i + 1 < list.length ? list[i + 1].start : Infinity };
}
export function moveAudioClip(show, track, clip, start) {
  const { prevEnd, nextStart } = audioNeighbours(track, clip);
  clip.start = Math.max(prevEnd, Math.min(nextStart - clip.duration, start));
  show.timeline.length = Math.max(show.timeline.length, clip.start + clip.duration);
}
// Moves one edge of a clip to a timeline time. songLength = the whole song's length (a clip can't run past it).
export function trimAudioClip(show, track, clip, edge, time, songLength = Infinity) {
  const { prevEnd, nextStart } = audioNeighbours(track, clip), min = 1 / 60;
  if (edge === 'start') {
    const end = clip.start + clip.duration;
    const t = Math.max(prevEnd, clip.start - clip.offset, Math.min(end - min, time));   // can't reach before the song's own start
    clip.offset += t - clip.start; clip.duration = end - t; clip.start = t;
  } else {
    const t = Math.min(nextStart, clip.start + songLength - clip.offset, Math.max(clip.start + min, time));
    clip.duration = t - clip.start;
  }
  show.timeline.length = Math.max(show.timeline.length, clip.start + clip.duration);
}
// Splits a clip in two at a timeline time; returns the second part (or null if the time isn't inside it).
export function splitAudioClip(show, track, clip, time) {
  if (time <= clip.start + 1 / 60 || time >= clip.start + clip.duration - 1 / 60) return null;
  const tl = show.timeline, cut = time - clip.start;
  const second = { ...clip, id: tl.counters.clip++, start: time, offset: clip.offset + cut, duration: clip.duration - cut, name: `${clip.name} (2)` };
  clip.duration = cut;
  track.audioClips.push(second);
  track.audioClips.sort((a, b) => a.start - b.start);
  return second;
}
export function deleteAudioClip(show, track, clip) {
  track.audioClips = track.audioClips.filter(c => c !== clip);
  if (!track.audioClips.length) show.timeline.tracks = show.timeline.tracks.filter(t => t !== track);
}
