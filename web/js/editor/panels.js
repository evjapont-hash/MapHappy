// The web editor's side panels: Surfaces (list + inspector), Media (generators, files, songs) and Presets.
import { el, clear, row, slider, select, checkbox, textInput, section, fileButton, toHex, fromHex } from './ui.js';
import { PARAMS, formatValue, cleanValue, LIGHT_MODEL_NAMES } from '../params.js';
import { GEN_TYPES, genType, newGeneratorEntry } from '../gendefs.js';
import { createSource } from '../media.js';
import { capturePreset, isShown } from '../show.js';

// Are a folder and every folder above it visible?
const isShownFolder = (show, id) => isShown(show, { p: { visible: 1 }, folder: id });
import { uniquePath } from '../files.js';
import * as ops from '../editor-ops.js';

const SHAPE_LIST = [['quad', 'Quad'], ['circle', 'Circle'], ['triangle', 'Triangle'], ['line', 'Line'], ['freeform', 'Freeform'], ['object3d', '3D Object']];
const AUDIO_EXT = /\.(mp3|wav|ogg|oga|flac|m4a|aac)$/i, VIDEO_EXT = /\.(mp4|webm|mov|m4v|ogv)$/i, OBJ_EXT = /\.obj$/i;

export class Panels {
  constructor(ed, root) {
    this.ed = ed; this.root = root; this.tab = 'surfaces'; this.bindings = [];
    this.tabs = el('div', { class: 'tabs' }, ...[['surfaces', 'Surfaces'], ['media', 'Media'], ['presets', 'Presets']].map(([id, label]) =>
      el('button', { 'data-tab': id, text: label, on: { click: () => { this.tab = id; this.render(); } } })));
    this.body = el('div', { class: 'panel-body' });
    root.append(this.tabs, this.body);
  }

  // Redraws the values (the timeline may be changing them) without rebuilding anything.
  refreshValues() { for (const b of this.bindings) b.refresh(); }

  render() {
    for (const b of this.tabs.children) b.classList.toggle('active', b.dataset.tab === this.tab);
    const scroll = this.body.scrollTop;
    clear(this.body); this.bindings = [];
    if (this.tab === 'surfaces') this.surfaces(); else if (this.tab === 'media') this.media(); else this.presets();
    this.body.scrollTop = scroll;
  }

  // ------------------------------------------------------------------ surfaces (and folders)
  // The folder new surfaces go into: the selected folder, or the selected surface's folder.
  currentFolder() { const ed = this.ed; if (ed.selFolder && ed.show.folders.has(ed.selFolder)) return ed.selFolder; return ed.selected()?.folder || 0; }

