// The web player's logic without a browser: geometry, timeline, parameters, models, reading projects, presets.
// Run with:  node --test web/tests
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { squareToQuad, invert3, mapPoint, quadCenter, lineQuad, effectiveOut, effectiveIn, objectViewProjection, objectModel, transformPoint, mul4, rotY } from '../js/math.js';
import { valueAt, clipValue, evaluateTrack, readTimeline, writeTimeline, paramTracks, audioClips } from '../js/timeline.js';
import { PARAMS, defaultParams, cleanValue, parsePath } from '../js/params.js';
import { makeCube, makeSphere, makeCylinder, makePlane, parseObj } from '../js/mesh.js';
import { readShow, writeShow, isShown, applyTimeline, PresetPlayer, capturePreset, emptyShow } from '../js/show.js';

const near = (a, b, tol = 1e-6) => assert.ok(Math.abs(a - b) <= tol, `${a} is not within ${tol} of ${b}`);
const radius = v => { let r = 0; for (let i = 0; i < v.length; i += 8) r = Math.max(r, Math.hypot(v[i], v[i + 1], v[i + 2])); return r; };

test('the warp maps the unit square onto the quad, and back', () => {
  const q = [{ x: 0.1, y: 0.2 }, { x: 0.9, y: 0.1 }, { x: 0.8, y: 0.9 }, { x: 0.2, y: 0.7 }];
  const H = squareToQuad(q), inv = invert3(H);
  [[0, 0], [1, 0], [1, 1], [0, 1]].forEach(([x, y], i) => { const p = mapPoint(H, { x, y }); near(p.x, q[i].x, 1e-6); near(p.y, q[i].y, 1e-6); });
  const back = mapPoint(inv, mapPoint(H, { x: 0.3, y: 0.6 }));
  near(back.x, 0.3, 1e-5); near(back.y, 0.6, 1e-5);
  assert.equal(invert3(squareToQuad([{ x: 0, y: 0 }, { x: 0, y: 0 }, { x: 0, y: 0 }, { x: 0, y: 0 }])), null);   // collapsed: skipped when inverting, as on the desktop
});

test('spin turns a surface by itself, timed by the show clock', () => {
  const s = { shape: 'quad', p: { ...defaultParams(), rotation: 90 }, out: [{ x: 0.4, y: 0.4 }, { x: 0.6, y: 0.4 }, { x: 0.6, y: 0.6 }, { x: 0.4, y: 0.6 }] };
  const a = effectiveOut(s, 1);
  s.p.rotation = 0; s.p.spin = 45; s.clock = 2;
  const b = effectiveOut(s, 1);
  near(a[0].x, b[0].x, 1e-9); near(a[0].y, b[0].y, 1e-9);
  const show = readShow(project(), resolve); show.surfaces[0].p.spinY = 10;
  applyTimeline(show, 7); assert.equal(show.surfaces[0].clock, 7);
});

test('surface transforms match the desktop app', () => {
  const s = { shape: 'quad', p: defaultParams(), out: [{ x: 0.36, y: 0.25 }, { x: 0.64, y: 0.25 }, { x: 0.64, y: 0.75 }, { x: 0.36, y: 0.75 }], in: [{ x: 0, y: 0 }, { x: 1, y: 0 }, { x: 1, y: 1 }, { x: 0, y: 1 }] };
  s.p.posX = 0.1; let e = effectiveOut(s, 16 / 9); near(e[0].x, 0.46, 1e-6);
  s.p.posX = 0; s.p.scale = 2; e = effectiveOut(s, 16 / 9); near(e[0].x, 0.5 + (0.36 - 0.5) * 2, 1e-6); near(quadCenter(e).y, 0.5, 1e-6);
  s.p.scale = 1; s.p.contentScale = 2; const i = effectiveIn(s, 1); near(i[0].x, 0.25, 1e-6); near(i[2].x, 0.75, 1e-6);
  const l = { shape: 'line', p: { ...defaultParams(), lineWidth: 0.1 }, lineA: { x: 0.2, y: 0.5 }, lineB: { x: 0.8, y: 0.5 }, out: s.out };
  const lq = lineQuad(l, 16 / 9); near(lq[0].y, 0.45); near(lq[3].y, 0.55); near(effectiveOut(l, 16 / 9)[1].x, 0.8);
});

test('the timeline evaluates exactly like the desktop one', () => {
  const keys = [{ t: 0, v: 0, interp: 'linear' }, { t: 1, v: 10, interp: 'smooth' }, { t: 2, v: 0, interp: 'hold' }, { t: 3, v: 5, interp: 'linear' }];
  near(valueAt(keys, 0.5), 5); near(valueAt(keys, 1.5), 5); near(valueAt(keys, 1.25), 10 - 10 * (0.25 * 0.25 * 2.5));
  near(valueAt(keys, 2.5), 0); near(valueAt(keys, -1), 0); near(valueAt(keys, 9), 5);
  const loop = { start: 0, duration: 5, keys: [{ t: 0, v: 0, interp: 'linear' }, { t: 2, v: 1, interp: 'linear' }] };
  near(clipValue(loop, 3), 0.5); near(clipValue(loop, 4), 1); near(clipValue(loop, 99), 0.5);
  const track = { clips: [{ start: 2, duration: 2, keys: [{ t: 0, v: 3, interp: 'linear' }, { t: 2, v: 4, interp: 'linear' }] }, { start: 6, duration: 1, keys: [{ t: 0, v: 9, interp: 'hold' }, { t: 1, v: 9, interp: 'hold' }] }] };
  near(evaluateTrack(track, 0), 3); near(evaluateTrack(track, 3), 3.5); near(evaluateTrack(track, 5), 4); near(evaluateTrack(track, 6.5), 9);
  assert.equal(evaluateTrack({ clips: [] }, 1), null);
});

