// Browser tests for the web player: every shader compiles under WebGL 2, and the pictures are right
// (checked by reading pixels back from the stage, like the desktop app's GL tests).
import { makeTarget } from '../js/gl.js';
import { readShow, applyTimeline, PresetPlayer } from '../js/show.js';
import { buildMedia } from '../js/media.js';
import { Renderer } from '../js/renderer.js';

const lines = [];
let passed = 0, failed = 0;
function expect(what, ok) { lines.push(`${ok ? 'PASS' : 'FAIL'}  ${what}`); ok ? passed++ : failed++; }
const near = (a, b, tol) => Math.abs(a - b) <= tol;

const gl = document.getElementById('c').getContext('webgl2');
const W = 400, H = 400;

// A pixel of the stage, y counted from the TOP.
function px(stage, x, y) {
  const d = new Uint8Array(4);
  gl.bindFramebuffer(gl.FRAMEBUFFER, stage.fbo);
  gl.readPixels(x, stage.h - 1 - y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, d);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  return { r: d[0], g: d[1], b: d[2] };
}
const isColor = (p, r, g, b, tol = 4) => near(p.r, r, tol) && near(p.g, g, tol) && near(p.b, b, tol);
const quad = (x0, y0, x1, y1) => [[x0, y0], [x1, y0], [x1, y1], [x0, y1]];
const solid = (id, rgb) => ({ id, kind: 'generator', type: 'solid', width: 16, height: 16, params: { color: [...rgb, 1] } });
const project = (surfaces, media = [], extra = {}) => ({ format: 'maphappy', version: 1, output: { width: W, height: H }, media: [{ id: 1, kind: 'generator', type: 'pattern' }, ...media], surfaces, ...extra });

function build(json) {
  const show = readShow(json, (p, rel) => (rel ? new URL(rel, location.href).href : null));
  show.media = buildMedia(gl, show.mediaList, () => null, w => show.warnings.push(w));
  return show;
}

