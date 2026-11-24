#pragma once
#include "GLUtil.h"
#include "LightingShaders.h"
#include "Project.h"
#include <algorithm>
#include <map>
#include <vector>

// Draws every visible surface into the stage texture.
//
// For each stage pixel the shader works backwards:
//   stage pixel --(inverse of the out-quad homography)--> surface unit square
//   unit square --(shape: distance to the edge, for anti-aliasing and feathering)
//   unit square --(in-quad homography)--> position in the media --> sample --> colour controls --> blend.
inline constexpr const char* kSurfaceFS = R"GLSL(#version 330 core
uniform sampler2D uTex;
uniform mat3  uInvOut;    // stage (normalized) -> surface unit square
uniform mat3  uIn;        // surface unit square -> media (normalized)
uniform vec2  uRes;       // render target size in pixels
uniform int   uShape;     // 0 quad (and line), 1 circle, 2 triangle, 4 freeform polygon
uniform vec2  uPoly[64];  // freeform: the polygon, in the unit square
uniform int   uPolyN;
// masks, in the surface's unit square: shape 0 rect / 1 ellipse / 2 polygon, mode 0 hide inside / 1 show only inside
uniform int   uMaskCount;
uniform int   uMaskShape[6];
uniform int   uMaskMode[6];
uniform float uMaskFeather[6];
uniform vec4  uMaskBox[6];      // rect / ellipse: min x, min y, max x, max y
uniform int   uMaskStart[6];    // polygon: where its points start in uMaskPts, and how many
uniform int   uMaskN[6];
uniform vec2  uMaskPts[96];
uniform float uOpacity;
uniform vec2  uFlip;      // 1.0 = flip that axis
uniform vec3  uTint;
uniform float uHue;       // radians
uniform float uSat;
uniform float uContrast;
uniform float uBright;
uniform float uFeather;   // 0 = crisp
uniform int   uBlend;     // 0 over, 1 add, 2 multiply, 3 screen
out vec4 frag;

// Signed distance to a polygon (even-odd rule for inside), so freeform edges get the same anti-aliasing and feather.
float polyDistance(vec2 s) {
    float d = 1e9;
    bool inside = false;
    for (int i = 0; i < uPolyN; i++) {
        vec2 a = uPoly[i == 0 ? uPolyN - 1 : i - 1], b = uPoly[i];
        vec2 e = b - a, w = s - a;
        float t = clamp(dot(w, e) / max(dot(e, e), 1e-12), 0.0, 1.0);
        d = min(d, length(w - e * t));
        if ((a.y > s.y) != (b.y > s.y) && s.x < (b.x - a.x) * (s.y - a.y) / (b.y - a.y) + a.x) inside = !inside;
    }
    return inside ? d : -d;
}

// How far inside mask i a point is (negative = outside), in unit-square units.
float maskDistance(int i, vec2 s) {
    vec4 b = uMaskBox[i];
    if (uMaskShape[i] == 0) return min(min(s.x - b.x, b.z - s.x), min(s.y - b.y, b.w - s.y));
    if (uMaskShape[i] == 1) {
        vec2 c = (b.xy + b.zw) * 0.5, r = max((b.zw - b.xy) * 0.5, vec2(1e-5));
        return (1.0 - length((s - c) / r)) * min(r.x, r.y);             // close to the true distance near the edge
    }
    float d = 1e9;
    bool inside = false;
    int n = uMaskN[i], o = uMaskStart[i];
    for (int k = 0; k < n; k++) {
        vec2 a = uMaskPts[o + (k == 0 ? n - 1 : k - 1)], e2 = uMaskPts[o + k];
        vec2 e = e2 - a, w = s - a;
        float t = clamp(dot(w, e) / max(dot(e, e), 1e-12), 0.0, 1.0);
        d = min(d, length(w - e * t));
        if ((a.y > s.y) != (e2.y > s.y) && s.x < (e2.x - a.x) * (s.y - a.y) / (e2.y - a.y) + a.x) inside = !inside;
    }
    return inside ? d : -d;
}
// What is left of the surface at s after all its masks (1 = untouched, 0 = cut away).
float maskFactor(vec2 s) {
    float f = 1.0;
    for (int i = 0; i < uMaskCount; i++) {
        float d = maskDistance(i, s);
        float cover = clamp(d / max(fwidth(d), 1e-6) + 0.5, 0.0, 1.0);        // anti-aliased like the surface's own edges
        if (uMaskFeather[i] > 0.0) cover = min(cover, smoothstep(-uMaskFeather[i] * 0.5, uMaskFeather[i] * 0.5, d));
        f *= uMaskMode[i] == 0 ? 1.0 - cover : cover;
    }
    return f;
}

