// The web editor's operations without a browser: surfaces, keyframes, songs, markers, snapping, the zip writer.
// Run with:  node --test web/tests/editor.test.mjs
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { emptyShow, readShow, writeShow, applyTimeline } from '../js/show.js';
import * as ops from '../js/editor-ops.js';
import { evaluateTrack, readTimeline, writeTimeline } from '../js/timeline.js';
import { makeZip, crc32, uniquePath } from '../js/files.js';

const near = (a, b, tol = 1e-6) => assert.ok(Math.abs(a - b) <= tol, `${a} is not within ${tol} of ${b}`);

test('adding, ordering and removing surfaces, named like the desktop app', () => {
  const show = emptyShow();
  const a = ops.addSurface(show, 'quad'), b = ops.addSurface(show, 'circle'), c = ops.addSurface(show, 'quad');
  assert.deepEqual(show.surfaces.map(s => s.name), ['Quad-2', 'Circle-1', 'Quad-1']);
  assert.ok(a.id !== b.id && b.id !== c.id);
  near(b.in[1].x - b.in[0].x, 1 / (16 / 9), 1e-6);                     // a circle samples a square of the picture
  ops.moveSurface(show, c.id, +1); assert.equal(show.surfaces[1].id, c.id);
  ops.setKey(show, a.id, 'opacity', 1, 0.5);
  ops.removeSurface(show, a.id);
  assert.equal(show.surfaces.length, 2); assert.equal(show.timeline.tracks.length, 0);
});

test('changing shape keeps the surface where it was', () => {
  const show = emptyShow(), s = ops.addSurface(show, 'quad');
  s.out = [{ x: 0.2, y: 0.4 }, { x: 0.8, y: 0.4 }, { x: 0.8, y: 0.6 }, { x: 0.2, y: 0.6 }];
  ops.setShape(show, s, 'line');
  near(s.lineA.x, 0.2); near(s.lineB.x, 0.8); near(s.lineA.y, 0.5);
  ops.setShape(show, s, 'quad');
  near((s.out[0].x + s.out[1].x) / 2, 0.5); assert.ok(ops.isConvex(s.out));
  assert.ok(!ops.isConvex([{ x: 0, y: 0 }, { x: 1, y: 1 }, { x: 1, y: 0 }, { x: 0, y: 1 }]));      // a bow-tie
});

test('keyframes: set, replace, interpolate, move and delete', () => {
  const show = emptyShow(), s = ops.addSurface(show, 'quad');
  ops.setKey(show, s.id, 'rotation', 1, 0);
  const t = ops.setKey(show, s.id, 'rotation', 3, 90);
  assert.equal(t.clips.length, 1); assert.equal(t.clips[0].keys.length, 2);
  near(evaluateTrack(t, 2), 45, 1e-6);                                       // smooth: halfway is halfway
  ops.setKey(show, s.id, 'rotation', 3.004, 180);                            // within half a frame: replaces
  assert.equal(t.clips[0].keys.length, 2); near(evaluateTrack(t, 3), 180);
  near(ops.animatedValue(show, s.id, 'rotation', 0), 0); assert.equal(ops.animatedValue(show, s.id, 'hue', 0), null);
  ops.setKey(show, s.id, 'rotation', 2, 9999);
  near(evaluateTrack(t, 2), 720);                                            // kept in range
  const ks = ops.keysOf(t);
  assert.deepEqual(ks.map(k => k.time), [1, 2, 3]);
  const idx = ops.moveKey(show, ks[1].clip, ks[1].index, 5);
  assert.equal(idx, 2); near(ops.keysOf(t)[2].time, 5); assert.ok(show.timeline.length >= 5);
  ops.setKey(show, s.id, 'visible', 1, 0);
  assert.equal(ops.findTrack(show, ops.trackPath(s.id, 'visible')).clips[0].keys[0].interp, 'hold');   // on/off keys hold
  for (let i = 2; i >= 0; i--) ops.deleteKey(show, t, t.clips[0], i);
  assert.equal(ops.findTrack(show, ops.trackPath(s.id, 'rotation')), null);
  assert.equal(ops.setKey(show, s.id, 'nope', 1, 1), null);
});

test('keys made in the editor drive the show, and survive saving (desktop format)', () => {
  const show = emptyShow(), s = ops.addSurface(show, 'object3d');
  ops.setKey(show, s.id, 'rotY', 0, 0, 'linear'); ops.setKey(show, s.id, 'rotY', 4, 360, 'linear');
  ops.setKey(show, s.id, 'lightModel', 0, 2); ops.setKey(show, s.id, 'lightModel', 2, 3);
  applyTimeline(show, 1); near(s.p.rotY, 90); near(s.p.lightModel, 2);
  applyTimeline(show, 2.5); near(s.p.lightModel, 3);
  applyTimeline(show, 30); near(s.p.lightModel, 3); near(s.p.rotY, 360);            // after the last key the value holds (no looping)
  const loop = ops.findTrack(show, ops.trackPath(s.id, 'rotY')).clips[0];
  loop.duration = 12; applyTimeline(show, 5); near(s.p.rotY, 90);                     // a clip stretched past its keys loops them, as on the desktop
  ops.setKey(show, s.id, 'rotY', 2, 180, 'linear'); assert.equal(loop.duration, 12);   // ...and stays a loop when edited
  const j = writeShow(show);
  const again = readShow(JSON.parse(JSON.stringify(j)), () => null);
  assert.deepEqual(writeShow(again), j);
  assert.equal(writeTimeline(readTimeline(j.timeline)).tracks.length, 2);
});

