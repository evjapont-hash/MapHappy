#pragma once
#include "GLUtil.h"
#include "Project.h"

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
uniform int   uShape;     // 0 quad, 1 circle, 2 triangle
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

// How far inside the shape a point is, in unit-square units (negative = outside).
float edgeDistance(vec2 s) {
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
        glGenVertexArrays(1, &vao_);
    }
    ~Renderer() {
        glDeleteVertexArrays(1, &vao_);
        glDeleteProgram(prog_);
    }

    void renderStage(const RenderTarget& rt, Project& project) {
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
            if (!s.visible || s.opacity <= 0.0f) continue;
            ContentSource* src = project.media.source(s.mediaId);
            if (!src) continue;

            // The quads actually drawn: the mapped quads with the animatable transform applied.
            const float mediaAspect = (float)src->width() / (float)std::max(1, src->height());
            Vec2 eo[4], ei[4];
            effectiveOut(s, stageAspect, eo);
            effectiveIn(s, mediaAspect, ei);
            Mat3 outH, invOut, inH;
            if (!squareToQuad(eo, outH) || !invert(outH, invOut)) continue;       // e.g. scaled down to nothing
            if (!squareToQuad(ei, inH)) continue;

            setBlend(s.blend);
            glBindTexture(GL_TEXTURE_2D, src->texture());
            glUniformMatrix3fv(locInvOut_, 1, GL_TRUE, invOut.m);   // GL_TRUE: our matrices are row-major
            glUniformMatrix3fv(locIn_,     1, GL_TRUE, inH.m);
            glUniform1i(locShape_, (int)s.shape);
            glUniform1f(locOpacity_, s.opacity);
            glUniform2f(locFlip_, s.flipH ? 1.0f : 0.0f, s.flipV ? 1.0f : 0.0f);
            glUniform3f(locTint_, s.tintR, s.tintG, s.tintB);
            glUniform1f(locHue_, s.hue * 3.14159265f / 180.0f);
            glUniform1f(locSat_, s.saturation);
            glUniform1f(locContrast_, s.contrast);
            glUniform1f(locBright_, s.brightness);
            glUniform1f(locFeather_, s.feather);
            glUniform1i(locBlend_, (int)s.blend);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        glDisable(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

private:
    static void setBlend(Blend b) {
        switch (b) {
            case Blend::Add:      glBlendFunc(GL_ONE, GL_ONE); break;
            case Blend::Multiply: glBlendFunc(GL_DST_COLOR, GL_ZERO); break;
            case Blend::Screen:   glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR); break;
            default:              glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
    }

    GLuint prog_ = 0, vao_ = 0;
    GLint locTex_ = -1, locInvOut_ = -1, locIn_ = -1, locRes_ = -1, locShape_ = -1, locOpacity_ = -1, locFlip_ = -1;
    GLint locTint_ = -1, locHue_ = -1, locSat_ = -1, locContrast_ = -1, locBright_ = -1, locFeather_ = -1, locBlend_ = -1;
};