async function run() {
  if (!gl) { expect('WebGL 2 is available', false); return; }
  let renderer;
  try { renderer = new Renderer(gl); expect('the surface shader and all four lighting programs compile and link under WebGL 2', true); }
  catch (e) { expect(`shaders compile: ${e.message}`, false); return; }
  const stage = makeTarget(gl, W, H);
  const draw = show => renderer.renderStage(stage, show);

  // ---- every generator type compiles and draws
  try {
    const types = ['testcard', 'grid', 'text', 'solid', 'gradient', 'strobe', 'lines'];
    const show = build(project([], types.map((t, i) => ({ id: i + 2, kind: 'generator', type: t, width: 64, height: 32, params: t === 'text' ? { text: 'Hi' } : {} }))));
    expect('every generator type compiles and draws', show.media.size === 8 && show.warnings.length === 0);
  } catch (e) { expect(`generators: ${e.message}`, false); }

  // ---- the same pictures as the desktop app's render tests
  { const show = build(project([{ id: 1, shape: 'quad', media: 2, in: quad(0, 0, 1, 1), out: quad(0.25, 0.25, 0.75, 0.75) }], [solid(2, [1, 0.5, 0])]));
    draw(show);
    expect('a quad shows its picture, and only inside its mapped area', isColor(px(stage, 200, 200), 255, 128, 0) && isColor(px(stage, 105, 105), 255, 128, 0) && isColor(px(stage, 95, 200), 0, 0, 0) && isColor(px(stage, 305, 200), 0, 0, 0));
    show.surfaces[0].p.hue = 120; show.surfaces[0].media = 3;
    show.media = build(project([], [solid(3, [1, 0, 0])])).media;
    draw(show);
    expect('hue +120 degrees turns red into green exactly (as on the desktop)', isColor(px(stage, 200, 200), 0, 255, 0, 3));
    show.surfaces[0].p.hue = 0; show.surfaces[0].p.opacity = 0.5; draw(show);
    expect('opacity 50% blends with the black behind', isColor(px(stage, 200, 200), 128, 0, 0, 3));
    show.surfaces[0].p.opacity = 1; show.surfaces[0].p.posX = 0.25; draw(show);
    expect('position X moves the quad (100 px)', isColor(px(stage, 350, 200), 255, 0, 0) && isColor(px(stage, 150, 200), 0, 0, 0)); }

  { const show = build(project([{ id: 1, shape: 'circle', media: 2, in: quad(0, 0, 1, 1), out: quad(0.25, 0.25, 0.75, 0.75) }], [solid(2, [1, 0.5, 0])]));
    draw(show);
    expect('a circle is round', isColor(px(stage, 200, 200), 255, 128, 0) && isColor(px(stage, 112, 112), 0, 0, 0) && isColor(px(stage, 266, 266), 255, 128, 0) && isColor(px(stage, 276, 276), 0, 0, 0));
    const s = show.surfaces[0];
    s.shape = 'freeform'; s.poly = [{ x: 0, y: 0 }, { x: 1, y: 0 }, { x: 1, y: 1 }, { x: 0.5, y: 0.5 }, { x: 0, y: 1 }]; draw(show);
    expect('a freeform outline fills its arms but not its notch', isColor(px(stage, 140, 200), 255, 128, 0) && isColor(px(stage, 200, 280), 0, 0, 0));
    s.shape = 'quad'; s.masks = [{ shape: 0, mode: 0, enabled: true, feather: 0, min: { x: 0.25, y: 0.25 }, max: { x: 0.75, y: 0.75 }, points: [] }]; draw(show);
    expect('a mask cuts a hole', isColor(px(stage, 200, 200), 0, 0, 0) && isColor(px(stage, 110, 200), 255, 128, 0));
    s.masks[0].mode = 1; draw(show);
    expect("...or keeps only what's inside it", isColor(px(stage, 200, 200), 255, 128, 0) && isColor(px(stage, 110, 200), 0, 0, 0)); }

  // ---- 3D objects and lighting (a white cube facing the camera)
  { const show = build(project([{ id: 1, shape: 'object3d', media: 2, mesh: 'cube', rotX: 0, rotY: 0, shading: 1, ambient: 0, ks: 0, dirAzimuth: 0, dirElevation: 0, lightModel: 2,
                                  in: quad(0, 0, 1, 1), out: quad(0, 0, 1, 1) }], [solid(2, [1, 1, 1])]));
    const s = show.surfaces[0], r = (x, y) => px(stage, x, y).r;
    draw(show);
    expect('3D: the cube is drawn, with empty space around it', r(200, 200) >= 250 && r(10, 10) === 0);
    s.p.dirAzimuth = 60; draw(show);
    expect('Blinn-Phong diffuse: at 60 degrees the face gets half the light', near(r(200, 200), 128, 4));
    s.p.ambient = 0.5; s.p.dirAzimuth = 90; draw(show);
    expect('the ambient term', near(r(200, 200), 128, 4));
    s.p.ambient = 0; s.p.lightModel = 0; draw(show);
    expect('the Unlit program shows the picture as it is', r(200, 200) >= 250);
    s.p.dirAzimuth = 0; s.p.lightModel = 2; s.p.ks = 1; s.media = 3;
    show.media.set(3, build(project([], [solid(3, [1, 0, 0])])).media.get(3));
    draw(show); const blinn = px(stage, 160, 160).g;
    s.p.lightModel = 1; draw(show); const phong = px(stage, 160, 160).g;
    expect('Phong and Blinn-Phong are different programs (Blinn highlight is wider)', px(stage, 200, 200).g > 200 && blinn > phong + 5);
    renderer.lightOverride = 3; draw(show); const toon = px(stage, 160, 160).g; renderer.lightOverride = -1;
    expect('the L key override swaps the program (Toon here)', toon !== phong);
    s.p.ks = 0; s.media = 2; s.p.dirIntensity = 0; s.p.spotIntensity = 2; s.p.spotX = 0; s.p.spotY = 0; s.p.spotZ = 4; s.p.spotAngle = 10; s.p.spotSoftness = 0; s.p.lightModel = 2; draw(show);
    expect('a narrow spotlight lights a disc in the middle only', r(200, 200) > 200 && r(120, 200) <= 3);
    s.p.spotIntensity = 0; s.p.pointIntensity = 1; s.p.pointX = 0; s.p.pointY = 0; s.p.pointZ = 1.2; s.p.pointRange = 1; draw(show);
    expect('a point light falls off with distance', r(200, 200) > r(90, 90) + 40); }

  // ---- the demo show: timeline, music track, presets
  try {
    const r = await fetch('../show/show.maphappy');
    const show = build(await r.json());
    expect('the demo show loads without warnings', show.warnings.length === 0 && show.audioAssets.size === 1 && show.timeline.audioClips.length === 1);
    const cube = show.surfaces.find(s => s.name === 'Cube'), title = show.surfaces.find(s => s.name === 'Title');
    applyTimeline(show, 0); const lm0 = cube.p.lightModel, t0 = title.p.opacity;
    applyTimeline(show, 9); const lm9 = cube.p.lightModel;
    applyTimeline(show, 17); const lm17 = cube.p.lightModel;
    expect('the timeline switches the light model program: Blinn-Phong, then Phong at 8 s, Toon at 16 s', lm0 === 2 && lm9 === 1 && lm17 === 3 && t0 === 0 && title.p.opacity === 1);
    applyTimeline(show, 2); const rotA = cube.p.rotY; applyTimeline(show, 4); const rotB = cube.p.rotY;
    expect('the cube turns with the music (a full turn every 8 beats)', near(rotB - rotA, 180, 1e-3));
    const stage2 = makeTarget(gl, show.outW / 4, show.outH / 4);
    renderer.renderStage(stage2, show);
    const centre = px(stage2, stage2.w / 2, stage2.h / 2), corner = px(stage2, 2, 2);
    expect('the demo draws the cube over the backdrop', centre.r + centre.g + centre.b > corner.r + corner.g + corner.b + 30);
    const player = new PresetPlayer();
    player.start(show, show.presets[9], 9); player.update(0.75);
    const half = cube.p.opacity; player.update(1);
    expect('preset 0 ("Dark") fades the cube out', near(half, 0.5, 0.01) && cube.p.opacity === 0);
    renderer.renderStage(stage2, show);
    const after = px(stage2, stage2.w / 2, stage2.h / 2);
    expect('...and then only the backdrop is left in the middle', after.r + after.g + after.b < centre.r + centre.g + centre.b - 30);
  } catch (e) { expect(`demo show: ${e.message}`, false); }

  expect('no WebGL errors', gl.getError() === gl.NO_ERROR);
}

run().catch(e => expect(`crashed: ${e.stack || e}`, false)).finally(() => {
  lines.push(failed ? `SOME CHECKS FAILED (${passed} passed, ${failed} failed)` : `ALL CHECKS PASSED (${passed} passed, 0 failed)`);
  document.getElementById('out').textContent = lines.join('\n');
  document.title = failed ? `FAILED ${failed}` : `PASSED ${passed}`;
});
