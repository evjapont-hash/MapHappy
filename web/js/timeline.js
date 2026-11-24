// The timeline, as in the desktop app's Timeline.h: tracks -> clips -> keyframes, evaluated the same way, so the
// same time always gives the same picture.

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
  if (!clips.length) return null;
  let cur = clips[0];
  for (const c of clips) { if (c.start <= time) cur = c; else break; }
  return clipValue(cur, time - cur.start);
}

// Reads the "timeline" part of a project file. Ghost clips keep sharing their keyframes (by reference).
export function readTimeline(j) {
  const tl = { length: 60, paramTracks: [], audioClips: [], markers: [] };
  if (!j || typeof j !== 'object') return tl;
  tl.length = Math.max(1, Number(j.length) || 60);
  for (const t of Array.isArray(j.tracks) ? j.tracks : []) {
    if (!t || typeof t !== 'object') continue;
    if (t.kind === 'audio') {
      if (t.muted) continue;
      for (const c of Array.isArray(t.audioClips) ? t.audioClips : [])
        tl.audioClips.push({ asset: c.asset, start: Math.max(0, +c.start || 0), duration: Math.max(0, +c.duration || 0), offset: Math.max(0, +c.offset || 0), gain: Math.max(0, Math.min(4, c.gain ?? 1)) });
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
      if (!keys) continue;
      clips.push({ start: Math.max(0, +c.start || 0), duration: Math.max(1 / 60, +c.duration || 1), keys });
    }
    clips.sort((a, b) => a.start - b.start);
    tl.paramTracks.push({ path: String(t.path || ''), muted: !!t.muted, clips });
    for (const c of clips) tl.length = Math.max(tl.length, c.start + c.duration);
  }
  for (const m of Array.isArray(j.markers) ? j.markers : []) if (m && typeof m.time === 'number') tl.markers.push(m.time);
  return tl;
}