// How far inside the shape a point is, in unit-square units (negative = outside).
float edgeDistance(vec2 s) {
    if (uShape == 4) return polyDistance(s);
    if (uShape == 1) return 0.5 - length(s - 0.5);
    if (uShape == 2) {
        const float k = 0.894427191;                       // 1 / sqrt(1.25): the slanted edges' normal length
        float dl = ((s.x - 0.5) + 0.5 * s.y) * k;
        float dr = (-(s.x - 0.5) + 0.5 * s.y) * k;
        return min(min(dl, dr), 1.0 - s.y);
    }
    return min(min(s.x, 1.0 - s.x), min(s.y, 1.0 - s.y));
}

void main() {
    vec2 p = vec2(gl_FragCoord.x / uRes.x, 1.0 - gl_FragCoord.y / uRes.y);
    vec3 q = uInvOut * vec3(p, 1.0);
    vec2 s = q.xy / q.z;

    // Coverage of this pixel by the shape: a one-pixel soft edge instead of a hard jagged cut.
    float d = edgeDistance(s);
    float edgeAlpha = clamp(d / max(fwidth(d), 1e-6) + 0.5, 0.0, 1.0);

    vec2 t = s;
    if (uFlip.x > 0.5) t.x = 1.0 - t.x;
    if (uFlip.y > 0.5) t.y = 1.0 - t.y;
    vec3 m = uIn * vec3(t, 1.0);
    vec2 uv = m.xy / m.z;
    vec4 c = texture(uTex, vec2(uv.x, 1.0 - uv.y));       // sampled before any discard so the derivatives stay valid

    // Where the picture ends (e.g. zoomed out past its edge) it is cut with a soft one-pixel edge. The ramp starts
    // a pixel OUTSIDE the picture so the surface's own anti-aliased edge, which overshoots the picture by up to half
    // a pixel, isn't sliced in half by this test.
    vec2 fuv = fwidth(uv);
    float mdist = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    float inMedia = clamp(mdist / max(max(fuv.x, fuv.y), 1e-6) + 1.5, 0.0, 1.0);

    if (edgeAlpha <= 0.0) discard;
    float a = c.a * uOpacity * edgeAlpha * inMedia;
    if (uFeather > 0.0) a *= smoothstep(0.0, uFeather * 0.5, d);
    if (uMaskCount > 0) a *= maskFactor(s);
    if (a <= 0.0) discard;

    vec3 col = c.rgb * uTint;
    // hue: rotate the colour around the grey axis (exact: 120 degrees turns red into green)
    float cs = cos(uHue), sn = sin(uHue);
    float k = (1.0 - cs) / 3.0, r = sn * 0.57735027;
    mat3 H = mat3(cs + k, k + r, k - r,
                  k - r, cs + k, k + r,
                  k + r, k - r, cs + k);
    col = H * col;
    float luma = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(vec3(luma), col, uSat);
    col = (col - 0.5) * uContrast + 0.5 + uBright;
    col = clamp(col, 0.0, 1.0);

    if (uBlend == 1)      frag = vec4(col * a, 1.0);                       // add:      dst + src*a
    else if (uBlend == 2) frag = vec4(mix(vec3(1.0), col, a), 1.0);        // multiply: dst * mix(1, src, a)
    else if (uBlend == 3) frag = vec4(col * a, 1.0);                       // screen:   1 - (1-dst)(1-src*a)
    else                  frag = vec4(col, a);                             // over
}
)GLSL";

