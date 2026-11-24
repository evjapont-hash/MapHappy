// Draws a show into the stage texture, the same way as the desktop app's Renderer.h:
//   1. every visible 3D object is rendered with depth into its own texture, lit by its light model's program;
//   2. surfaces are drawn back to front: each stage pixel is mapped back through the warp into the media.
import { compileProgram, makeTarget, destroyTarget, colMajor3 } from './gl.js';
import { squareToQuad, invert3, effectiveOut, effectiveIn, objectModel, objectViewProjection, objectCameraDistance, mul4 } from './math.js';
import { builtinMesh, parseObj } from './mesh.js';
import { isShown } from './show.js';
import * as S from './shaders.gen.js';

const SHAPE_CODE = { quad: 0, circle: 1, triangle: 2, line: 0, freeform: 4, object3d: 0 };
export const LIGHT_MODELS = ['Unlit', 'Phong', 'Blinn-Phong', 'Toon'];
const SHADE_BODIES = [S.kShadeUnlitFS, S.kShadePhongFS, S.kShadeBlinnPhongFS, S.kShadeToonFS];

export class Renderer {
  constructor(gl) {
    this.gl = gl;
    this.surfaceProg = compileProgram(gl, S.kFullscreenVS, S.kSurfaceFS, 'surface shader');
    // one program per light model, all compiled now; each 3D object picks its own with useProgram
    this.lightProgs = SHADE_BODIES.map((body, i) => compileProgram(gl, S.kMeshVS, S.kLightCommonFS + body + S.kLightMainFS, `${LIGHT_MODELS[i]} lighting`));
    this.vao = gl.createVertexArray();
    this.meshes = new Map();            // 'cube' | url -> { vao, count }
    this.meshErrors = new Map();
    this.objTargets = new Map();        // surface id -> render target with depth
    this.lightOverride = -1;            // -1 = each object's own light model; else force one (the L key)
  }

