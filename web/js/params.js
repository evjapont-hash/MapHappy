// Every animatable surface parameter, as in the desktop app's Params.h: same keys, labels, groups, ranges, defaults.
// A surface keeps its values in s.p[key]. Tracks drive them through paths like "surface/3/opacity".

const all = () => true, line = s => s.shape === 'line', obj = s => s.shape === 'object3d';

// format: 'pct' shown as 0..100 %, 'deg' degrees, 'bool' on/off, '' plain
// [key, label, group, min, max, default, format, appliesTo]
const TABLE = [
  ['opacity', 'Opacity', 'Appearance', 0, 1, 1, 'pct'], ['visible', 'Visible', 'Appearance', 0, 1, 1, 'bool'], ['feather', 'Feather', 'Appearance', 0, 1, 0, 'pct'],
  ['posX', 'Position X', 'Transform', -1, 1, 0, 'pct'], ['posY', 'Position Y', 'Transform', -1, 1, 0, 'pct'], ['rotation', 'Rotation', 'Transform', -720, 720, 0, 'deg'],
  ['spin', 'Spin (°/s)', 'Transform', -720, 720, 0, 'deg'],
  ['scale', 'Scale', 'Transform', 0, 4, 1, 'pct'], ['scaleX', 'Scale X', 'Transform', 0, 4, 1, 'pct'], ['scaleY', 'Scale Y', 'Transform', 0, 4, 1, 'pct'],
  ['contentX', 'Offset X', 'Content', -1, 1, 0, 'pct'], ['contentY', 'Offset Y', 'Content', -1, 1, 0, 'pct'], ['contentScale', 'Zoom', 'Content', 0.1, 8, 1, 'pct'],
  ['contentRotation', 'Rotation', 'Content', -720, 720, 0, 'deg'],
  ['tintR', 'Red', 'Color', 0, 1, 1, 'pct'], ['tintG', 'Green', 'Color', 0, 1, 1, 'pct'], ['tintB', 'Blue', 'Color', 0, 1, 1, 'pct'],
  ['hue', 'Hue', 'Color', -360, 360, 0, 'deg'], ['saturation', 'Saturation', 'Color', 0, 2, 1, 'pct'], ['contrast', 'Contrast', 'Color', 0, 2, 1, 'pct'],
  ['brightness', 'Brightness', 'Color', -1, 1, 0, 'pct'],
  ['lineWidth', 'Line width', 'Line', 0.001, 0.5, 0.02, 'pct', line],
  ['rotX', 'Turn X', '3D Object', -720, 720, -20, 'deg', obj], ['rotY', 'Turn Y', '3D Object', -720, 720, 30, 'deg', obj], ['rotZ', 'Turn Z', '3D Object', -720, 720, 0, 'deg', obj],
  ['spinX', 'Spin X (°/s)', '3D Object', -720, 720, 0, 'deg', obj], ['spinY', 'Spin Y (°/s)', '3D Object', -720, 720, 0, 'deg', obj], ['spinZ', 'Spin Z (°/s)', '3D Object', -720, 720, 0, 'deg', obj],
  ['objScale', 'Size', '3D Object', 0, 4, 1, 'pct', obj], ['fov', 'Field of view', '3D Object', 5, 150, 40, 'deg', obj], ['shading', 'Lighting', '3D Object', 0, 1, 0, 'pct', obj],
  ['lightModel', 'Light model', 'Lighting', 0, 3, 2, 'choice', obj], ['ambient', 'Ambient', 'Lighting', 0, 1, 0.2, 'pct', obj],
  ['kd', 'Diffuse', 'Material', 0, 2, 1, 'pct', obj], ['ks', 'Specular', 'Material', 0, 2, 0.5, 'pct', obj], ['shininess', 'Shininess', 'Material', 1, 256, 32, '', obj],
  ['dirIntensity', 'Strength', 'Sun', 0, 3, 1, 'pct', obj], ['dirAzimuth', 'Direction', 'Sun', -180, 180, -30, 'deg', obj], ['dirElevation', 'Height', 'Sun', -90, 90, 30, 'deg', obj],
  ['dirR', 'Red', 'Sun', 0, 1, 1, 'pct', obj], ['dirG', 'Green', 'Sun', 0, 1, 1, 'pct', obj], ['dirB', 'Blue', 'Sun', 0, 1, 1, 'pct', obj],
  ['pointIntensity', 'Strength', 'Point light', 0, 5, 0, 'pct', obj], ['pointX', 'X', 'Point light', -5, 5, 1.5, '', obj], ['pointY', 'Y', 'Point light', -5, 5, 1, '', obj],
  ['pointZ', 'Z', 'Point light', -5, 5, 2, '', obj], ['pointRange', 'Reach', 'Point light', 0.5, 20, 4, '', obj],
  ['pointR', 'Red', 'Point light', 0, 1, 1, 'pct', obj], ['pointG', 'Green', 'Point light', 0, 1, 0.6, 'pct', obj], ['pointB', 'Blue', 'Point light', 0, 1, 0.3, 'pct', obj],
  ['spotIntensity', 'Strength', 'Spotlight', 0, 5, 0, 'pct', obj], ['spotX', 'X', 'Spotlight', -5, 5, -2, '', obj], ['spotY', 'Y', 'Spotlight', -5, 5, 2, '', obj],
  ['spotZ', 'Z', 'Spotlight', -5, 5, 2.5, '', obj], ['spotAngle', 'Cone angle', 'Spotlight', 1, 90, 25, 'deg', obj], ['spotSoftness', 'Softness', 'Spotlight', 0, 1, 0.3, 'pct', obj],
  ['spotR', 'Red', 'Spotlight', 0, 1, 0.4, 'pct', obj], ['spotG', 'Green', 'Spotlight', 0, 1, 0.7, 'pct', obj], ['spotB', 'Blue', 'Spotlight', 0, 1, 1, 'pct', obj],
];

export const PARAMS = new Map(TABLE.map(([key, label, group, min, max, def, format, applies = all]) =>
  [key, { key, label, group, min, max, def, format, isBool: format === 'bool', applies }]));

export const LIGHT_MODEL_NAMES = ['Unlit', 'Phong', 'Blinn-Phong', 'Toon'];

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

// How a value is shown: 0.5 -> "50%", 30 -> "30°".
export function formatValue(d, v) {
  if (d.format === 'pct') return `${Math.round(v * 100)}%`;
  if (d.format === 'deg') return `${v.toFixed(1)}°`;
  if (d.format === 'bool') return v >= 0.5 ? 'On' : 'Off';
  if (d.format === 'choice' && d.key === 'lightModel') return LIGHT_MODEL_NAMES[Math.round(v)] || '';
  return Number.isInteger(v) ? String(v) : v.toFixed(2);
}

// "surface/3/posX" -> { id: 3, key: "posX" }
export function parsePath(path) {
  const m = /^surface\/(\d+)\/(.+)$/.exec(path || '');
  return m ? { id: Number(m[1]), key: m[2] } : null;
}
