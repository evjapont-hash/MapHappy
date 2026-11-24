// The media generators' types and parameters, as in the desktop app's GeneratorDefs.h (for the web editor's
// "+ Generator" menu and its inspector). Parameter "x" is the shader uniform "u_x".
// kind: 'float' | 'int' | 'bool' | 'color' | 'choice' | 'text'

const f = (key, label, min, max, def) => ({ key, label, kind: 'float', min, max, def });
const i = (key, label, min, max, def) => ({ key, label, kind: 'int', min, max, def });
const b = (key, label, def) => ({ key, label, kind: 'bool', def });
const c = (key, label, r, g, bl, a = 1) => ({ key, label, kind: 'color', def: [r, g, bl, a] });
const ch = (key, label, choices, def) => ({ key, label, kind: 'choice', choices, def });
const t = (key, label, def) => ({ key, label, kind: 'text', def });

export const GEN_TYPES = [
  { type: 'testcard', label: 'Test Card', params: [i('columns', 'Columns', 2, 64, 16), i('rows', 'Rows', 2, 36, 9), t('label', 'Label', 'MAPHAPPY'),
    b('showResolution', 'Show resolution', true), b('sweep', 'Moving line', true), c('accent', 'Accent', 1, 0.6, 0)] },
  { type: 'grid', label: 'Grid', params: [i('columns', 'Columns', 1, 128, 16), i('rows', 'Rows', 1, 128, 9), f('thickness', 'Line width (px)', 1, 64, 3),
    c('lineColor', 'Lines', 1, 1, 1), c('background', 'Background', 0, 0, 0), f('scrollX', 'Scroll X', -8, 8, 0), f('scrollY', 'Scroll Y', -8, 8, 0)] },
  { type: 'text', label: 'Text', params: [t('text', 'Text', 'HELLO'), ch('font', 'Font', ['sans', 'pixel'], 'sans'), ch('align', 'Align', ['left', 'center', 'right'], 'center'),
    f('size', 'Size', 0.02, 1, 0.25), f('posY', 'Height on picture', 0, 1, 0.5), c('color', 'Colour', 1, 1, 1), c('background', 'Background', 0, 0, 0), f('scroll', 'Scroll', -2, 2, 0)] },
  { type: 'solid', label: 'Solid Colour', params: [c('color', 'Colour', 1, 0.5, 0)] },
  { type: 'gradient', label: 'Gradient', params: [c('colorA', 'From', 0.05, 0.1, 0.6), c('colorB', 'To', 1, 0.3, 0.5), ch('style', 'Style', ['linear', 'radial'], 'linear'),
    f('angle', 'Angle', -360, 360, 0), f('spin', 'Spin (deg/s)', -360, 360, 0), i('repeat', 'Repeat', 1, 16, 1), f('scroll', 'Scroll', -4, 4, 0)] },
  { type: 'strobe', label: 'Strobe', params: [f('rate', 'Rate (Hz)', 0.1, 30, 4), f('duty', 'On time', 0.01, 1, 0.25), f('fade', 'Fade out', 0, 1, 0), c('onColor', 'Flash', 1, 1, 1), c('offColor', 'Off', 0, 0, 0)] },
  { type: 'lines', label: 'Lines', params: [i('count', 'Count', 1, 200, 12), f('thickness', 'Thickness', 0.01, 0.99, 0.3), f('softness', 'Softness', 0, 1, 0),
    f('angle', 'Angle', -180, 180, 0), f('speed', 'Speed (lines/s)', -10, 10, 0.5), c('color', 'Lines', 1, 1, 1), c('background', 'Background', 0, 0, 0)] },
];

export const genType = type => GEN_TYPES.find(g => g.type === type);

// A new generator's media entry (the project file's format), with every default.
export function newGeneratorEntry(id, type, name) {
  const params = {};
  for (const p of genType(type).params) params[p.key] = Array.isArray(p.def) ? [...p.def] : p.def;
  return { id, name, kind: 'generator', type, playing: true, loop: true, width: 1920, height: 1080, params };
}
