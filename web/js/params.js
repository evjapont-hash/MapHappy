// Every animatable surface parameter, as in the desktop app's Params.h: same keys, ranges and defaults.
// A surface keeps its values in s.p[key]. Tracks drive them through paths like "surface/3/opacity".

const all = () => true, line = s => s.shape === 'line', obj = s => s.shape === 'object3d';

// [key, min, max, default, isBool, appliesTo]
const TABLE = [
  ['opacity', 0, 1, 1], ['visible', 0, 1, 1, true], ['feather', 0, 1, 0],
  ['posX', -1, 1, 0], ['posY', -1, 1, 0], ['rotation', -720, 720, 0], ['scale', 0, 4, 1], ['scaleX', 0, 4, 1], ['scaleY', 0, 4, 1],
  ['contentX', -1, 1, 0], ['contentY', -1, 1, 0], ['contentScale', 0.1, 8, 1], ['contentRotation', -720, 720, 0],
  ['tintR', 0, 1, 1], ['tintG', 0, 1, 1], ['tintB', 0, 1, 1], ['hue', -360, 360, 0], ['saturation', 0, 2, 1], ['contrast', 0, 2, 1], ['brightness', -1, 1, 0],
  ['lineWidth', 0.001, 0.5, 0.02, false, line],
  ['rotX', -720, 720, -20, false, obj], ['rotY', -720, 720, 30, false, obj], ['rotZ', -720, 720, 0, false, obj],
  ['objScale', 0, 4, 1, false, obj], ['fov', 5, 150, 40, false, obj], ['shading', 0, 1, 0, false, obj],
  ['lightModel', 0, 3, 2, false, obj], ['ambient', 0, 1, 0.2, false, obj], ['kd', 0, 2, 1, false, obj], ['ks', 0, 2, 0.5, false, obj], ['shininess', 1, 256, 32, false, obj],
  ['dirIntensity', 0, 3, 1, false, obj], ['dirAzimuth', -180, 180, -30, false, obj], ['dirElevation', -90, 90, 30, false, obj],
  ['dirR', 0, 1, 1, false, obj], ['dirG', 0, 1, 1, false, obj], ['dirB', 0, 1, 1, false, obj],
  ['pointIntensity', 0, 5, 0, false, obj], ['pointX', -5, 5, 1.5, false, obj], ['pointY', -5, 5, 1, false, obj], ['pointZ', -5, 5, 2, false, obj], ['pointRange', 0.5, 20, 4, false, obj],
  ['pointR', 0, 1, 1, false, obj], ['pointG', 0, 1, 0.6, false, obj], ['pointB', 0, 1, 0.3, false, obj],
  ['spotIntensity', 0, 5, 0, false, obj], ['spotX', -5, 5, -2, false, obj], ['spotY', -5, 5, 2, false, obj], ['spotZ', -5, 5, 2.5, false, obj],
  ['spotAngle', 1, 90, 25, false, obj], ['spotSoftness', 0, 1, 0.3, false, obj],
  ['spotR', 0, 1, 0.4, false, obj], ['spotG', 0, 1, 0.7, false, obj], ['spotB', 0, 1, 1, false, obj],
];

export const PARAMS = new Map(TABLE.map(([key, min, max, def, isBool = false, applies = all]) => [key, { key, min, max, def, isBool, applies }]));

export function defaultParams() {
  const p = {};
  for (const d of PARAMS.values()) p[d.key] = d.def;
  return p;
}

// Keeps a value in its parameter's range (on/off ones become 0 or 1). Returns null for unknown or broken values.
export function cleanValue(key, v) {
  const d = PARAMS.get(key);
  if (!d) return null;
  if (typeof v === 'boolean') v = v ? 1 : 0;
  if (typeof v !== 'number' || !Number.isFinite(v)) return null;
  return d.isBool ? (v >= 0.5 ? 1 : 0) : Math.max(d.min, Math.min(d.max, v));
}

// "surface/3/posX" -> { id: 3, key: "posX" }
export function parsePath(path) {
  const m = /^surface\/(\d+)\/(.+)$/.exec(path || '');
  return m ? { id: Number(m[1]), key: m[2] } : null;
}
