// Drives the web editor like a person would and checks the result.
import { squareToQuad, mapPoint } from '../js/math.js';
import * as ops from '../js/editor-ops.js';
const lines = [];
let passed = 0, failed = 0;
const expect = (what, ok) => { lines.push(`${ok ? 'PASS' : 'FAIL'}  ${what}`); ok ? passed++ : failed++; document.getElementById('out').textContent = lines.join('\n') + '\n...'; };
const near = (a, b, tol) => Math.abs(a - b) <= tol;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const frames = n => new Promise(r => { const f = k => (k ? requestAnimationFrame(() => f(k - 1)) : r()); f(n); });

const frame = document.getElementById('ed');
for (let i = 0; i < 200 && !(frame.contentWindow?.__editor?.show && frame.contentDocument.querySelector('#panel .item')); i++) await sleep(50);   // (the frame may have loaded already)
const win = frame.contentWindow, doc = frame.contentDocument;
const ed = win.__editor;
document.getElementById('out').textContent = `editor found: ${!!ed}, show: ${!!ed?.show}\n...`;

// ---- helpers: real DOM events on the editor's own elements
const button = text => [...doc.querySelectorAll('button')].find(b => b.textContent.trim() === text);
const click = node => node.dispatchEvent(new win.MouseEvent('click', { bubbles: true }));
function pointer(target, type, x, y, extra = {}) {
  const b = target.getBoundingClientRect();
  target.dispatchEvent(new win.PointerEvent(type, { bubbles: true, clientX: b.left + x, clientY: b.top + y, button: 0, buttons: type === 'pointerup' ? 0 : 1, pointerId: 1, ...extra }));
}
async function drag(target, from, to) {
  pointer(target, 'pointerdown', from.x, from.y);
  for (let i = 1; i <= 4; i++) pointer(target, 'pointermove', from.x + (to.x - from.x) * i / 4, from.y + (to.y - from.y) * i / 4);
  pointer(target, 'pointerup', to.x, to.y);
  await frames(2);
}
const overlay = doc.getElementById('overlay');
const onStage = p => { const r = ed.stageView.rect; return { x: r.x + p.x * r.w, y: r.y + p.y * r.h }; };
function stagePixel(fx, fy) {
  const gl = ed.gl, d = new Uint8Array(4), rt = ed.stageRT;
  gl.bindFramebuffer(gl.FRAMEBUFFER, rt.fbo);
  gl.readPixels(Math.floor(fx * rt.w), rt.h - 1 - Math.floor(fy * rt.h), 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, d);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  return { r: d[0], g: d[1], b: d[2] };
}

