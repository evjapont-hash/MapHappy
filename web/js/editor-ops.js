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