  surfaces() {
    const ed = this.ed, show = ed.show, b = this.body;
    if (ed.selFolder && !show.folders.has(ed.selFolder)) ed.selFolder = 0;
    b.append(el('div', { class: 'buttons' }, SHAPE_LIST.map(([shape, label]) =>
      el('button', { text: `+ ${label}`, on: { click: () => { const s = ops.addSurface(show, shape, ed.defaultMedia(), this.currentFolder()); ed.select(s.id); ed.commit(); } } }))));
    const sel = ed.selFolder ? null : ed.selected();
    b.append(el('div', { class: 'buttons' },
      el('button', { text: 'Up', disabled: !sel, title: 'Draw it in front of the one above', on: { click: () => { ops.moveSurface(show, sel.id, -1); ed.commit(); this.render(); } } }),
      el('button', { text: 'Down', disabled: !sel, on: { click: () => { ops.moveSurface(show, sel.id, +1); ed.commit(); this.render(); } } }),
      el('button', { text: 'Duplicate', disabled: !sel, on: { click: () => { const c = ed.duplicate(sel); ed.select(c.id); ed.commit(); } } }),
      el('button', { text: 'Delete', class: 'danger', disabled: !sel && !ed.selFolder, title: ed.selFolder ? 'Delete the folder (what is in it moves up a level)' : 'Delete the surface',
        on: { click: () => { if (ed.selFolder) { ops.removeFolder(show, ed.selFolder); ed.selFolder = 0; } else { ops.removeSurface(show, sel.id); ed.select(0); } ed.commit(); this.render(); } } }),
      el('button', { text: '+ Folder', title: 'A folder to organise surfaces: drag surfaces onto it; hiding it hides all it holds',
        on: { click: () => { ed.selFolder = ops.addFolder(show, '', this.currentFolder()); ed.commit(); this.render(); } } })));

    const list = el('div', { class: 'list tree' });
    const tags = { quad: '▭', circle: '◯', triangle: '△', line: '╱', freeform: '✶', object3d: '❒' };
    // drag and drop: surfaces and folders onto folders (or onto the top-level strip)
    const dropOn = (node, folder) => {
      node.addEventListener('dragover', e => { e.preventDefault(); node.classList.add('drop'); });
      node.addEventListener('dragleave', () => node.classList.remove('drop'));
      node.addEventListener('drop', e => {
        e.preventDefault(); e.stopPropagation(); node.classList.remove('drop');
        const [kind, id] = (e.dataTransfer.getData('text/plain') || '').split(':');
        if (kind === 'surface') ops.moveSurfaceToFolder(show, Number(id), folder);
        else if (kind === 'folder' && !ops.moveFolder(show, Number(id), folder)) { ed.status("A folder can't go inside itself."); return; }
        ed.commit(); this.render();
      });
    };
    const drag = (node, what) => { node.draggable = true; node.addEventListener('dragstart', e => e.dataTransfer.setData('text/plain', what)); };
    const addRows = (parent, depth) => {
      for (const f of show.folders.values()) {
        if (f.parent !== parent) continue;
        const dim = !isShownFolder(show, f.parent);
        const row = el('div', { class: 'item folder' + (ed.selFolder === f.id ? ' selected' : '') + (dim ? ' dimmed' : ''), style: { paddingLeft: `${6 + depth * 14}px` }, 'data-folder': f.id,
          on: { click: e => { if (e.target.tagName === 'INPUT') return; if (e.target.classList.contains('twisty')) { f.open = !f.open; ed.commit(); this.render(); return; } ed.selFolder = f.id; ed.sel.surface = 0; this.render(); } } },
          el('span', { class: 'twisty', text: f.open ? '▾' : '▸' }),
          checkbox(f.visible, v => { f.visible = v; ed.commit(); this.render(); }, { title: 'Visible (hides everything inside)' }),
          el('span', { class: 'tag', text: '🗀' }), el('span', { text: f.name }));
        drag(row, `folder:${f.id}`); dropOn(row, f.id);
        list.append(row);
        if (f.open) addRows(f.id, depth + 1);
      }
      for (const s of show.surfaces) {
        if (s.folder !== parent && !(parent === 0 && s.folder && !show.folders.has(s.folder))) continue;
        const dim = !isShownFolder(show, s.folder);
        const row = el('div', { class: 'item' + (sel === s ? ' selected' : '') + (dim ? ' dimmed' : ''), style: { paddingLeft: `${6 + depth * 14}px` }, 'data-surface': s.id,
          on: { click: e => { if (e.target.tagName !== 'INPUT') ed.select(s.id); } } },
          checkbox(s.p.visible >= 0.5, v => { this.setParam(s, 'visible', v ? 1 : 0, true); }, { title: 'Visible' }),
          el('span', { class: 'tag', text: tags[s.shape] }), el('span', { text: s.name }));
        drag(row, `surface:${s.id}`);
        list.append(row);
      }
    };
    addRows(0, 0);
    if (show.folders.size) { const top = el('div', { class: 'item droptop', text: '↥ drop here for the top level' }); dropOn(top, 0); list.append(top); }
    if (!show.surfaces.length) list.append(el('div', { class: 'hint', text: 'No surfaces yet: add one above.' }));
    b.append(list);
    const folder = ed.selFolder && show.folders.get(ed.selFolder);
    if (folder) this.folderInspector(folder);
    else if (sel) this.inspector(sel); else b.append(el('p', { class: 'hint', text: 'Select a surface (here or on the stage) to edit it.' }));
  }

