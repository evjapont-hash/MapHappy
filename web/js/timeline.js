// The timeline, as in the desktop app's Timeline.h: tracks -> clips -> keyframes, evaluated the same way, so the
// same time always gives the same picture. Kept in full (ids, names, ghosts) so the web editor can edit and save it.
//
//   timeline = { length, tracks: [...], markers: [{ id, time }], counters: { track, clip, marker, audioTrack } }
//   param track = { id, kind: 'param', name, path, muted, clips: [{ id, name, start, duration, ghost, keys }] }
//   audio track = { id, kind: 'audio', name, path, muted, audioClips: [{ id, name, asset, start, duration, offset, gain }] }
//   key = { t, v, interp: 'linear' | 'smooth' | 'hold' }   (a ghost clip shares its keys ARRAY with the original)

const INTERP = { linear: 'linear', smooth: 'smooth', hold: 'hold' };

// Value of a sorted keyframe list at t seconds (clamped to the first and last key).
export function valueAt(keys, t) {
  if (!keys.length) return 0;
  if (t <= keys[0].t) return keys[0].v;
  const last = keys[keys.length - 1];
  if (t >= last.t) return last.v;
  let i = 1;
  while (keys[i].t < t) i++;
  const a = keys[i - 1], b = keys[i], span = b.t - a.t;
  if (span <= 1e-12) return b.v;
  let u = (t - a.t) / span;
  if (a.interp === 'hold') return a.v;
  if (a.interp === 'smooth') u = u * u * (3 - 2 * u);
  return a.v + (b.v - a.v) * u;
}

// A clip `local` seconds after it starts: loops when longer than its keyframes, holds its end once over.
export function clipValue(clip, local) {
  const keys = clip.keys;
  if (!keys.length) return 0;
  local = Math.max(0, Math.min(local, clip.duration));
  const len = keys[keys.length - 1].t;
  if (len > 1e-9 && local > len) {
    const m = local % len;
    local = m < 1e-9 ? len : m;                      // exactly on a cycle boundary = the end of the cycle
  }
  return valueAt(keys, local);
}

// Before the first clip: its starting value. Inside a clip: the animation. Between or after: the previous clip's end, held.
export function evaluateTrack(track, time) {
  const clips = track.clips;
  if (!clips || !clips.length) return null;
  let cur = clips[0];
  for (const c of clips) { if (c.start <= time) cur = c; else break; }
  return clipValue(cur, time - cur.start);
}

export const paramTracks = tl => tl.tracks.filter(t => t.kind === 'param');
export const audioClips = tl => tl.tracks.filter(t => t.kind === 'audio' && !t.muted).flatMap(t => t.audioClips);

// Reads the "timeline" part of a project file, skipping anything damaged.
export function readTimeline(j) {
  const tl = { length: 60, tracks: [], markers: [], counters: { track: 1, clip: 1, marker: 1, audioTrack: 0 } };
  if (!j || typeof j !== 'object') return tl;
  tl.length = Math.max(1, Number(j.length) || 60);
  const ids = new Set(), clipIds = new Set();
  let maxTrack = 0, maxClip = 0, maxMarker = 0;
  for (const t of Array.isArray(j.tracks) ? j.tracks : []) {
    if (!t || typeof t !== 'object' || !(t.id > 0) || ids.has(t.id)) continue;
    ids.add(t.id); maxTrack = Math.max(maxTrack, t.id);
    const base = { id: t.id, name: String(t.name || ''), path: String(t.path || ''), muted: !!t.muted };
    if (t.kind === 'audio') {
      const audio = [];
      for (const c of Array.isArray(t.audioClips) ? t.audioClips : []) {
        if (!c || !(c.id > 0) || clipIds.has(c.id)) continue;
        clipIds.add(c.id); maxClip = Math.max(maxClip, c.id);
        audio.push({ id: c.id, name: String(c.name || ''), asset: c.asset | 0, start: Math.max(0, +c.start || 0), duration: Math.max(1 / 60, +c.duration || 1),
                     offset: Math.max(0, +c.offset || 0), gain: Math.max(0, Math.min(4, c.gain ?? 1)) });
      }
      audio.sort((a, b) => a.start - b.start);
      tl.tracks.push({ ...base, kind: 'audio', audioClips: audio });
      continue;
    }
    const datas = (Array.isArray(t.clipData) ? t.clipData : []).map(d => {
      const keys = (d && Array.isArray(d.keys) ? d.keys : [])
        .filter(k => k && typeof k === 'object')
        .map(k => ({ t: Math.max(0, +k.t || 0), v: +k.v || 0, interp: INTERP[k.curve] || 'smooth' }));
      keys.sort((a, b) => a.t - b.t);
      return keys;
    });
    const clips = [];
    for (const c of Array.isArray(t.clips) ? t.clips : []) {
      const keys = datas[c.data];
      if (!keys || !(c.id > 0) || clipIds.has(c.id)) continue;
      clipIds.add(c.id); maxClip = Math.max(maxClip, c.id);
      if (!keys.length) keys.push({ t: 0, v: 0, interp: 'smooth' });
      clips.push({ id: c.id, name: String(c.name || `Clip-${c.id}`), start: Math.max(0, +c.start || 0), duration: Math.max(1 / 60, +c.duration || 1), ghost: !!c.ghost, keys });
    }
    clips.sort((a, b) => a.start - b.start);
    tl.tracks.push({ ...base, kind: 'param', clips });
  }
  for (const m of Array.isArray(j.markers) ? j.markers : []) {
    if (!m || typeof m.time !== 'number' || !(m.id > 0)) continue;
    tl.markers.push({ id: m.id, time: Math.max(0, m.time) }); maxMarker = Math.max(maxMarker, m.id);
  }
  tl.markers.sort((a, b) => a.time - b.time);
  const c = j.counters || {};
  tl.counters = { track: Math.max(c.track | 0, maxTrack + 1), clip: Math.max(c.clip | 0, maxClip + 1), marker: Math.max(c.marker | 0, maxMarker + 1),
                  audioTrack: Math.max(c.audioTrack | 0, tl.tracks.filter(t => t.kind === 'audio').length) };
  for (const t of tl.tracks) for (const cl of t.clips || t.audioClips) tl.length = Math.max(tl.length, cl.start + cl.duration);
  return tl;
}

// The timeline back as project-file JSON (the desktop app's format). Clips sharing a keys array stay linked.
export function writeTimeline(tl) {
  const tracks = tl.tracks.map(t => {
    const o = { id: t.id, kind: t.kind, name: t.name, path: t.path, muted: t.muted };
    if (t.kind === 'audio') {
      o.audioClips = t.audioClips.map(c => ({ id: c.id, name: c.name, asset: c.asset, start: c.start, duration: c.duration, offset: c.offset, gain: c.gain }));
      return o;
    }
    const datas = [], clips = [];
    for (const c of t.clips) {
      let idx = datas.indexOf(c.keys);
      if (idx < 0) { idx = datas.length; datas.push(c.keys); }
      clips.push({ id: c.id, name: c.name, start: c.start, duration: c.duration, ghost: c.ghost, data: idx });
    }
    o.clipData = datas.map(keys => ({ keys: keys.map(k => ({ t: k.t, v: k.v, curve: k.interp })) }));
    o.clips = clips;
    return o;
  });
  return { length: tl.length, tracks, markers: tl.markers.map(m => ({ id: m.id, time: m.time })), counters: { ...tl.counters } };
}
