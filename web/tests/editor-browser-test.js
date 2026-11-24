// Drives the web editor like a person would and checks the result.
import { squareToQuad, mapPoint } from '../js/math.js';
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
  await drag(tl, { x: tv.xOf(2), y: row.y + row.h / 2 }, { x: tv.xOf(3), y: row.y + row.h / 2 });
  expect('dragging a keyframe on the timeline moves it', near(track.clips[0].keys[1].t, 3, 0.05));
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
  pointer(overlay, 'pointerdown', 0, 0); pointer(overlay, 'pointerup', 0, 0);
  const mid = onStage(unitToStage({ x: 0.5, y: 0.7 })), b2 = overlay.getBoundingClientRect();
  overlay.dispatchEvent(new win.MouseEvent('dblclick', { bubbles: true, clientX: b2.left + mid.x, clientY: b2.top + mid.y })); await frames(1);
  expect('double-clicking a polygon mask edge adds a point', pm.points.length === 4);
  const p0 = onStage(unitToStage(pm.points[0]));
  overlay.dispatchEvent(new win.MouseEvent('contextmenu', { bubbles: true, cancelable: true, clientX: b2.left + p0.x, clientY: b2.top + p0.y })); await frames(1);
  expect('right-clicking one of its points deletes it', pm.points.length === 3);

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
  expect('no errors were shown', !doc.getElementById('status').dataset.error);
} catch (e) { expect(`crashed: ${e.stack || e}`, false); }

lines.push(failed ? `SOME CHECKS FAILED (${passed} passed, ${failed} failed)` : `ALL CHECKS PASSED (${passed} passed, 0 failed)`);
document.getElementById('out').textContent = lines.join('\n');
document.title = failed ? `FAILED ${failed}` : `PASSED ${passed}`;
