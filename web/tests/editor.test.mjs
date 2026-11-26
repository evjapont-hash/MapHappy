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

test('song clips: move, trim, split and delete, never overlapping', () => {
  const show = emptyShow();
  ops.addSong(show, 'a.mp3', 'music/a.mp3', 10);
  ops.addSong(show, 'b.mp3', 'music/b.mp3', 10);
  const track = show.timeline.tracks.find(t => t.kind === 'audio'), [a, b] = track.audioClips;
  ops.moveAudioClip(show, track, b, 5);
  near(b.start, 10);                                                       // can't slide back over the first clip
  ops.trimAudioClip(show, track, a, 'start', 2, 10);
  near(a.start, 2); near(a.offset, 2); near(a.duration, 8);              // the left edge trims the beginning of the song
  ops.trimAudioClip(show, track, a, 'start', -5, 10);
  near(a.start, 0); near(a.offset, 0);                                    // ...but not before the song starts
  ops.trimAudioClip(show, track, a, 'end', 30, 10);
  near(a.duration, 10);                                                    // nor past its end (or the next clip)
  ops.trimAudioClip(show, track, a, 'end', 6, 10); near(a.duration, 6);
  ops.moveAudioClip(show, track, b, 7); near(b.start, 7);                 // room opened up
  const second = ops.splitAudioClip(show, track, b, 9);
  assert.ok(second); near(b.duration, 2); near(second.start, 9); near(second.offset, 2); near(second.duration, 8);
  assert.equal(ops.splitAudioClip(show, track, b, 50), null);
  assert.equal(track.audioClips.length, 3); assert.notEqual(second.id, b.id);
  ops.deleteAudioClip(show, track, second); ops.deleteAudioClip(show, track, b); ops.deleteAudioClip(show, track, a);
  assert.equal(show.timeline.tracks.length, 0);
});

test('clips: make, move, stretch (loops), duplicate, ghost copy, delete', () => {
  const show = emptyShow(), s = ops.addSurface(show, 'quad');
  ops.setKey(show, s.id, 'opacity', 0, 0); ops.setKey(show, s.id, 'opacity', 1, 1);
  const track = show.timeline.tracks[0], a = track.clips[0];
  assert.equal(ops.addClip(show, track, 0.5, 0.3), null);                   // no room inside another clip
  const b = ops.addClip(show, track, 3, 0.3);
  assert.ok(b && b.start === 3 && b.duration === 2 && b.keys.length === 2 && b.keys[0].v === 0.3);
  ops.moveClip(show, track, b, 0.2); near(b.start, 1);                       // it stops at the clip before it
  ops.resizeClip(show, track, a, 0.5); near(a.duration, 0.5);
  ops.resizeClip(show, track, a, 2); near(a.duration, 1);                    // ...and the clip after it
  ops.moveClip(show, track, b, 5);
  ops.resizeClip(show, track, a, 3); near(a.duration, 3);
  near(evaluateTrack(track, 1.5), evaluateTrack(track, 0.5));               // longer than its keys: they loop
  const dup = ops.duplicateClip(show, track, a, false), ghost = ops.duplicateClip(show, track, a, true);
  near(dup.start, 7); near(ghost.start, 10);                                 // the next free spots after it (b is at 5..7, so 3..6 is taken)
  assert.ok(dup.keys !== a.keys && ghost.keys === a.keys && ghost.ghost && !dup.ghost);
  a.keys[1].v = 0.5;
  assert.equal(ghost.keys[1].v, 0.5); assert.equal(dup.keys[1].v, 1);       // a ghost follows the original; a duplicate doesn't
  const again = readShow(JSON.parse(JSON.stringify(writeShow(show))));       // ghosts survive saving
  const t2 = again.timeline.tracks[0];
  assert.ok(t2.clips.find(c => c.ghost).keys === t2.clips[0].keys);
  ops.removeClip(show, track, dup);
  assert.equal(track.clips.length, 3);
  for (const c of [...track.clips]) ops.removeClip(show, track, c);
  assert.equal(show.timeline.tracks.length, 0);                              // the last clip takes its track with it
});

test('keyframes: copy and paste (onto the same or another track), move several, delete several', () => {
  const show = emptyShow(), s = ops.addSurface(show, 'quad');
  ops.setKey(show, s.id, 'opacity', 1, 0.2); ops.setKey(show, s.id, 'opacity', 2, 0.8);
  ops.setKey(show, s.id, 'posX', 1.5, 0.1);
  const op = show.timeline.tracks.find(t => t.path.endsWith('/opacity')), px = show.timeline.tracks.find(t => t.path.endsWith('/posX'));
  const pick = (t, i) => { const k = ops.keysOf(t)[i]; return { track: t, clip: k.clip, key: k.key }; };
  const both = ops.copyKeys([pick(op, 0), pick(op, 1)]);
  const pasted = ops.pasteKeys(show, both, 4);
  assert.equal(pasted.length, 2);
  near(evaluateTrack(op, 4), 0.2); near(evaluateTrack(op, 5), 0.8);          // at the playhead, the same spacing and values
  const onX = ops.pasteKeys(show, both, 6, px.path);                         // from one track onto another
  assert.ok(onX.every(x => x.track === px)); near(evaluateTrack(px, 7), 0.8);
  const mixed = ops.copyKeys([pick(op, 0), pick(px, 0)]);
  ops.pasteKeys(show, mixed, 10, op.path);                                   // from two tracks: each goes back to its own
  near(evaluateTrack(op, 10), 0.2); near(evaluateTrack(px, 10.5), 0.1);
  const moving = ops.keyTimes(pasted);
  ops.shiftKeys(show, moving, 0.5);
  near(evaluateTrack(op, 4.5), 0.2); near(evaluateTrack(op, 5.5), 0.8);
  ops.shiftKeys(show, moving, 1);                                            // (always from where they started)
  near(evaluateTrack(op, 6), 0.8);
  const n = ops.keysOf(op).length;
  ops.deleteKeys(show, pasted);
  assert.equal(ops.keysOf(op).length, n - 2);
  ops.removeSurface(show, s.id);
  assert.equal(ops.pasteKeys(show, both, 0).length, 0);                      // its surface is gone: nothing to paste onto
});