  folderOptions(exclude = 0) {
    const show = this.ed.show, out = [[0, 'Top level']];
    const walk = (parent, depth) => { for (const f of show.folders.values()) if (f.parent === parent && !(exclude && ops.folderWithin(show, f.id, exclude))) { out.push([f.id, `${'  '.repeat(depth)}${f.name}`]); walk(f.id, depth + 1); } };
    walk(0, 1);
    return out;
  }

  folderInspector(f) {
    const ed = this.ed, show = ed.show, b = this.body;
    b.append(el('h3', { text: 'Folder' }));
    b.append(row('Name', textInput(f.name, v => { f.name = v || f.name; ed.commit(); this.render(); })));
    b.append(row('Visible', checkbox(f.visible, v => { f.visible = v; ed.commit(); this.render(); })));
    b.append(row('Inside', select(this.folderOptions(f.id), f.parent, v => { if (!ops.moveFolder(show, f.id, Number(v))) ed.status("A folder can't go inside itself."); ed.commit(); this.render(); })));
    const n = show.surfaces.filter(s => s.folder && ops.folderWithin(show, s.folder, f.id)).length;
    b.append(el('p', { class: 'hint', text: `${n} surface${n === 1 ? '' : 's'} inside. New surfaces are added to the selected folder. Deleting a folder keeps what is in it (it moves up a level).` }));
    b.append(el('div', { class: 'buttons' }, el('button', { text: 'Delete folder', class: 'danger', on: { click: () => { ops.removeFolder(show, f.id); ed.selFolder = 0; ed.commit(); this.render(); } } })));
  }

  setParam(s, key, v, final) {
    const ed = this.ed;
    if (ops.findTrack(ed.show, ops.trackPath(s.id, key))) ops.setKey(ed.show, s.id, key, ed.clock.time, v);    // animated: set a keyframe here
    else { const c = cleanValue(key, v); if (c !== null) s.p[key] = c; }
    final ? ed.commit() : ed.touched();
  }

  paramRow(s, key) {
    const d = PARAMS.get(key), ed = this.ed;
    const animated = () => !!ops.findTrack(ed.show, ops.trackPath(s.id, key));
    const onKey = () => { ops.setKey(ed.show, s.id, key, ed.clock.time, s.p[key]); ed.commit(); };
    if (d.isBool || d.format === 'choice') {
      const ctl = d.isBool ? checkbox(s.p[key] >= 0.5, v => this.setParam(s, key, v ? 1 : 0, true))
        : select(LIGHT_MODEL_NAMES.map((n, i) => [i, n]), Math.round(s.p[key]), v => { this.setParam(s, key, Number(v), true); });
      const keyBtn = el('button', { class: 'key', text: '◆', title: 'Add a keyframe here, at the playhead', on: { click: onKey } });
      const node = row(d.label, ctl, keyBtn);
      this.bindings.push({ refresh: () => { if (d.isBool) ctl.checked = s.p[key] >= 0.5; else if (document.activeElement !== ctl) ctl.value = String(Math.round(s.p[key])); node.classList.toggle('animated', animated()); keyBtn.classList.toggle('on', animated()); } });
      return node;
    }
    const pct = d.format === 'pct';
    const sl = slider({ label: d.label, min: d.min, max: d.max, step: key === 'shininess' ? 1 : pct ? 0.001 : d.format === 'deg' ? 0.1 : (d.max - d.min) / 1000,
      get: () => s.p[key], set: (v, final) => this.setParam(s, key, v, final), format: v => formatValue(d, v),
      typed: pct ? v => v * 100 : null, untyped: pct ? v => v / 100 : null, animated, onKey });
    this.bindings.push(sl);
    return sl.node;
  }
  params(s, group, body) { for (const d of PARAMS.values()) if (d.group === group && d.applies(s)) body.append(this.paramRow(s, d.key)); }