  // A model on the GPU. Model files load in the background; until then (or if they fail) the cube stands in.
  mesh(kind, url) {
    const key = kind === 'file' ? url : kind;
    if (this.meshes.has(key)) return this.meshes.get(key);
    if (kind === 'file') {
      this.meshes.set(key, this.mesh('cube'));
      fetch(url).then(r => { if (!r.ok) throw new Error(`${r.status} ${r.statusText}`); return r.text(); })
        .then(text => this.meshes.set(key, this.upload(parseObj(text))))
        .catch(e => this.meshErrors.set(url, String(e.message || e)));
      return this.meshes.get(key);
    }
    const m = this.upload(builtinMesh(kind));
    this.meshes.set(key, m);
    return m;
  }
  upload(verts) {
    const gl = this.gl, vao = gl.createVertexArray(), vbo = gl.createBuffer();
    gl.bindVertexArray(vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
    gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);
    for (const [loc, size, off] of [[0, 3, 0], [1, 3, 12], [2, 2, 24]]) { gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, size, gl.FLOAT, false, 32, off); }
    gl.bindVertexArray(null);
    return { vao, count: verts.length / 8 };
  }

  renderStage(stage, show) {
    const gl = this.gl;
    this.renderObjects(stage, show);
    gl.bindFramebuffer(gl.FRAMEBUFFER, stage.fbo);
    gl.viewport(0, 0, stage.w, stage.h);
    gl.disable(gl.DEPTH_TEST); gl.disable(gl.SCISSOR_TEST);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.enable(gl.BLEND);
    const p = this.surfaceProg;
    gl.useProgram(p);
    gl.bindVertexArray(this.vao);
    gl.activeTexture(gl.TEXTURE0);
    gl.uniform1i(p.u('uTex'), 0);
    gl.uniform2f(p.u('uRes'), stage.w, stage.h);
    const aspect = stage.w / Math.max(1, stage.h);

    for (let i = show.surfaces.length - 1; i >= 0; i--) {          // back-most first, so the front of the list ends on top
      const s = show.surfaces[i];
      if (!isShown(show, s) || s.p.opacity <= 0) continue;
      if (s.shape === 'freeform' && s.poly.length < 3) continue;
      const src = show.media.get(s.media);
      if (!src) continue;
      const eo = effectiveOut(s, aspect);
      let ei = effectiveIn(s, src.w / Math.max(1, src.h)), tex = src.tex;
      if (s.shape === 'object3d') {
        const t = this.objTargets.get(s.id);
        if (!t) continue;
        tex = t.tex; ei = [{ x: 0, y: 0 }, { x: 1, y: 0 }, { x: 1, y: 1 }, { x: 0, y: 1 }];
      }
      const outH = squareToQuad(eo), invOut = outH && invert3(outH), inH = squareToQuad(ei);
      if (!invOut || !inH) continue;
      this.setBlend(s.blend);
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.uniformMatrix3fv(p.u('uInvOut'), false, colMajor3(invOut));
      gl.uniformMatrix3fv(p.u('uIn'), false, colMajor3(inH));
      gl.uniform1i(p.u('uShape'), SHAPE_CODE[s.shape]);
      if (s.shape === 'freeform') { gl.uniform2fv(p.u('uPoly'), new Float32Array(s.poly.flatMap(v => [v.x, v.y]))); gl.uniform1i(p.u('uPolyN'), s.poly.length); }
      const q = s.p;
      gl.uniform1f(p.u('uOpacity'), q.opacity);
      gl.uniform2f(p.u('uFlip'), s.flipH ? 1 : 0, s.flipV ? 1 : 0);
      gl.uniform3f(p.u('uTint'), q.tintR, q.tintG, q.tintB);
      gl.uniform1f(p.u('uHue'), q.hue * Math.PI / 180);
      gl.uniform1f(p.u('uSat'), q.saturation);
      gl.uniform1f(p.u('uContrast'), q.contrast);
      gl.uniform1f(p.u('uBright'), q.brightness);
      gl.uniform1f(p.u('uFeather'), q.feather);
      gl.uniform1i(p.u('uBlend'), s.blend);
      this.setMasks(s);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    }
    gl.disable(gl.BLEND);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  }

  setMasks(s) {
    const gl = this.gl, p = this.surfaceProg;
    const shape = [], mode = [], feather = [], box = [], start = [], count = [], pts = [];
    for (const m of s.masks) {
      if (!m.enabled || shape.length >= 6 || (m.shape === 2 && m.points.length < 3)) continue;
      shape.push(m.shape); mode.push(m.mode); feather.push(m.feather);
      box.push(m.min.x, m.min.y, m.max.x, m.max.y);
      start.push(pts.length / 2);
      for (const v of m.points.slice(0, 16)) pts.push(v.x, v.y);
      count.push(pts.length / 2 - start[start.length - 1]);
    }
    gl.uniform1i(p.u('uMaskCount'), shape.length);
    if (!shape.length) return;
    gl.uniform1iv(p.u('uMaskShape'), shape); gl.uniform1iv(p.u('uMaskMode'), mode); gl.uniform1fv(p.u('uMaskFeather'), feather);
    gl.uniform4fv(p.u('uMaskBox'), box); gl.uniform1iv(p.u('uMaskStart'), start); gl.uniform1iv(p.u('uMaskN'), count);
    if (pts.length) gl.uniform2fv(p.u('uMaskPts'), pts);
  }

  renderObjects(stage, show) {
    const gl = this.gl, aspect = stage.w / Math.max(1, stage.h), live = new Set();
    for (const s of show.surfaces) {
      if (s.shape !== 'object3d' || !isShown(show, s) || s.p.opacity <= 0) continue;
      const src = show.media.get(s.media);
      if (!src) continue;
      const eo = effectiveOut(s, aspect);
      const px = (a, b) => Math.hypot((a.x - b.x) * stage.w, (a.y - b.y) * stage.h);
      const w = 0.5 * (px(eo[0], eo[1]) + px(eo[3], eo[2])), h = 0.5 * (px(eo[0], eo[3]) + px(eo[1], eo[2]));
      if (w < 1 || h < 1) continue;
      const k = Math.min(1, 2048 / Math.max(w, h)), tw = Math.max(16, Math.round(w * k)), th = Math.max(16, Math.round(h * k));
      let t = this.objTargets.get(s.id);
      if (!t || t.w !== tw || t.h !== th) { destroyTarget(gl, t); t = makeTarget(gl, tw, th, true); this.objTargets.set(s.id, t); }
      live.add(s.id);
      const inH = squareToQuad(effectiveIn(s, src.w / Math.max(1, src.h)));
      if (!inH) continue;
      const q = s.p, model = objectModel(q.rotX, q.rotY, q.rotZ, q.objScale), mvp = mul4(objectViewProjection(q.fov, tw / th), model);
      const lm = this.lightOverride >= 0 ? this.lightOverride : Math.max(0, Math.min(3, Math.round(q.lightModel)));
      const prog = this.lightProgs[lm];

      gl.bindFramebuffer(gl.FRAMEBUFFER, t.fbo);
      gl.viewport(0, 0, tw, th);
      gl.clearColor(0, 0, 0, 0); gl.clearDepth(1);
      gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
      gl.enable(gl.DEPTH_TEST); gl.depthFunc(gl.LESS);
      gl.disable(gl.BLEND); gl.disable(gl.CULL_FACE);
      gl.useProgram(prog);                                       // this object's light model
      gl.uniformMatrix4fv(prog.u('uMVP'), false, mvp);
      gl.uniformMatrix4fv(prog.u('uModel'), false, model);
      gl.uniformMatrix3fv(prog.u('uIn'), false, colMajor3(inH));
      gl.uniform1i(prog.u('uTex'), 0);
      this.setLights(prog, s, tw / th, lm === 0);
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, src.tex);
      const m = this.mesh(s.mesh, s.meshUrl);
      gl.bindVertexArray(m.vao);
      gl.drawArrays(gl.TRIANGLES, 0, m.count);
      gl.bindVertexArray(null);
      gl.disable(gl.DEPTH_TEST);
    }
    for (const [id, t] of this.objTargets) if (!live.has(id)) { destroyTarget(gl, t); this.objTargets.delete(id); }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  }

  // The lights and material (see LightingShaders.h for what each uniform means).
  setLights(prog, s, aspect, unlit) {
    const gl = this.gl, L = s.p, deg = Math.PI / 180, u = n => prog.u(n);
    gl.uniform1f(u('uLighting'), unlit ? 0 : L.shading);
    gl.uniform3f(u('uCamPos'), 0, 0, objectCameraDistance(L.fov, aspect));
    gl.uniform1f(u('uAmbient'), L.ambient); gl.uniform1f(u('uKd'), L.kd); gl.uniform1f(u('uKs'), L.ks); gl.uniform1f(u('uShininess'), Math.max(1, L.shininess));
    const az = L.dirAzimuth * deg, el = L.dirElevation * deg;
    gl.uniform3f(u('uDirDir'), Math.sin(az) * Math.cos(el), Math.sin(el), Math.cos(az) * Math.cos(el));
    gl.uniform3f(u('uDirColor'), L.dirR * L.dirIntensity, L.dirG * L.dirIntensity, L.dirB * L.dirIntensity);
    gl.uniform3f(u('uPointPos'), L.pointX, L.pointY, L.pointZ);
    gl.uniform3f(u('uPointColor'), L.pointR * L.pointIntensity, L.pointG * L.pointIntensity, L.pointB * L.pointIntensity);
    gl.uniform1f(u('uPointRange'), Math.max(0.01, L.pointRange));
    gl.uniform3f(u('uSpotPos'), L.spotX, L.spotY, L.spotZ);
    const len = Math.max(1e-5, Math.hypot(L.spotX, L.spotY, L.spotZ));
    gl.uniform3f(u('uSpotDir'), -L.spotX / len, -L.spotY / len, -L.spotZ / len);
    gl.uniform3f(u('uSpotColor'), L.spotR * L.spotIntensity, L.spotG * L.spotIntensity, L.spotB * L.spotIntensity);
    const outer = Math.max(0.5, L.spotAngle) * 0.5 * deg, inner = outer * (1 - Math.max(0, Math.min(1, L.spotSoftness)));
    gl.uniform1f(u('uSpotCosOuter'), Math.cos(outer)); gl.uniform1f(u('uSpotCosInner'), Math.cos(inner) + 1e-4);
  }

  setBlend(b) {
    const gl = this.gl;
    if (b === 1) gl.blendFunc(gl.ONE, gl.ONE);
    else if (b === 2) gl.blendFunc(gl.DST_COLOR, gl.ZERO);
    else if (b === 3) gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_COLOR);
    else gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
  }
}