try {
  expect('the editor starts, with the published show open', !!ed && ed.show.surfaces.length === 3 && doc.querySelectorAll('#panel .item').length === 3);

  // a clean show for the rest
  ed.newShow(); await frames(3);
  expect('New starts a show with one quad, selected', ed.show.surfaces.length === 1 && ed.selected()?.shape === 'quad');

  click(button('Circle')); await frames(2);
  expect('+ Circle adds a circle and selects it', ed.show.surfaces.length === 2 && ed.selected().shape === 'circle' && ed.selected().name === 'Circle-1');

  // a solid generator on it, so its pixels can be checked
  const sel = doc.querySelector('.tabs button[data-tab="media"]'); click(sel); await frames(2);
  const genMenu = [...doc.querySelectorAll('#panel select')].find(s => s.options[0].text.startsWith('+ Generator'));
  genMenu.value = 'solid'; genMenu.dispatchEvent(new win.Event('change')); await frames(2);
  const solidId = ed.show.mediaList[ed.show.mediaList.length - 1].id;
  expect('+ Generator > Solid Colour adds one to the media', ed.show.mediaList.some(m => m.type === 'solid') && ed.selMedia === solidId);
  click(button('Use on selected surface')); await frames(3);
  const circle = ed.selected();
  expect('Use on selected surface puts it on the circle', circle.media === solidId);
  circle.out = [{ x: 0.3, y: 0.3 }, { x: 0.7, y: 0.3 }, { x: 0.7, y: 0.7 }, { x: 0.3, y: 0.7 }];
  ed.show.surfaces.find(s => s.name === 'Quad-1').p.visible = 0;          // (nothing behind the circle, so its pixels are its own)
  ed.commit(); await frames(3);
  const c = stagePixel(0.5, 0.5);
  expect('the stage shows the circle in its colour (orange), and not outside it', c.r > 240 && near(c.g, 128, 6) && c.b < 10 && stagePixel(0.31, 0.31).r < 240);

  // ---- warping with the mouse
  await drag(overlay, onStage(circle.out[2]), onStage({ x: 0.85, y: 0.9 }));
  expect('dragging a corner on the stage warps the surface', near(circle.out[2].x, 0.85, 0.01) && near(circle.out[2].y, 0.9, 0.01));
  await drag(overlay, onStage({ x: 0.5, y: 0.5 }), onStage({ x: 0.45, y: 0.5 }));
  expect('dragging inside moves the whole surface', near(circle.out[0].x, 0.25, 0.01) && near(circle.out[2].x, 0.80, 0.01));
  await drag(overlay, onStage(circle.out[0]), onStage({ x: 0.95, y: 0.95 }));
  { const q = ed.selected().out, cross = (a, b, c) => (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
    const signs = [0, 1, 2, 3].map(i => Math.sign(cross(q[i], q[(i + 1) % 4], q[(i + 2) % 4])));
    expect('a corner dragged towards a twist stops where the quad would twist (it stays convex)', signs.every(x => x === signs[0]) && !near(q[0].x, 0.95, 0.05)); }
  expect('...each finished drag is one undo step', ed.undo.length >= 3);
  click(doc.getElementById('undo')); await frames(2);
  expect('Undo puts the last drag back', near(ed.selected().out[0].x, 0.25, 0.01) && near(ed.selected().out[0].y, 0.3, 0.01));
  click(doc.getElementById('undo')); await frames(2);
  expect('...and the one before it', near(ed.selected().out[0].x, 0.3, 0.01) && near(ed.selected().out[2].x, 0.85, 0.01));
  click(doc.getElementById('redo')); await frames(2);
  expect('Redo does it again', near(ed.selected().out[0].x, 0.25, 0.01));

  // ---- keyframes from the inspector
  click(doc.querySelector('.tabs button[data-tab="surfaces"]')); await frames(2);
  const opacityRow = [...doc.querySelectorAll('#panel .row.slider')].find(r => r.querySelector('label').textContent === 'Opacity');
  ed.seek(0); await frames(1);
  click(opacityRow.querySelector('button.key')); await frames(1);
  ed.seek(2); await frames(1);
  const range = opacityRow.querySelector('input[type=range]');
  range.value = '0'; range.dispatchEvent(new win.Event('input')); range.dispatchEvent(new win.Event('change')); await frames(2);
  const track = ed.show.timeline.tracks.find(t => t.path === `surface/${ed.selected().id}/opacity`);
  expect('◆ makes a keyframe, and changing an animated setting adds one at the playhead', track && track.clips[0].keys.length === 2 && track.clips[0].keys[1].v === 0);
  expect('...and the row is marked as animated', opacityRow.classList.contains('animated'));
  ed.seek(1); await frames(3);
  { const o = ed.selected().p.opacity, px = stagePixel(0.5, 0.5);
    expect(`halfway between the keys the surface is half faded (opacity ${o.toFixed(3)}, pixel red ${px.r})`, near(o, 0.5, 0.01) && near(px.r, 128, 8)); }

  // ---- the timeline
  const tl = doc.getElementById('timeline'), tv = ed.timelineView;
  const row = tv.rows().find(r => r.track === track);
  const ky = () => tv.keyY(row, { key: track.clips[0].keys[1] });
  expect('a parameter row shows its curve, with the keys on it', !!tv.curve(row) && near(ky(), row.y + row.h - 8, 0.5));
  await drag(tl, { x: tv.xOf(2), y: ky() }, { x: tv.xOf(3), y: ky() });
  expect('dragging a keyframe sideways changes its time, not its value', near(track.clips[0].keys[1].t, 3, 0.05) && track.clips[0].keys[1].v === 0);
  { const per = tv.curve(row).perPx, y0 = ky();
    await drag(tl, { x: tv.xOf(3), y: y0 }, { x: tv.xOf(3), y: y0 - 0.6 / per });
    expect('dragging it up raises its value (opacity 0% -> 60%)', near(track.clips[0].keys[1].v, 0.6, 0.02) && near(track.clips[0].keys[1].t, 3, 0.05));
    await drag(tl, { x: tv.xOf(3), y: ky() }, { x: tv.xOf(3), y: ky() + 5 / per });
    expect('...and it stays within the setting\'s range', track.clips[0].keys[1].v === 0);
    pointer(tl, 'pointerdown', tv.xOf(3), ky()); pointer(tl, 'pointermove', tv.xOf(3.5), ky() - 0.3 / per, { shiftKey: true }); pointer(tl, 'pointerup', 0, 0); await frames(1);
    expect('with Shift, only the value changes', near(track.clips[0].keys[1].t, 3, 0.05) && near(track.clips[0].keys[1].v, 0.3, 0.02));
    track.clips[0].keys[1].v = 0; ed.commit(); }
  pointer(tl, 'pointerdown', tv.xOf(4), 10); pointer(tl, 'pointerup', tv.xOf(4), 10); await frames(1);
  expect('clicking the ruler moves the playhead', near(ed.clock.time, 4, 0.05));
  doc.body.dispatchEvent(new win.KeyboardEvent('keydown', { key: 'm', code: 'KeyM', bubbles: true })); await frames(1);
  expect('M drops a marker at the playhead', ed.show.timeline.markers.some(m => near(m.time, 4, 0.05)));
  ed.sel.key = { track, clip: track.clips[0], index: 1 };
  doc.body.dispatchEvent(new win.KeyboardEvent('keydown', { key: 'Delete', code: 'Delete', bubbles: true })); await frames(1);
  expect('Delete removes the selected keyframe', track.clips[0].keys.length === 1);

  // ---- song clips: move, trim, split, delete
  { const songId = (await import('../js/editor-ops.js')).addSong(ed.show, 'beat.wav', 'music/beat.wav', 8);
    ed.commit(); await frames(2);
    const atrack = ed.show.timeline.tracks.find(t => t.kind === 'audio'), clip = atrack.audioClips.find(c => c.asset === songId);
    const arow = () => tv.rows().find(r => r.track === atrack), y = () => arow().y + arow().h / 2;
    ed.snap = false;
    await drag(tl, { x: tv.xOf(clip.start + 4), y: y() }, { x: tv.xOf(clip.start + 5), y: y() });
    expect('dragging a song clip moves it along the timeline', near(clip.start, 1, 0.05) && clip.offset === 0);
    await drag(tl, { x: tv.xOf(clip.start) + 2, y: y() }, { x: tv.xOf(2), y: y() });
    expect('dragging its left end trims the start of the song', near(clip.start, 2, 0.05) && near(clip.offset, 1, 0.05) && near(clip.duration, 7, 0.05));
    await drag(tl, { x: tv.xOf(clip.start + clip.duration) - 2, y: y() }, { x: tv.xOf(6), y: y() });
    expect('dragging its right end trims the end', near(clip.duration, 4, 0.05));
    ed.seek(4); doc.body.dispatchEvent(new win.KeyboardEvent('keydown', { key: 's', code: 'KeyS', bubbles: true })); await frames(1);
    expect('S splits it at the playhead', atrack.audioClips.length === 2 && near(clip.duration, 2, 0.05) && near(atrack.audioClips[1].start, 4, 0.05) && near(atrack.audioClips[1].offset, 3, 0.05));
    doc.body.dispatchEvent(new win.KeyboardEvent('keydown', { key: 'Delete', code: 'Delete', bubbles: true })); await frames(1);
    expect('Delete removes the selected song clip (the second part)', atrack.audioClips.length === 1 && atrack.audioClips[0] === clip);
    ed.snap = true; }

  // ---- presets and live mode
  ed.selected().p.hue = 0; ed.commit();
  click(doc.querySelector('.tabs button[data-tab="presets"]')); await frames(1);
  click(button('Store current look')); await frames(1);
  expect('a preset stores the current look on key 1', !!ed.show.presets[0] && ed.show.presets[0].surfaces.length === 2);
  ed.show.presets[0].fade = 0; ed.selected().p.hue = 90;
  click(doc.getElementById('live')); await frames(1);
  doc.body.dispatchEvent(new win.KeyboardEvent('keydown', { key: '1', code: 'Digit1', bubbles: true })); await frames(2);
  expect('in Live mode, key 1 plays it', ed.live && near(ed.selected().p.hue, 0, 1e-6));

  // ---- folders
  click(doc.querySelector('.tabs button[data-tab="surfaces"]')); await frames(1);
  const circ = ed.show.surfaces.find(s => s.shape === 'circle');
  ed.select(circ.id); await frames(1);
  click(button('Folder')); await frames(1);
  const fid = ed.selFolder;
  expect('+ Folder makes a folder, listed and selected', fid > 0 && ed.show.folders.has(fid) && !!doc.querySelector(`#panel [data-folder="${fid}"]`));
  { const src = doc.querySelector(`#panel [data-surface="${circ.id}"]`), dst = doc.querySelector(`#panel [data-folder="${fid}"]`), dt = new win.DataTransfer();
    src.dispatchEvent(new win.DragEvent('dragstart', { bubbles: true, dataTransfer: dt }));
    dst.dispatchEvent(new win.DragEvent('dragover', { bubbles: true, cancelable: true, dataTransfer: dt }));
    dst.dispatchEvent(new win.DragEvent('drop', { bubbles: true, cancelable: true, dataTransfer: dt })); await frames(2); }
  expect('dragging a surface onto a folder puts it inside', circ.folder === fid);
  doc.querySelector(`#panel [data-folder="${fid}"] input[type=checkbox]`).click(); await frames(3);
  expect('unticking the folder hides what is inside it on the stage', !ed.show.folders.get(fid).visible && stagePixel(0.5, 0.5).r < 20);
  doc.querySelector(`#panel [data-folder="${fid}"] input[type=checkbox]`).click(); await frames(3);
  expect('...and ticking it brings it back', stagePixel(0.5, 0.5).r > 100);
  click(button('Delete folder')); await frames(1);
  expect('deleting the folder keeps the surface (it moves up a level)', !ed.show.folders.has(fid) && circ.folder === 0 && ed.show.surfaces.includes(circ));

  // ---- masks, dragged on the stage
  ed.select(circ.id); await frames(1);
  const masksSec = [...doc.querySelectorAll('#panel details')].find(d => d.querySelector('summary').textContent.startsWith('Masks'));
  masksSec.open = true; await frames(1);
  click([...masksSec.querySelectorAll('button')].find(b => b.textContent === 'Rectangle')); await frames(3);
  const mk = circ.masks[0];
  expect('+ Rectangle adds a mask, selected for editing, and it cuts a hole', ed.selMask === 0 && !!mk && stagePixel(0.5, 0.5).r < 20);
  const unitToStage = u => mapPoint(squareToQuad(circ.out), u);          // through the surface's real (perspective) warp
  await drag(overlay, onStage(unitToStage(mk.max)), onStage(unitToStage({ x: 0.9, y: 0.9 })));
  expect('dragging a mask corner on the stage resizes it', near(mk.max.x, 0.9, 0.02) && near(mk.max.y, 0.9, 0.02) && near(mk.min.x, 0.3, 1e-6));
  await drag(overlay, onStage(unitToStage({ x: 0.5, y: 0.5 })), onStage(unitToStage({ x: 0.45, y: 0.5 })));
  expect('dragging inside it moves it', near(mk.min.x, 0.25, 0.02) && near(mk.max.x, 0.85, 0.02));
  click([...masksSec.querySelectorAll('button')].find(b => b.textContent === 'Polygon')); await frames(2);
  const pm = circ.masks[1];
  const mid = onStage(unitToStage({ x: 0.5, y: 0.7 })), b2 = overlay.getBoundingClientRect();
  overlay.dispatchEvent(new win.MouseEvent('dblclick', { bubbles: true, clientX: b2.left + mid.x, clientY: b2.top + mid.y })); await frames(1);
  expect('double-clicking a polygon mask edge adds a point', pm.points.length === 4);
  const p0 = onStage(unitToStage(pm.points[0]));
  overlay.dispatchEvent(new win.MouseEvent('contextmenu', { bubbles: true, cancelable: true, clientX: b2.left + p0.x, clientY: b2.top + p0.y })); await frames(1);
  expect('right-clicking one of its points deletes it', pm.points.length === 3);

  // ---- delete, copy and paste, Shift-drag, deselecting
  const press = (key, code, extra = {}, target = doc.body) => target.dispatchEvent(new win.KeyboardEvent('keydown', { key, code, bubbles: true, cancelable: true, ...extra }));
  ed.newShow(); await frames(2);
  { const q = ed.selected(), id = q.id;
    q.out = [{ x: 0.2, y: 0.2 }, { x: 0.4, y: 0.25 }, { x: 0.38, y: 0.5 }, { x: 0.22, y: 0.45 }]; ed.commit(); await frames(2);
    const size = s => Math.hypot((s.out[1].x - s.out[0].x) * 16, (s.out[1].y - s.out[0].y) * 9), ratio = s => size(s) / Math.hypot((s.out[3].x - s.out[0].x) * 16, (s.out[3].y - s.out[0].y) * 9);
    const before = JSON.parse(JSON.stringify(q)), to = { x: 0.6, y: 0.75 };
    pointer(overlay, 'pointerdown', onStage(q.out[2]).x, onStage(q.out[2]).y);
    for (const t of [0.5, 1]) { const m = onStage({ x: 0.38 + (to.x - 0.38) * t, y: 0.5 + (to.y - 0.5) * t }); pointer(overlay, 'pointermove', m.x, m.y, { shiftKey: true }); }
    pointer(overlay, 'pointerup', 0, 0); await frames(2);
    expect('Shift-dragging a corner makes the whole shape bigger, in proportion', size(q) > size(before) * 1.5 && near(ratio(q), ratio(before), 1e-3));
    expect('...and the opposite corner stays where it was', near(q.out[0].x, 0.2, 1e-6) && near(q.out[0].y, 0.2, 1e-6));
    pointer(overlay, 'pointerdown', onStage(q.out[2]).x, onStage(q.out[2]).y); { const m = onStage({ x: 0, y: 0 }); pointer(overlay, 'pointermove', m.x, m.y, { shiftKey: true }); } pointer(overlay, 'pointerup', 0, 0); await frames(2);
    expect('...and it never collapses or turns inside out', q.out[2].x > q.out[0].x && q.out[2].y > q.out[0].y);
    q.out = before.out; ed.commit(); await frames(2);

    press('c', 'KeyC', { ctrlKey: true }); press('v', 'KeyV', { ctrlKey: true }); await frames(2);
    const c1 = ed.selected();
    expect('Ctrl+C, Ctrl+V pastes a copy and selects it', ed.show.surfaces.length === 2 && c1.id !== id && c1.name === 'Quad-1 copy' && near(c1.out[0].x, 0.23, 1e-6) && c1.out[0] !== q.out[0]);
    press('v', 'KeyV', { ctrlKey: true }); await frames(2);
    expect('pasting again makes another, a step further', ed.show.surfaces.length === 3 && ed.selected().name === 'Quad-1 copy 2' && near(ed.selected().out[0].x, 0.26, 1e-6));
    press('Backspace', 'Backspace'); await frames(2);
    expect('Backspace deletes the selected surface', ed.show.surfaces.length === 2 && !ed.selected());
    press('z', 'KeyZ', { ctrlKey: true }); await frames(2);
    expect('undo brings it back, selected', ed.show.surfaces.length === 3 && ed.selected()?.name === 'Quad-1 copy 2');
    press('Delete', 'Delete'); await frames(2);
    expect('Delete does too', ed.show.surfaces.length === 2);

    ed.select(id); await frames(2);
    const name = [...doc.querySelectorAll('#panel input[type=text]')][0];
    press('Backspace', 'Backspace', {}, name); press('v', 'KeyV', { ctrlKey: true }, name); await frames(1);
    expect('typing in a box never deletes or pastes surfaces', ed.show.surfaces.length === 2);
    press('Delete', 'Delete', {}, doc.querySelector('#panel input[type=range]')); await frames(2);
    expect('...but Delete works after using a slider', ed.show.surfaces.length === 1);
    press('z', 'KeyZ', { ctrlKey: true }); await frames(2);

    ed.select(id); click(doc.querySelector('.tabs button[data-tab="media"]')); await frames(2);
    { const m = onStage({ x: 0.9, y: 0.9 }); pointer(overlay, 'pointerdown', m.x, m.y); pointer(overlay, 'pointerup', m.x, m.y); } await frames(2);
    expect('clicking an empty part of the stage deselects, staying on the same tab', !ed.selected() && ed.panels.tab === 'media');
    { const m = onStage({ x: 0.3, y: 0.35 }); pointer(overlay, 'pointerdown', m.x, m.y); pointer(overlay, 'pointerup', m.x, m.y); } await frames(2);
    expect('clicking a surface selects it again', !!ed.selected());

    const s = ed.selected();
    s.masks.push({ name: 'Mask 1', shape: 0, mode: 0, enabled: true, feather: 0, min: { x: 0.3, y: 0.3 }, max: { x: 0.7, y: 0.7 }, points: [] }); ed.selMask = 0; ed.commit();
    press('Delete', 'Delete'); await frames(1);
    expect('while a mask is being edited, Delete deletes the mask, not the surface', ed.selected() === s && s.masks.length === 0);
    ops.setKey(ed.show, s.id, 'opacity', 1, 0.5); ed.commit();
    const tr = ed.show.timeline.tracks.find(t => t.kind === 'param'); ops.setKey(ed.show, s.id, 'opacity', 2, 1); ed.selectKey({ track: tr, clip: tr.clips[0], index: 0 });
    press('Delete', 'Delete'); await frames(1);
    expect('with a keyframe selected, Delete deletes the keyframe, not the surface', ed.selected() === s && tr.clips[0].keys.length === 1);

    ops.addSurface(ed.show, 'line', 1); const ln = ed.show.surfaces[0]; ed.select(ln.id); const cp = ed.duplicate(ln);
    expect('a duplicated line is moved aside too', near(cp.lineA.x, ln.lineA.x + 0.03, 1e-6));
    }

  // ---- typing a value, then clicking elsewhere (this used to throw "The node to be removed is no longer a child")
  { ed.newShow(); ops.addSurface(ed.show, 'circle', 1); ed.commit(); await frames(2);
    const s1 = ed.selected(), other = ed.show.surfaces.find(x => x !== s1).id;
    const value = [...doc.querySelectorAll('#panel .row.slider')].find(r => r.querySelector('label').textContent === 'Opacity').querySelector('.value');
    value.click(); await frames(1);
    const box = doc.querySelector('#panel input.typed'); box.value = '40';
    let thrown = null;
    try { ed.select(other); } catch (err) { thrown = err; }
    await frames(2);
    expect('clicking elsewhere while typing a value keeps it, without an error', !thrown && near(s1.p.opacity, 0.4, 1e-6) && ed.selected().id === other);
    win.mhShowError('test message'); const st = doc.getElementById('status');
    expect('an error message has a close button', !!st.querySelector('button') && st.classList.contains('show'));
    click(st.querySelector('button'));
    expect('...that closes it', !st.classList.contains('show') && !st.dataset.error);
    delete doc.documentElement.dataset.error; }

  // ---- resizing the panels
  { const main = doc.querySelector('main'), panel = doc.getElementById('panel'), w0 = panel.getBoundingClientRect().width;
    const bar = doc.getElementById('splitV'), bb = bar.getBoundingClientRect();
    const pd = (t, type, x, y) => t.dispatchEvent(new win.PointerEvent(type, { bubbles: true, clientX: x, clientY: y, button: 0, buttons: type === 'pointerup' ? 0 : 1, pointerId: 7 }));
    pd(bar, 'pointerdown', bb.left + 4, 300); pd(bar, 'pointermove', bb.left + 104, 300); pd(bar, 'pointerup', bb.left + 104, 300); await frames(2);
    expect('dragging the border beside the side panel makes it wider', near(panel.getBoundingClientRect().width, w0 + 100, 2));
    const tlw = doc.getElementById('timelinewrap'), h0 = tlw.getBoundingClientRect().height, hb = doc.getElementById('splitH'), hr = hb.getBoundingClientRect();
    pd(hb, 'pointerdown', 900, hr.top + 4); pd(hb, 'pointermove', 900, hr.top - 76); pd(hb, 'pointerup', 900, hr.top - 76); await frames(2);
    expect('dragging the border above the timeline makes it taller', near(tlw.getBoundingClientRect().height, h0 + 80, 2));
    expect('...and the stage fits in what is left', ed.stageView.rect.y + ed.stageView.rect.h <= doc.getElementById('stagewrap').clientHeight);
    expect('the sizes are remembered', JSON.parse(win.localStorage.getItem('maphappy-editor-layout')).panel === Math.round(w0 + 100));
    pd(bar, 'pointerdown', 0, 0); pd(bar, 'pointerup', 0, 0);
    bar.dispatchEvent(new win.MouseEvent('dblclick', { bubbles: true })); hb.dispatchEvent(new win.MouseEvent('dblclick', { bubbles: true })); await frames(2);
    expect('double-clicking a border puts it back', near(panel.getBoundingClientRect().width, 340, 2) && near(tlw.getBoundingClientRect().height, 260, 2)); }

  // ---- zooming and moving around the stage
  { ed.select(ed.show.surfaces[0].id); await frames(2);
    const q = ed.selected(); q.out = [{ x: 0.4, y: 0.4 }, { x: 0.6, y: 0.4 }, { x: 0.6, y: 0.6 }, { x: 0.4, y: 0.6 }]; ed.commit(); await frames(2);
    const r0 = { ...ed.stageView.rect }, at = onStage({ x: 0.4, y: 0.4 }), b = overlay.getBoundingClientRect();
    overlay.dispatchEvent(new win.WheelEvent('wheel', { bubbles: true, cancelable: true, clientX: b.left + at.x, clientY: b.top + at.y, deltaY: -300 })); await frames(2);
    const r1 = ed.stageView.rect, at1 = onStage({ x: 0.4, y: 0.4 });
    expect('scrolling on the stage zooms in, keeping the point under the mouse in place', r1.w > r0.w * 1.5 && near(at1.x, at.x, 0.5) && near(at1.y, at.y, 0.5));
    expect('...and the zoom shows on the button', doc.getElementById('zoomFit').textContent === `${Math.round(ed.stageView.view().zoom * 100)}%`);
    await drag(overlay, onStage(q.out[2]), onStage({ x: 0.65, y: 0.62 }));
    expect('corners are still dragged exactly when zoomed in', near(q.out[2].x, 0.65, 0.002) && near(q.out[2].y, 0.62, 0.002));
    const before = { ...ed.stageView.rect }, empty = onStage({ x: 0.97, y: 0.97 });
    await drag(overlay, empty, { x: empty.x + 60, y: empty.y + 25 });
    expect('dragging an empty part of the stage moves the view, and keeps the selection', near(ed.stageView.rect.x, before.x + 60, 1) && near(ed.stageView.rect.y, before.y + 25, 1) && ed.selected() === q);
    pointer(overlay, 'pointerdown', 20, 20, { button: 1, buttons: 4 }); pointer(overlay, 'pointermove', 50, 20, { button: 1, buttons: 4 }); pointer(overlay, 'pointerup', 50, 20, { button: 1 }); await frames(1);
    expect('so does dragging with the middle button', near(ed.stageView.rect.x, before.x + 90, 1));
    click(doc.getElementById('zoomFit')); await frames(1);
    expect('the % button fits the whole picture again', near(ed.stageView.rect.x, r0.x, 0.5) && near(ed.stageView.rect.w, r0.w, 0.5));
    click(doc.getElementById('zoomIn')); click(doc.getElementById('zoomOut')); await frames(1);
    expect('+ and - zoom in and out', near(ed.stageView.rect.w, r0.w, 0.5));
    const e2 = onStage({ x: 0.1, y: 0.9 }); pointer(overlay, 'pointerdown', e2.x, e2.y); pointer(overlay, 'pointerup', e2.x, e2.y); await frames(1);
    expect('a click (not a drag) on an empty part still deselects', !ed.selected()); }

  // ---- taller timeline rows, and the scroll bars
  { ed.newShow(); const sh = ed.show, s1 = ed.show.surfaces[0];
    for (const [i, key] of ['opacity', 'posX', 'posY', 'rotation', 'hue', 'scale', 'saturation', 'contrast', 'brightness', 'feather', 'tintR', 'tintG'].entries()) { ops.setKey(sh, s1.id, key, 0, 0.2); ops.setKey(sh, s1.id, key, 30 + i, 0.8); }
    ops.addSong(sh, 'beat.wav', 'music/beat.wav', 60); ed.commit(); await frames(2);
    const tl = doc.getElementById('timeline'), tv = ed.timelineView, atrack = sh.timeline.tracks.find(t => t.kind === 'audio');
    tv.heights.clear(); tv.vy = 0; tv.scroll = 0; tv.pps = 60; await frames(1);
    const ar = () => tv.rows().find(r => r.track === atrack), h0 = ar().h;
    await drag(tl, { x: 400, y: ar().y + ar().h - 1 }, { x: 400, y: ar().y + ar().h + 59 });
    expect('dragging the song row\'s bottom edge makes it taller', near(ar().h, h0 + 60, 1));
    const op = () => tv.rows().find(r => r.track.path?.endsWith('/opacity'));
    await drag(tl, { x: 400, y: op().y + op().h - 1 }, { x: 400, y: op().y + op().h + 40 });
    expect('...and a parameter row too', op().h >= 60);
    tv.scroll = 25; await frames(1);
    const k = op().track.clips[0].keys[1], ky = op().y + op().h - 8 - (op().h - 16) * 0.8;
    pointer(tl, 'pointerdown', tv.xOf(30), ky); pointer(tl, 'pointerup', tv.xOf(30), ky); await frames(1);
    expect('a tall row shows its curve, and its keys sit on it (clicking one there selects it)', ed.sel.key?.clip.keys[ed.sel.key.index] === k);
    expect('the row heights are remembered', !!JSON.parse(win.localStorage.getItem('maphappy-editor-row-heights'))[`audio:${atrack.id}`]);
    tl.dispatchEvent(new win.MouseEvent('dblclick', { bubbles: true, clientX: tl.getBoundingClientRect().left + 400, clientY: tl.getBoundingClientRect().top + ar().y + ar().h - 1 })); await frames(1);
    expect('double-clicking a row\'s edge puts it back to its standard height', ar().h === h0);
    const b = tv.bars(), H = tl.clientHeight, W = tl.clientWidth;
    expect('with more rows than fit, there is a scroll bar on the right', b.v.on);
    pointer(tl, 'pointerdown', W - 6, b.v.y + 5); pointer(tl, 'pointermove', W - 6, H - 10); pointer(tl, 'pointerup', W - 6, H - 10); await frames(1);
    expect('dragging it to the bottom shows the last rows', near(tv.vy, tv.maxVy(), 1) && tv.vy > 0);
    pointer(tl, 'pointerdown', W - 6, 30); pointer(tl, 'pointerup', W - 6, 30); await frames(1);
    expect('clicking near its top goes back to the top', tv.vy === 0);
    const hb = tv.bars().h;
    pointer(tl, 'pointerdown', hb.x + 5, H - 6); pointer(tl, 'pointermove', W - 20, H - 6); pointer(tl, 'pointerup', W - 20, H - 6); await frames(1);
    expect('dragging the bottom scroll bar to the end shows the end of the show', tv.tOf(W - 12) >= 60 && tv.scroll > 0);
    pointer(tl, 'pointerdown', 200, H - 6); pointer(tl, 'pointerup', 200, H - 6); await frames(1);
    expect('...and clicking at its start goes back to the beginning', tv.scroll === 0);
    tl.dispatchEvent(new win.WheelEvent('wheel', { bubbles: true, cancelable: true, clientX: tl.getBoundingClientRect().left + 50, clientY: tl.getBoundingClientRect().top + 100, deltaY: 120 })); await frames(1);
    expect('scrolling over the track names scrolls down', tv.vy > 0);
    tv.heights.clear(); tv.saveHeights(); tv.vy = 0; }

  // ---- several surfaces into a folder at once
  { ed.newShow(); for (const sh of ['circle', 'triangle', 'quad']) ops.addSurface(ed.show, sh, 1);
    const f = ops.addFolder(ed.show, 'F'); ed.commit(); click(doc.querySelector('.tabs button[data-tab="surfaces"]')); await frames(2);
    const ids = ed.show.surfaces.map(s => s.id), rowOf = id => doc.querySelector(`#panel [data-surface="${id}"]`);
    rowOf(ids[0]).dispatchEvent(new win.MouseEvent('click', { bubbles: true }));
    rowOf(ids[2]).dispatchEvent(new win.MouseEvent('click', { bubbles: true, shiftKey: true })); await frames(1);
    expect('Shift-click selects everything between', ed.multi.size === 3 && ed.selected().id === ids[2] && doc.querySelectorAll('#panel .item.multi').length === 2);
    rowOf(ids[1]).dispatchEvent(new win.MouseEvent('click', { bubbles: true, ctrlKey: true })); await frames(1);
    expect('Ctrl-click takes one out again', ed.multi.size === 2 && !ed.multi.has(ids[1]));
    { const dt = new win.DataTransfer(), dst = doc.querySelector(`#panel [data-folder="${f}"]`);
      rowOf(ids[0]).dispatchEvent(new win.DragEvent('dragstart', { bubbles: true, dataTransfer: dt }));
      dst.dispatchEvent(new win.DragEvent('dragover', { bubbles: true, cancelable: true, dataTransfer: dt }));
      dst.dispatchEvent(new win.DragEvent('drop', { bubbles: true, cancelable: true, dataTransfer: dt })); await frames(2); }
    const s = id => ed.show.surfaces.find(x => x.id === id);
    expect('dragging one of them onto a folder puts them all in it', s(ids[0]).folder === f && s(ids[2]).folder === f && s(ids[1]).folder === 0);
    rowOf(ids[1]).dispatchEvent(new win.MouseEvent('click', { bubbles: true })); await frames(1);
    expect('a plain click selects just that one again', ed.multi.size === 1 && ed.selected().id === ids[1] && !doc.querySelector('#panel .item.multi'));
    rowOf(ids[0]).dispatchEvent(new win.MouseEvent('click', { bubbles: true, ctrlKey: true })); await frames(1);
    press('Delete', 'Delete'); await frames(1);
    expect('Delete with several selected deletes them all', ed.show.surfaces.length === 2 && !ed.show.surfaces.some(x => x.id === ids[0] || x.id === ids[1]));
    ed.newShow(); ops.addSurface(ed.show, 'circle', 1); ops.addMarker(ed.show, 1); ed.storePreset(0); ed.commit(); await frames(2); }

  // ---- 3D object and lighting
  click(doc.querySelector('.tabs button[data-tab="surfaces"]')); await frames(1);
  click(button('3D Object')); await frames(2);
  const obj = ed.selected();
  expect('+ 3D Object adds one, with the lighting controls', obj.shape === 'object3d' && [...doc.querySelectorAll('#panel h4')].some(h => h.textContent === 'Spotlight'));
  const lm = [...doc.querySelectorAll('#panel .row')].find(r => r.querySelector('label')?.textContent === 'Light model').querySelector('select');
  lm.value = '3'; lm.dispatchEvent(new win.Event('change')); await frames(1);
  expect('the Light model list switches the shader program (Toon)', obj.p.lightModel === 3);

  // ---- saving
  const json = JSON.parse(ed.snapshot);
  expect('the show is saved in the desktop format (surfaces, keyframes, presets, markers)', json.format === 'maphappy' && json.surfaces.length === 3 && json.presets.length === 1 && json.timeline.markers.length === 1);
  await sleep(2200);
  expect('...and autosaved in the browser', win.localStorage.getItem('maphappy-editor-show') === ed.snapshot);
  expect('no errors were shown', !doc.documentElement.dataset.error);
} catch (e) { expect(`crashed: ${e.stack || e}`, false); }

lines.push(failed ? `SOME CHECKS FAILED (${passed} passed, ${failed} failed)` : `ALL CHECKS PASSED (${passed} passed, 0 failed)`);
document.getElementById('out').textContent = lines.join('\n');
document.title = failed ? `FAILED ${failed}` : `PASSED ${passed}`;