  inspector(s) {
    const ed = this.ed, show = ed.show, b = this.body;
    b.append(el('h3', { text: 'Inspector' }));
    b.append(row('Name', textInput(s.name, v => { s.name = v || s.name; ed.commit(); this.render(); })));
    b.append(row('Shape', select(SHAPE_LIST, s.shape, v => { ops.setShape(show, s, v); ed.commit(); this.render(); })));
    if (show.folders.size) b.append(row('Folder', select(this.folderOptions(), s.folder, v => { ops.moveSurfaceToFolder(show, s.id, Number(v)); ed.commit(); this.render(); })));
    b.append(row('Picture', select(show.mediaList.map(m => [m.id, m.name || `Media ${m.id}`]), s.media, v => { s.media = Number(v); ed.commit(); }),
      el('button', { text: 'Fit', title: 'Show the whole picture again (resets the input region)', on: { click: () => { ops.resetInput(show, s); ed.commit(); } } })));
    if (s.shape === 'line') this.params(s, 'Line', b);
    if (s.shape === 'freeform') b.append(el('p', { class: 'hint', text: `${s.poly.length} points. Drag them on the stage; double-click an edge to add one, right-click a point to delete it.` }),
      el('div', { class: 'buttons' }, el('button', { text: 'Reset outline', on: { click: () => { s.poly = [...Array(6)].map((_, i) => { const a = 2 * Math.PI * (i + 0.5) / 6; return { x: 0.5 + 0.5 * Math.cos(a), y: 0.5 + 0.5 * Math.sin(a) }; }); ed.commit(); this.render(); } } })));
    if (s.shape === 'object3d') {
      b.append(section('3D Object', body => {
        body.append(row('Model', select([['cube', 'Cube'], ['sphere', 'Sphere'], ['cylinder', 'Cylinder'], ['plane', 'Plane'], ['file', s.meshPath ? `File: ${s.meshPath.split('/').pop()}` : 'File (OBJ)']], s.mesh, v => {
          if (v === 'file' && !s.meshPath) { this.render(); return; }
          s.mesh = v; ed.commit();
        }), fileButton('Load OBJ…', '.obj', false, files => ed.addFiles(files, s))));
        this.params(s, '3D Object', body);
      }, { open: true }));
      b.append(section('Lighting', body => {
        body.append(el('p', { class: 'hint', text: 'Each light model is its own shader program. Turn "Lighting" (in 3D Object) up to see the lights.' }));
        for (const g of ['Lighting', 'Material', 'Sun', 'Point light', 'Spotlight']) {
          if (g !== 'Lighting') body.append(el('h4', { text: g }));
          this.params(s, g, body);
        }
      }, { open: true }));
    }
    b.append(section('Appearance', body => {
      this.params(s, 'Appearance', body);
      body.append(row('Blend', select([[0, 'Over'], [1, 'Add'], [2, 'Multiply'], [3, 'Screen']], s.blend, v => { s.blend = Number(v); ed.commit(); })));
      body.append(row('Flip', el('label', { class: 'inline' }, checkbox(s.flipH, v => { s.flipH = v; ed.commit(); }), ' horizontal'),
                               el('label', { class: 'inline' }, checkbox(s.flipV, v => { s.flipV = v; ed.commit(); }), ' vertical')));
    }, { open: true }));
    for (const g of ['Transform', 'Content', 'Color']) b.append(section(g === 'Content' ? 'Picture inside (scroll, zoom, spin)' : g, body => {
      this.params(s, g, body);
      body.append(el('button', { text: 'Reset', on: { click: () => { for (const d of PARAMS.values()) if (d.group === g) this.setParam(s, d.key, d.def, false); ed.commit(); } } }));
    }, { id: g }));
    b.append(section(`Masks (${s.masks.length})`, body => this.masks(s, body), { id: 'masks' }));
  }