class Renderer {
public:
    Renderer() {
        prog_ = makeProgram(kFullscreenVS, kSurfaceFS);
        auto u = [&](const char* n) { return glGetUniformLocation(prog_, n); };
        locTex_ = u("uTex"); locInvOut_ = u("uInvOut"); locIn_ = u("uIn"); locRes_ = u("uRes"); locShape_ = u("uShape");
        locOpacity_ = u("uOpacity"); locFlip_ = u("uFlip"); locTint_ = u("uTint"); locHue_ = u("uHue"); locSat_ = u("uSat");
        locContrast_ = u("uContrast"); locBright_ = u("uBright"); locFeather_ = u("uFeather"); locBlend_ = u("uBlend");
        locPoly_ = u("uPoly"); locPolyN_ = u("uPolyN");
        locMaskCount_ = u("uMaskCount"); locMaskShape_ = u("uMaskShape"); locMaskMode_ = u("uMaskMode"); locMaskFeather_ = u("uMaskFeather");
        locMaskBox_ = u("uMaskBox"); locMaskStart_ = u("uMaskStart"); locMaskN_ = u("uMaskN"); locMaskPts_ = u("uMaskPts");
        glGenVertexArrays(1, &vao_);
        for (int i = 0; i < kLightModelCount; i++) meshProgs_[i] = makeProgram(kMeshVS, lightingFragmentShader(i).c_str());   // all compiled now, picked per object
    }
    ~Renderer() {
        for (auto& kv : meshes_) { glDeleteVertexArrays(1, &kv.second.vao); glDeleteBuffers(1, &kv.second.vbo); }
        for (auto& kv : objTargets_) destroyObjTarget(kv.second);
        for (GLuint pr : meshProgs_) glDeleteProgram(pr);
        glDeleteVertexArrays(1, &vao_);
        glDeleteProgram(prog_);
    }
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // The texture a 3D object surface was last rendered into (0 if none). Used by the tests.
    GLuint objectTexture(int surfaceId) const { auto it = objTargets_.find(surfaceId); return it == objTargets_.end() ? 0 : it->second.rt.tex; }
    int objectTextureWidth(int surfaceId) const { auto it = objTargets_.find(surfaceId); return it == objTargets_.end() ? 0 : it->second.rt.w; }
    int objectTextureHeight(int surfaceId) const { auto it = objTargets_.find(surfaceId); return it == objTargets_.end() ? 0 : it->second.rt.h; }

    void renderStage(const RenderTarget& rt, Project& project) {
        renderObjects(rt, project);                 // 3D objects first, each into its own texture
        glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
        glViewport(0, 0, rt.w, rt.h);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_BLEND);
        glUseProgram(prog_);
        glBindVertexArray(vao_);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(locTex_, 0);
        glUniform2f(locRes_, (float)rt.w, (float)rt.h);
        const float stageAspect = (float)rt.w / (float)std::max(1, rt.h);