test('reading a timeline keeps ghost clips sharing their keyframes, and skips damage', () => {
  const tl = readTimeline({ length: 10, tracks: [
    { id: 1, kind: 'param', path: 'surface/1/opacity', clipData: [{ keys: [{ t: 1, v: 1, curve: 'linear' }, { t: 0, v: 0, curve: 'linear' }] }], clips: [{ id: 1, start: 0, duration: 1, data: 0 }, { id: 2, start: 3, duration: 1, data: 0, ghost: true }, { id: 3, start: 5, duration: 1, data: 7 }] },
    { id: 2, kind: 'audio', audioClips: [{ id: 4, asset: 1, start: 1, duration: 8, offset: 2, gain: 9 }] }, 'junk', { id: 1, kind: 'param' }] });
  const t = paramTracks(tl)[0];
  assert.equal(tl.tracks.length, 2);
  assert.equal(t.clips.length, 2); assert.equal(t.clips[0].keys, t.clips[1].keys); near(t.clips[0].keys[0].t, 0);
  assert.equal(audioClips(tl)[0].gain, 4); near(tl.length, 10);
  assert.ok(tl.counters.track >= 3 && tl.counters.clip >= 5);
  // writing it back keeps ghosts linked (one shared keyframe list)
  const w = writeTimeline(tl);
  assert.equal(w.tracks[0].clipData.length, 1); assert.equal(w.tracks[0].clips[1].data, 0); assert.equal(w.tracks[0].clips[1].ghost, true);
  const again = readTimeline(JSON.parse(JSON.stringify(w)));
  assert.deepEqual(writeTimeline(again), w);
});

test('parameters: the same table, ranges and paths as the desktop app', () => {
  assert.equal(PARAMS.size, 59);
  assert.equal(cleanValue('opacity', 5), 1); assert.equal(cleanValue('visible', true), 1); assert.equal(cleanValue('visible', 0.2), 0);
  assert.equal(cleanValue('nope', 1), null); assert.equal(cleanValue('hue', NaN), null);
  assert.deepEqual(parsePath('surface/12/posX'), { id: 12, key: 'posX' }); assert.equal(parsePath('audio/1'), null);
  assert.ok(PARAMS.get('rotY').applies({ shape: 'object3d' }) && !PARAMS.get('rotY').applies({ shape: 'quad' }));
});

test('built-in models and OBJ files fit a ball of radius 1', () => {
  for (const m of [makeCube(), makeSphere(), makeCylinder(), makePlane()]) near(radius(m), 1, 1e-5);
  assert.equal(makeCube().length / 24, 12);
  const tri = parseObj('v 0 0 0\nv 2 0 0\nv 0 2 0\nvt 0 0\nvt 1 0\nvt 0 1\nf 1/1 2/2 3/3\n');
  assert.equal(tri.length, 24); near(radius(tri), 1, 1e-5); near(tri[7], 1); near(tri[5], 1, 1e-6);
  assert.equal(parseObj('v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf -4 -3 -2 -1\n').length / 24, 2);
  assert.throws(() => parseObj('nothing here'));
});

test('the 3D camera just fits a model of radius 1, like the desktop one', () => {
  const vp = objectViewProjection(40, 1), sphere = makeSphere();
  let top = 0;
  for (let i = 0; i < sphere.length; i += 8) top = Math.max(top, transformPoint(vp, sphere[i], sphere[i + 1], sphere[i + 2])[1]);
  assert.ok(top > 0.97 && top <= 1.001, `top ${top}`);
  const p = transformPoint(mul4(rotY(90), objectModel(0, 0, 0, 1)), 1, 0, 0); near(p[2], -1, 1e-6);
});