  masks(s, body) {
    const ed = this.ed;
    const add = shape => () => { if (s.masks.length >= 6) return; ed.selMask = s.masks.length; s.masks.push({ name: `Mask ${s.masks.length + 1}`, shape, mode: 0, enabled: true, feather: 0, min: { x: 0.3, y: 0.3 }, max: { x: 0.7, y: 0.7 }, points: shape === 2 ? [{ x: 0.5, y: 0.25 }, { x: 0.75, y: 0.7 }, { x: 0.25, y: 0.7 }] : [] }); ed.commit(); this.render(); };
    body.append(el('p', { class: 'hint', text: 'Masks cut parts of the surface away (positions are 0..1 across the surface).' }),
      el('div', { class: 'buttons' }, el('button', { text: '+ Rectangle', on: { click: add(0) } }), el('button', { text: '+ Ellipse', on: { click: add(1) } }), el('button', { text: '+ Polygon', on: { click: add(2) } })),
      el('p', { class: 'hint', text: 'Click a mask to edit it on the stage: drag its corners or points, drag inside it to move it. Polygon: double-click an edge to add a point, right-click a point to delete it.' }));
    s.masks.forEach((m, i) => {
      const box = el('div', { class: 'card mask' + (ed.selMask === i ? ' active' : ''), on: { click: e => { if (e.target.closest('input, select, button')) return; ed.selMask = ed.selMask === i ? -1 : i; this.render(); } } });
      box.append(row(m.name, checkbox(m.enabled, v => { m.enabled = v; ed.commit(); }, { title: 'On' }),
        select([[0, 'Hide inside'], [1, 'Show only inside']], m.mode, v => { m.mode = Number(v); ed.commit(); }),
        el('button', { text: '✕', class: 'danger', title: 'Delete this mask', on: { click: () => { s.masks.splice(i, 1); ed.selMask = -1; ed.commit(); this.render(); } } })));
      if (m.shape === 2) box.append(el('p', { class: 'hint', text: `Polygon, ${m.points.length} points.` }));
      if (m.shape !== 2) for (const [label, pt, k] of [['Left', 'min', 'x'], ['Top', 'min', 'y'], ['Right', 'max', 'x'], ['Bottom', 'max', 'y']]) {
        const sl = slider({ label, min: 0, max: 1, step: 0.001, get: () => m[pt][k], set: (v, final) => { m[pt][k] = v; if (m.min.x > m.max.x - 0.01 && pt === 'min') m.min.x = m.max.x - 0.01; final ? ed.commit() : ed.touched(); }, format: v => `${Math.round(v * 100)}%` });
        this.bindings.push(sl); box.append(sl.node);
      }
      const fe = slider({ label: 'Feather', min: 0, max: 0.5, step: 0.001, get: () => m.feather, set: (v, final) => { m.feather = v; final ? ed.commit() : ed.touched(); }, format: v => `${Math.round(v * 100)}%` });
      this.bindings.push(fe); box.append(fe.node);
      body.append(box);
    });
  }

  // ------------------------------------------------------------------ media
  media() {
    const ed = this.ed, show = ed.show, b = this.body;
    b.append(el('div', { class: 'buttons' },
      select([['', '+ Generator…'], ...GEN_TYPES.map(g => [g.type, g.label])], '', v => { if (v) { const id = this.addGenerator(v); ed.selMedia = id; ed.commit(); this.render(); } }),
      fileButton('Add files…', 'audio/*,video/*,.obj,.mp3,.wav,.mp4,.webm', true, files => ed.addFiles(files))));
    b.append(el('p', { class: 'hint', text: 'Add your song (MP3/WAV), videos (MP4/WebM) and 3D models (OBJ). They are kept in this browser until you download the show.' }));
    const list = el('div', { class: 'list' });
    for (const m of show.mediaList) {
      const kind = m.kind === 'video' ? 'video' : m.type === 'pattern' ? 'test pattern' : genType(m.type)?.label || m.type;
      list.append(el('div', { class: 'item' + (ed.selMedia === m.id ? ' selected' : ''), on: { click: () => { ed.selMedia = m.id; this.render(); } } },
        el('span', { class: 'tag', text: m.kind === 'video' ? '▶' : '✦' }), el('span', { text: m.name || `Media ${m.id}` }), el('span', { class: 'dim', text: ` ${kind}` })));
    }
    b.append(list);
    const m = show.mediaList.find(x => x.id === ed.selMedia);
    if (m) this.mediaInspector(m);
    b.append(el('h3', { text: 'Songs' }));
    if (!show.audioAssets.size) b.append(el('p', { class: 'hint', text: 'No song yet: use "Add files…" above. It goes on the timeline from the start.' }));
    for (const a of show.audioAssets.values()) this.songCard(a);
  }