test('a song goes on an audio track after what is there; beats and markers snap', () => {
  const show = emptyShow();
  const a = ops.addSong(show, 'song.mp3', 'music/song.mp3', 30);
  const b = ops.addSong(show, 'two.mp3', 'music/two.mp3', 10);
  const track = show.timeline.tracks.find(t => t.kind === 'audio');
  assert.equal(track.audioClips.length, 2); near(track.audioClips[1].start, 30); near(show.timeline.length, 60, 1e-9);
  assert.ok(a !== b);
  assert.equal(ops.beatGrid(show), null);
  show.audioAssets.get(a).bpm = 120; show.audioAssets.get(a).beatOffset = 0.1;
  near(ops.snapTime(show, 1.08, 0.05), 1.1); near(ops.snapTime(show, 1.3, 0.05), 1.3);
  ops.addMarker(show, 1.32); ops.addMarker(show, 1.325);
  assert.equal(show.timeline.markers.length, 1); near(ops.snapTime(show, 1.3, 0.05), 1.32);
  assert.equal(ops.trackLabel(show, track), 'Audio-1');
});

test('file paths for added files never collide', () => {
  const taken = new Set(['music/song.mp3']);
  assert.equal(uniquePath('music', 'song.mp3', taken), 'music/song-2.mp3');
  assert.equal(uniquePath('models', 'My Model?.obj', taken), 'models/My Model_.obj');
});

test('the zip writer makes a real .zip (checked with Python)', () => {
  assert.equal(crc32(new TextEncoder().encode('hello')), 0x3610a686);
  const blob = makeZip([{ name: 'show/show.maphappy', data: new TextEncoder().encode('{"a":1}') }, { name: 'show/music/é.bin', data: new Uint8Array([1, 2, 3, 255]) }]);
  const dir = mkdtempSync(join(tmpdir(), 'mhzip-')), file = join(dir, 'show.zip');
  return blob.arrayBuffer().then(buf => {
    writeFileSync(file, Buffer.from(buf));
    const out = execFileSync(process.platform === 'win32' ? 'python' : 'python3', ['-c',
      'import zipfile,sys; z=zipfile.ZipFile(sys.argv[1]); assert z.testzip() is None; print(ascii(sorted((i.filename, i.file_size) for i in z.infolist()))); print(z.read("show/show.maphappy").decode())', file]).toString();
    rmSync(dir, { recursive: true, force: true });
    assert.ok(out.includes("('show/music/\\xe9.bin', 4)") && out.includes('{"a":1}'), out);
  });
});

test('tempo detection finds the demo song\'s 120 BPM and its first beat', async () => {
  const { readFileSync } = await import('node:fs');
  const { detectBeats } = await import('../js/beat.js');
  const b = readFileSync(new URL('../show/music/demo-beat.wav', import.meta.url));
  const rate = b.readUInt32LE(24), data = b.subarray(44), x = new Float32Array(data.length / 2);
  for (let i = 0; i < x.length; i++) x[i] = data.readInt16LE(i * 2) / 32768;
  const r = detectBeats(x, rate);
  near(r.bpm, 120, 0.05);
  const beat = 60 / r.bpm, err = Math.min(r.offset % beat, beat - (r.offset % beat));   // the beats land on 0, 0.5, 1, ...
  assert.ok(err < 0.03, `first beat off by ${err}`);
  assert.equal(detectBeats(new Float32Array(1000), rate).bpm, 0);                        // too short: no tempo
});

test('folders: nesting, no loops, hiding passes down, deleting keeps the contents', async () => {
  const { isShown } = await import('../js/show.js');
  const show = emptyShow();
  const a = ops.addSurface(show, 'quad'), b = ops.addSurface(show, 'circle');
  const outer = ops.addFolder(show, 'Outer'), inner = ops.addFolder(show, '', outer);
  assert.equal(show.folders.get(inner).name, `Folder ${inner}`); assert.equal(ops.folderDepth(show, inner), 2);
  assert.ok(ops.moveSurfaceToFolder(show, a.id, inner) && ops.moveSurfaceToFolder(show, b.id, outer));
  assert.ok(!ops.moveSurfaceToFolder(show, a.id, 99));
  assert.ok(!ops.moveFolder(show, outer, inner) && !ops.moveFolder(show, outer, outer));            // not inside itself
  assert.ok(isShown(show, a));
  show.folders.get(outer).visible = false;
  assert.ok(!isShown(show, a) && !isShown(show, b));                                                 // hiding passes down
  show.folders.get(outer).visible = true;
  ops.removeFolder(show, outer);
  assert.equal(show.folders.get(inner).parent, 0); assert.equal(b.folder, 0); assert.equal(a.folder, inner);
  const j = writeShow(show), again = readShow(JSON.parse(JSON.stringify(j)), () => null);
  assert.deepEqual(writeShow(again), j); assert.equal(again.folders.size, 1);
  assert.ok(ops.addFolder(show) > inner);
});