// A small project, as the desktop app writes it.
const project = () => ({
  format: 'maphappy', version: 1, output: { width: 1280, height: 720 },
  media: [{ id: 1, kind: 'generator', type: 'pattern' }, { id: 2, kind: 'video', name: 'clip', path: 'C:/x/clip.mp4', pathRel: 'media/clip.mp4' }],
  folders: [{ id: 1, name: 'F', parent: 0, visible: true }, { id: 2, name: 'G', parent: 1, visible: true }],
  surfaces: [
    { id: 1, name: 'Cube', shape: 'object3d', media: 1, folder: 2, opacity: 0.5, rotY: 45, shininess: 999, mesh: 'file', meshPath: '/m/a.obj', meshPathRel: 'a.obj', in: [[0, 0], [1, 0], [1, 1], [0, 1]], out: [[0, 0], [1, 0], [1, 1], [0, 1]],
      masks: [{ shape: 'ellipse', mode: 'showOnly', min: [0.1, 0.2], max: [0.9, 2] }] },
    { id: 2, name: 'Poly', shape: 'freeform', points: [[0, 0], [1, 0], [0.5, 1]], blend: 'add', in: 'broken', out: [[0, 0], [1, 0], [1, 1], [0, 1]] },
  ],
  timeline: { length: 20, tracks: [{ id: 1, kind: 'param', path: 'surface/1/opacity', clipData: [{ keys: [{ t: 0, v: 0, curve: 'linear' }, { t: 2, v: 1, curve: 'linear' }] }], clips: [{ id: 1, start: 1, duration: 2, data: 0 }] }] },
  presets: [{ slot: 2, name: 'Blue', fade: 1, surfaces: [{ id: 1, media: 1, values: { hue: 200, visible: false } }], folders: [{ id: 2, visible: false }] }, { slot: 99 }],
  audioAssets: [{ id: 1, name: 'song', path: '/a/song.mp3', pathRel: 'music/song.mp3' }],
});
const resolve = (path, rel) => (rel ? `https://host/show/${rel}` : null);

test('reading a project: surfaces, folders, masks, models and songs', () => {
  const show = readShow(project(), resolve);
  assert.equal(show.outW, 1280);
  const cube = show.surfaces[0], poly = show.surfaces[1];
  assert.equal(cube.shape, 'object3d'); near(cube.p.opacity, 0.5); near(cube.p.rotY, 45); near(cube.p.shininess, 256);
  assert.equal(cube.meshUrl, 'https://host/show/a.obj');
  assert.equal(cube.masks[0].mode, 1); near(cube.masks[0].max.y, 1);
  assert.equal(poly.blend, 1); assert.equal(poly.poly.length, 3); near(poly.in[2].x, 1);
  assert.equal(show.audioAssets.get(1).url, 'https://host/show/music/song.mp3');
  assert.ok(show.presets[2] && !show.presets[9]);
  assert.throws(() => readShow({ format: 'nope' }, resolve));
});

test('folders hide what is inside them, and the timeline drives parameters', () => {
  const show = readShow(project(), resolve);
  const cube = show.surfaces[0];
  assert.ok(isShown(show, cube));
  show.folders.get(1).visible = false; assert.ok(!isShown(show, cube));
  show.folders.get(1).visible = true;
  applyTimeline(show, 2); near(cube.p.opacity, 0.5);
  applyTimeline(show, 0); near(cube.p.opacity, 0);
  applyTimeline(show, 10); near(cube.p.opacity, 1);
});

test('presets cross-fade, switch things on at the start and off at the end', () => {
  const show = readShow(project(), resolve);
  show.media = new Map([[1, {}]]);
  const cube = show.surfaces[0], pl = new PresetPlayer();
  cube.p.hue = 0;
  pl.start(show, show.presets[2], 2);
  assert.ok(pl.fading); near(cube.p.hue, 0);
  pl.update(0.5); near(cube.p.hue, 100, 1e-6); assert.equal(cube.p.visible, 1); assert.ok(show.folders.get(2).visible);
  pl.update(0.6); near(cube.p.hue, 200); assert.equal(cube.p.visible, 0); assert.ok(!show.folders.get(2).visible); assert.ok(!pl.fading);
});

test('writeShow saves what readShow read: a show survives a round trip unchanged', () => {
  const a = readShow(project(), resolve);
  const j1 = writeShow(a);
  const b = readShow(JSON.parse(JSON.stringify(j1)), resolve);
  assert.deepEqual(writeShow(b), j1);
  assert.equal(j1.format, 'maphappy'); assert.equal(j1.surfaces.length, 2);
  assert.equal(j1.surfaces[0].mesh, 'file'); assert.equal(j1.surfaces[0].meshPathRel, 'a.obj');
  assert.equal(j1.surfaces[1].blend, 'add'); assert.equal(j1.surfaces[1].points.length, 3);
  assert.equal(j1.surfaces[0].masks[0].mode, 'showOnly');
  assert.equal(j1.surfaces[0].visible, true);                    // on/off values are saved as true/false, like the desktop app
  assert.equal(j1.presets.length, 1); assert.equal(j1.presets[0].key, 3);
  assert.equal(j1.audioAssets[0].pathRel, 'music/song.mp3');
  assert.ok(j1.counters.surface >= 3);
});

test('an empty show, and capturing a preset from the current look', () => {
  const e = emptyShow();
  assert.equal(e.surfaces.length, 0); assert.equal(e.mediaList[0].type, 'pattern');
  const show = readShow(project(), resolve);
  show.surfaces[0].p.hue = 33;
  const pr = capturePreset(show, 'Now', 0.5);
  assert.equal(pr.surfaces.length, 2); near(pr.surfaces[0].values.hue, 33); assert.ok(!('lineWidth' in pr.surfaces[0].values));
});