  addGenerator(type) {
    const ed = this.ed, show = ed.show;
    const id = Math.max(1, ...show.mediaList.map(m => m.id)) + 1;
    const base = genType(type).label;
    let name = base;
    for (let n = 2; show.mediaList.some(m => m.name === name); n++) name = `${base} ${n}`;
    const entry = newGeneratorEntry(id, type, name);
    show.mediaList.push(entry);
    show.media.set(id, createSource(ed.gl, entry, ed.files.resolve, w => ed.status(w)));
    return id;
  }

  mediaInspector(m) {
    const ed = this.ed, show = ed.show, b = this.body, src = show.media.get(m.id);
    const card = el('div', { class: 'card' });
    card.append(row('Name', textInput(m.name || '', v => { m.name = v; ed.commit(); this.render(); })));
    const changed = final => { if (src?.refresh) src.refresh(m); final ? ed.commit() : ed.touched(); };
    const speed = slider({ label: 'Speed', min: 0, max: 4, step: 0.01, title: 'How fast it plays: 0 = frozen, 1 = normal, 2 = twice as fast',
      get: () => m.speed ?? 1, set: (x, final) => { m.speed = Math.round(x * 100) / 100; changed(final); }, format: x => `${(+x).toFixed(2)}×` });
    this.bindings.push(speed); card.append(speed.node);
    const g = genType(m.type);
    if (m.kind === 'generator' && g) {
      for (const p of g.params) {
        const v = m.params[p.key] ?? p.def;
        if (p.kind === 'float' || p.kind === 'int') {
          const sl = slider({ label: p.label, min: p.min, max: p.max, step: p.kind === 'int' ? 1 : (p.max - p.min) / 1000, get: () => m.params[p.key] ?? p.def,
            set: (x, final) => { m.params[p.key] = p.kind === 'int' ? Math.round(x) : x; changed(final); }, format: x => (p.kind === 'int' ? String(Math.round(x)) : (+x).toFixed(2)) });
          this.bindings.push(sl); card.append(sl.node);
        } else if (p.kind === 'bool') card.append(row(p.label, checkbox(v, x => { m.params[p.key] = x; changed(true); })));
        else if (p.kind === 'color') {
          const c = el('input', { type: 'color', value: toHex(v) });
          c.addEventListener('input', () => { m.params[p.key] = fromHex(c.value, (m.params[p.key] || [0, 0, 0, 1])[3]); changed(false); });
          c.addEventListener('change', () => changed(true));
          card.append(row(p.label, c));
        } else if (p.kind === 'choice') card.append(row(p.label, select(p.choices.map(c => [c, c[0].toUpperCase() + c.slice(1)]), v, x => { m.params[p.key] = x; changed(true); })));
        else if (p.kind === 'text') {
          const t = el('textarea', { rows: 2, value: v });
          t.addEventListener('input', () => { m.params[p.key] = t.value; changed(false); });
          t.addEventListener('change', () => changed(true));
          t.addEventListener('keydown', e => e.stopPropagation());
          card.append(row(p.label, t));
        }
      }
      const w = el('input', { type: 'number', value: m.width || 1920, min: 16, max: 8192, class: 'num' }), h = el('input', { type: 'number', value: m.height || 1080, min: 16, max: 8192, class: 'num' });
      card.append(row('Size (px)', w, '×', h, el('button', { text: 'Apply', on: { click: () => { m.width = +w.value; m.height = +h.value; changed(true); } } })));
      card.append(row('Playing', checkbox(m.playing !== false, x => { m.playing = x; changed(true); })));
    } else if (m.kind === 'video') {
      card.append(el('p', { class: 'hint', text: m.pathRel || m.path }));
      card.append(row('Loop', checkbox(m.loop !== false, x => { m.loop = x; if (src?.video) src.video.loop = x; ed.commit(); })));
    } else card.append(el('p', { class: 'hint', text: 'The built-in test pattern: handy for lining surfaces up with real objects.' }));
    const sel = ed.selected();
    card.append(el('div', { class: 'buttons' },
      el('button', { text: 'Use on selected surface', disabled: !sel, on: { click: () => { sel.media = m.id; ed.commit(); } } }),
      m.id !== 1 && el('button', { text: 'Remove', class: 'danger', on: { click: () => {
        show.mediaList = show.mediaList.filter(x => x !== m); show.media.delete(m.id);
        for (const s of show.surfaces) if (s.media === m.id) s.media = 1;
        ed.selMedia = 0; ed.commit(); this.render();
      } } })));
    b.append(card);
  }

