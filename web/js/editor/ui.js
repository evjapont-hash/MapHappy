// Small DOM helpers for the web editor's panels.
import { icon } from './icons.js';

// el('div', { class: 'row', text: 'Hi', on: { click: fn } }, child, ...)   icon: 'name' puts that icon first
export function el(tag, attrs = {}, ...children) {
  const n = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v === undefined || v === null || v === false) continue;
    if (k === 'icon') continue;
    if (k === 'class') n.className = v;
    else if (k === 'text') n.textContent = v;
    else if (k === 'on') for (const [ev, fn] of Object.entries(v)) n.addEventListener(ev, fn);
    else if (k === 'style') Object.assign(n.style, v);
    else if (k === 'iconSize') continue;
    else if (k in n && typeof v !== 'string') n[k] = v;
    else n.setAttribute(k, v === true ? '' : v);
  }
  if (attrs.icon) n.prepend(icon(attrs.icon, attrs.iconSize || 16));
  for (const c of children.flat()) if (c !== null && c !== undefined && c !== false) n.append(c instanceof Node ? c : document.createTextNode(String(c)));
  return n;
}

export const clear = n => { while (n.firstChild) n.firstChild.remove(); return n; };

// A labelled control row.
export const row = (label, ...controls) => el('div', { class: 'row' }, el('label', { text: label }), el('div', { class: 'ctl' }, ...controls));

// A slider with its value shown (click the value to type one), and optionally a keyframe button.
//   opts: { label, min, max, step, get(), set(v, final), format(v), animated(), onKey(), title }
// Returns { node, refresh() } — refresh() redraws it from get() (e.g. while the timeline plays).
export function slider(opts) {
  const step = opts.step ?? (opts.max - opts.min) / 1000;
  const input = el('input', { type: 'range', min: opts.min, max: opts.max, step, title: opts.title });
  const value = el('span', { class: 'value', title: 'Click to type a value' });
  const key = opts.onKey ? el('button', { class: 'key', title: 'Add a keyframe here, at the playhead', 'aria-label': 'Add keyframe', icon: 'key', iconSize: 12 }) : null;
  const label = el('label', { text: opts.label });
  const node = el('div', { class: 'row slider' }, label, el('div', { class: 'ctl' }, input, value, key));
  let dragging = false;
  input.addEventListener('input', () => { dragging = true; opts.set(Number(input.value), false); refresh(); });
  input.addEventListener('change', () => { dragging = false; opts.set(Number(input.value), true); refresh(); });
  value.addEventListener('click', () => {
    const shown = opts.typed ? opts.typed(opts.get()) : opts.get();
    const box = el('input', { type: 'number', class: 'typed', value: +Number(shown).toFixed(4), step: 'any' });
    value.replaceWith(box); box.focus(); box.select();
    const done = ok => { if (ok && box.value !== '') opts.set(opts.untyped ? opts.untyped(Number(box.value)) : Number(box.value), true); box.replaceWith(value); refresh(); };
    box.addEventListener('keydown', e => { if (e.key === 'Enter') done(true); if (e.key === 'Escape') done(false); e.stopPropagation(); });
    box.addEventListener('blur', () => done(true));
  });
  if (key) key.addEventListener('click', () => { opts.onKey(); refresh(); });
  function refresh() {
    const v = opts.get();
    if (!dragging && document.activeElement !== input) input.value = v;
    value.textContent = opts.format ? opts.format(v) : (+v).toFixed(2);
    const anim = opts.animated ? opts.animated() : false;
    node.classList.toggle('animated', anim);
    if (key) key.classList.toggle('on', anim);
  }
  refresh();
  return { node, refresh };
}

// A drop-down.  options: [[value, label], ...]
export function select(options, current, onChange, attrs = {}) {
  const s = el('select', attrs, options.map(([v, l]) => el('option', { value: String(v), text: l })));
  s.value = String(current);
  s.addEventListener('change', () => onChange(s.value));
  return s;
}

export function checkbox(checked, onChange, attrs = {}) {
  const c = el('input', { type: 'checkbox', ...attrs });
  c.checked = !!checked;
  c.addEventListener('change', () => onChange(c.checked));
  return c;
}

export function textInput(value, onChange, attrs = {}) {
  const t = el('input', { type: 'text', value, ...attrs });
  t.addEventListener('change', () => onChange(t.value));
  t.addEventListener('keydown', e => e.stopPropagation());       // typing never triggers shortcuts
  return t;
}

// A collapsible section that remembers whether it was open.
const openState = new Map();
export function section(title, build, { open = false, id = title } = {}) {
  const d = el('details', { class: 'section' }, el('summary', { text: title }));
  d.open = openState.has(id) ? openState.get(id) : open;
  d.addEventListener('toggle', () => openState.set(id, d.open));
  const body = el('div', { class: 'body' });
  d.append(body);
  build(body);
  return d;
}

// A file picker button.
export function fileButton(label, accept, multiple, onFiles, attrs = {}) {   // attrs may include icon
  const input = el('input', { type: 'file', accept, multiple, style: { display: 'none' } });
  input.addEventListener('change', () => { const f = [...input.files]; input.value = ''; if (f.length) onFiles(f); });
  return el('span', {}, el('button', { text: label, on: { click: () => input.click() }, ...attrs }), input);
}

// "rgba" array (0..1) <-> "#rrggbb"
export const toHex = c => '#' + c.slice(0, 3).map(v => Math.round(Math.max(0, Math.min(1, v)) * 255).toString(16).padStart(2, '0')).join('');
export const fromHex = (h, a = 1) => [1, 3, 5].map(i => parseInt(h.slice(i, i + 2), 16) / 255).concat([a]);