        // Back-most surface first, so the front of the list ends up on top.
        for (auto it = project.surfaces.rbegin(); it != project.surfaces.rend(); ++it) {
            const Surface& s = *it;
            if (!project.isShown(s) || s.opacity <= 0.0f) continue;
            if (s.shape == Shape::Freeform && s.poly.size() < 3) continue;
            ContentSource* src = project.media.source(s.mediaId);
            if (!src) continue;

            // The quads actually drawn: the mapped quads with the animatable transform applied.
            const float mediaAspect = (float)src->width() / (float)std::max(1, src->height());
            Vec2 eo[4], ei[4];
            effectiveOut(s, stageAspect, eo);
            effectiveIn(s, mediaAspect, ei);
            GLuint tex = src->texture();
            if (s.shape == Shape::Object3D) {           // the rendered model is the picture; the input quad was used to texture it
                auto it = objTargets_.find(s.id);
                if (it == objTargets_.end()) continue;
                tex = it->second.rt.tex;
                ei[0] = {0, 0}; ei[1] = {1, 0}; ei[2] = {1, 1}; ei[3] = {0, 1};
            }
            Mat3 outH, invOut, inH;
            if (!squareToQuad(eo, outH) || !invert(outH, invOut)) continue;       // e.g. scaled down to nothing
            if (!squareToQuad(ei, inH)) continue;

            setBlend(s.blend);
            glBindTexture(GL_TEXTURE_2D, tex);
            glUniformMatrix3fv(locInvOut_, 1, GL_TRUE, invOut.m);   // GL_TRUE: our matrices are row-major
            glUniformMatrix3fv(locIn_,     1, GL_TRUE, inH.m);
            glUniform1i(locShape_, (s.shape == Shape::Line || s.shape == Shape::Object3D) ? 0 : (int)s.shape);   // a line or a 3D object fills its quad
            if (s.shape == Shape::Freeform) {
                const int n = std::min<int>((int)s.poly.size(), kMaxPolyPoints);
                glUniform2fv(locPoly_, n, &s.poly[0].x);
                glUniform1i(locPolyN_, n);
            }
            glUniform1f(locOpacity_, s.opacity);
            glUniform2f(locFlip_, s.flipH ? 1.0f : 0.0f, s.flipV ? 1.0f : 0.0f);
            glUniform3f(locTint_, s.tintR, s.tintG, s.tintB);
            glUniform1f(locHue_, s.hue * 3.14159265f / 180.0f);
            glUniform1f(locSat_, s.saturation);
            glUniform1f(locContrast_, s.contrast);
            glUniform1f(locBright_, s.brightness);
            glUniform1f(locFeather_, s.feather);
            glUniform1i(locBlend_, (int)s.blend);
            setMasks(s);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        glDisable(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

private:
    struct GpuMesh { GLuint vao = 0, vbo = 0; GLsizei count = 0; };
    struct ObjTarget { RenderTarget rt; GLuint depth = 0; };

    static void destroyObjTarget(ObjTarget& t) { if (t.depth) glDeleteRenderbuffers(1, &t.depth); destroyRenderTarget(t.rt); t.depth = 0; }

    const GpuMesh& gpuMesh(const MeshData* m) {
        auto it = meshes_.find(m);
        if (it != meshes_.end()) return it->second;
        GpuMesh g;
        glGenVertexArrays(1, &g.vao); glGenBuffers(1, &g.vbo);
        glBindVertexArray(g.vao);
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(m->vertices.size() * sizeof(MeshVertex)), m->vertices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)(6 * sizeof(float)));
        glBindVertexArray(0);
        g.count = (GLsizei)m->vertices.size();
        return meshes_.emplace(m, g).first->second;
    }

    // Renders every visible 3D object into its own texture, sized like its quad on the stage (so nothing stretches).
    void renderObjects(const RenderTarget& stage, Project& project) {
        const float stageAspect = (float)stage.w / (float)std::max(1, stage.h);
        std::vector<int> live;
        for (const Surface& s : project.surfaces) {
            if (s.shape != Shape::Object3D || !project.isShown(s) || s.opacity <= 0.0f) continue;
            ContentSource* src = project.media.source(s.mediaId);
            if (!src) continue;
            Vec2 eo[4], ei[4];
            effectiveOut(s, stageAspect, eo);
            auto px = [&](Vec2 a, Vec2 b) { return std::hypot((a.x - b.x) * stage.w, (a.y - b.y) * stage.h); };
            float w = 0.5f * (px(eo[0], eo[1]) + px(eo[3], eo[2])), h = 0.5f * (px(eo[0], eo[3]) + px(eo[1], eo[2]));
            if (w < 1.0f || h < 1.0f) continue;
            const float k = std::min(1.0f, 2048.0f / std::max(w, h));
            const int tw = std::max(16, (int)std::lround(w * k)), th = std::max(16, (int)std::lround(h * k));
            ObjTarget& t = objTargets_[s.id];
            if (t.rt.w != tw || t.rt.h != th) {
                destroyObjTarget(t);
                t.rt = makeRenderTarget(tw, th);
                glGenRenderbuffers(1, &t.depth);
                glBindRenderbuffer(GL_RENDERBUFFER, t.depth);
                glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, tw, th);
                glBindFramebuffer(GL_FRAMEBUFFER, t.rt.fbo);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depth);
            }
            live.push_back(s.id);