  songCard(a) {
    const ed = this.ed, show = ed.show;
    const bpm = el('input', { type: 'number', value: a.bpm ? +a.bpm.toFixed(2) : '', min: 20, max: 300, step: 0.01, class: 'num', placeholder: '?' });
    bpm.addEventListener('change', () => { a.bpm = Math.max(0, +bpm.value || 0); ed.commit(); });
    const off = el('input', { type: 'number', value: +a.beatOffset.toFixed(3), min: 0, step: 0.001, class: 'num' });
    off.addEventListener('change', () => { a.beatOffset = Math.max(0, +off.value || 0); ed.commit(); });
    let taps = [];
    const tap = el('button', { text: 'Tap', title: 'Tap along with the beat a few times', on: { click: () => {
      const now = performance.now() / 1000;
      taps = taps.filter(t => now - t < 3).concat(now);
      if (taps.length >= 4) { const span = (taps[taps.length - 1] - taps[0]) / (taps.length - 1); a.bpm = 60 / span; bpm.value = a.bpm.toFixed(2); ed.commit(); }
    } } });
    const detect = el('button', { text: 'Detect', title: 'Find the tempo and the first beat automatically', on: { click: async () => {
      const r = await ed.detectTempo(a.id);
      if (r) { bpm.value = a.bpm.toFixed(2); off.value = a.beatOffset.toFixed(3); } else ed.status('Press Play once first, so the song is loaded.');
    } } });
    this.body.append(el('div', { class: 'card' },
      el('div', { class: 'song', text: `♪ ${a.name}` }),
      row('Tempo (BPM)', bpm, tap, detect),
      row('First beat (s)', off),
      el('div', { class: 'buttons' }, el('button', { text: 'Remove song', class: 'danger', on: { click: () => {
        show.audioAssets.delete(a.id);
        for (const t of show.timeline.tracks) if (t.kind === 'audio') t.audioClips = t.audioClips.filter(c => c.asset !== a.id);
        show.timeline.tracks = show.timeline.tracks.filter(t => t.kind !== 'audio' || t.audioClips.length);
        ed.commit(); this.render();
      } } }))));
  }

  // ------------------------------------------------------------------ presets
  presets() {
    const ed = this.ed, show = ed.show, b = this.body;
    b.append(row('Live mode', checkbox(ed.live, v => { ed.setLive(v); })));
    b.append(el('p', { class: 'hint', text: 'In live mode the keys 1–9 and 0 play these presets (Shift + key stores the current look). Presets play on the published page too.' }));
    for (let slot = 0; slot < 10; slot++) {
      const key = slot === 9 ? 0 : slot + 1, pr = show.presets[slot];
      const card = el('div', { class: 'card preset' + (ed.player.active === slot ? ' active' : '') });
      card.append(el('div', { class: 'presethead' }, el('span', { class: 'keycap', text: String(key) }),
        pr ? textInput(pr.name, v => { pr.name = v; ed.commit(); }) : el('span', { class: 'dim', text: '(empty)' })));
      const btns = el('div', { class: 'buttons' }, el('button', { text: pr ? 'Store again' : 'Store current look', on: { click: () => { ed.storePreset(slot); this.render(); } } }));
      if (pr) {
        const fade = el('input', { type: 'number', min: 0, max: 30, step: 0.1, value: pr.fade, class: 'num', title: 'Fade time (s); 0 = cut' });
        fade.addEventListener('change', () => { pr.fade = Math.max(0, Math.min(30, +fade.value || 0)); ed.commit(); });
        btns.append(el('span', { class: 'dim', text: 'fade' }), fade, el('button', { text: 'Play', on: { click: () => ed.playPreset(slot) } }),
          el('button', { text: 'Clear', class: 'danger', on: { click: () => { show.presets[slot] = null; ed.commit(); this.render(); } } }));
      }
      card.append(btns);
      b.append(card);
    }
  }
}

export { capturePreset, AUDIO_EXT, VIDEO_EXT, OBJ_EXT, uniquePath };