            effectiveIn(s, (float)src->width() / (float)std::max(1, src->height()), ei);
            Mat3 inH;
            if (!squareToQuad(ei, inH)) continue;
            const Mat4 model = objectModel(s.rotX, s.rotY, s.rotZ, s.objScale);
            const Mat4 mvp = objectViewProjection(s.fov, (float)tw / (float)th) * model;

            glBindFramebuffer(GL_FRAMEBUFFER, t.rt.fbo);
            glViewport(0, 0, tw, th);
            glClearColor(0, 0, 0, 0);
            glClearDepth(1.0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
            glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
            const int lm = std::max(0, std::min(kLightModelCount - 1, (int)std::lround(s.light.model)));
            const GLuint prog = meshProgs_[lm];
            glUseProgram(prog);                                              // the light model's own program
            auto u = [&](const char* n) { return glGetUniformLocation(prog, n); };
            glUniformMatrix4fv(u("uMVP"), 1, GL_FALSE, mvp.m.data());
            glUniformMatrix4fv(u("uModel"), 1, GL_FALSE, model.m.data());
            glUniformMatrix3fv(u("uIn"), 1, GL_TRUE, inH.m);
            glUniform1i(u("uTex"), 0);
            setLightUniforms(prog, s, (float)tw / (float)th, lm == (int)LightModel::Unlit);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, src->texture());
            const GpuMesh& g = gpuMesh(meshFor(s.meshKind, s.meshPath));
            glBindVertexArray(g.vao);
            glDrawArrays(GL_TRIANGLES, 0, g.count);
            glBindVertexArray(0);
            glDisable(GL_DEPTH_TEST);
        }
        for (auto it = objTargets_.begin(); it != objTargets_.end();) {           // surfaces that are gone, hidden or no longer 3D
            if (std::find(live.begin(), live.end(), it->first) == live.end()) { destroyObjTarget(it->second); it = objTargets_.erase(it); }
            else ++it;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // The surface's enabled masks, as shader uniforms (a polygon needs at least three points).
    void setMasks(const Surface& s) {
        int shape[kMaxMasks] = {}, mode[kMaxMasks] = {}, start[kMaxMasks] = {}, count[kMaxMasks] = {};
        float feather[kMaxMasks] = {}, box[kMaxMasks * 4] = {};
        std::vector<float> pts;
        int n = 0;
        for (const Mask& m : s.masks) {
            if (!m.enabled || n >= kMaxMasks) continue;
            if (m.shape == MaskShape::Polygon && m.points.size() < 3) continue;
            shape[n] = (int)m.shape; mode[n] = (int)m.mode; feather[n] = m.feather;
            box[n * 4 + 0] = m.min.x; box[n * 4 + 1] = m.min.y; box[n * 4 + 2] = m.max.x; box[n * 4 + 3] = m.max.y;
            start[n] = (int)pts.size() / 2;
            for (size_t k = 0; k < m.points.size() && k < (size_t)kMaxMaskPoints; k++) { pts.push_back(m.points[k].x); pts.push_back(m.points[k].y); }
            count[n] = (int)pts.size() / 2 - start[n];
            n++;
        }
        glUniform1i(locMaskCount_, n);
        if (n == 0) return;
        glUniform1iv(locMaskShape_, n, shape); glUniform1iv(locMaskMode_, n, mode); glUniform1fv(locMaskFeather_, n, feather);
        glUniform4fv(locMaskBox_, n, box); glUniform1iv(locMaskStart_, n, start); glUniform1iv(locMaskN_, n, count);
        if (!pts.empty()) glUniform2fv(locMaskPts_, (GLsizei)pts.size() / 2, pts.data());
    }

    // The lights and material of a 3D object (see LightingShaders.h for what each uniform means).
    static void setLightUniforms(GLuint prog, const Surface& s, float aspect, bool unlit) {
        auto u = [&](const char* n) { return glGetUniformLocation(prog, n); };
        const LightRig& L = s.light;
        const float deg = 3.14159265f / 180.0f;
        glUniform1f(u("uLighting"), unlit ? 0.0f : s.shading);
        glUniform3f(u("uCamPos"), 0.0f, 0.0f, objectCameraDistance(s.fov, aspect));
        glUniform1f(u("uAmbient"), L.ambient); glUniform1f(u("uKd"), L.kd); glUniform1f(u("uKs"), L.ks); glUniform1f(u("uShininess"), std::max(1.0f, L.shininess));
        const float az = L.dirAzimuth * deg, el = L.dirElevation * deg;
        glUniform3f(u("uDirDir"), std::sin(az) * std::cos(el), std::sin(el), std::cos(az) * std::cos(el));
        glUniform3f(u("uDirColor"), L.dirR * L.dirIntensity, L.dirG * L.dirIntensity, L.dirB * L.dirIntensity);
        glUniform3f(u("uPointPos"), L.pointX, L.pointY, L.pointZ);
        glUniform3f(u("uPointColor"), L.pointR * L.pointIntensity, L.pointG * L.pointIntensity, L.pointB * L.pointIntensity);
        glUniform1f(u("uPointRange"), std::max(0.01f, L.pointRange));
        glUniform3f(u("uSpotPos"), L.spotX, L.spotY, L.spotZ);
        const float len = std::max(1e-5f, std::sqrt(L.spotX * L.spotX + L.spotY * L.spotY + L.spotZ * L.spotZ));
        glUniform3f(u("uSpotDir"), -L.spotX / len, -L.spotY / len, -L.spotZ / len);          // aimed at the model
        glUniform3f(u("uSpotColor"), L.spotR * L.spotIntensity, L.spotG * L.spotIntensity, L.spotB * L.spotIntensity);
        const float outer = std::max(0.5f, L.spotAngle) * 0.5f * deg, inner = outer * (1.0f - std::max(0.0f, std::min(1.0f, L.spotSoftness)));
        glUniform1f(u("uSpotCosOuter"), std::cos(outer)); glUniform1f(u("uSpotCosInner"), std::cos(inner) + 1e-4f);
    }

    static void setBlend(Blend b) {
        switch (b) {
            case Blend::Add:      glBlendFunc(GL_ONE, GL_ONE); break;
            case Blend::Multiply: glBlendFunc(GL_DST_COLOR, GL_ZERO); break;
            case Blend::Screen:   glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR); break;
            default:              glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
    }

    GLuint prog_ = 0, vao_ = 0;
    GLuint meshProgs_[kLightModelCount] = {};                // one program per light model (LightingShaders.h)
    std::map<const MeshData*, GpuMesh> meshes_;
    std::map<int, ObjTarget> objTargets_;                   // by surface id
    GLint locTex_ = -1, locInvOut_ = -1, locIn_ = -1, locRes_ = -1, locShape_ = -1, locOpacity_ = -1, locFlip_ = -1;
    GLint locPoly_ = -1, locPolyN_ = -1;
    GLint locMaskCount_ = -1, locMaskShape_ = -1, locMaskMode_ = -1, locMaskFeather_ = -1, locMaskBox_ = -1, locMaskStart_ = -1, locMaskN_ = -1, locMaskPts_ = -1;
    GLint locTint_ = -1, locHue_ = -1, locSat_ = -1, locContrast_ = -1, locBright_ = -1, locFeather_ = -1, locBlend_ = -1;
};
