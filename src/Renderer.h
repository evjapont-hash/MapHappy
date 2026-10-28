#pragma once
#include "GLUtil.h"
#include "Project.h"

// Draws every visible surface into the stage texture.
//
// For each stage pixel the shader works backwards:
//   stage pixel --(inverse of the out-quad homography)--> surface unit square
//   unit square --(shape mask: discard outside the circle/triangle)
//   unit square --(in-quad homography)--> position in the media --> sample.
inline constexpr const char* kSurfaceFS = R"GLSL(#version 330 core
uniform sampler2D uTex;
uniform mat3  uInvOut;    // stage (normalized) -> surface unit square
uniform mat3  uIn;        // surface unit square -> media (normalized)
uniform vec2  uRes;       // render target size in pixels
uniform int   uShape;     // 0 quad, 1 circle, 2 triangle
uniform float uOpacity;
uniform vec2  uFlip;      // 1.0 = flip that axis
out vec4 frag;
void main() {
    vec2 p = vec2(gl_FragCoord.x / uRes.x, 1.0 - gl_FragCoord.y / uRes.y);
    vec3 q = uInvOut * vec3(p, 1.0);
    vec2 s = q.xy / q.z;
    if (s.x < 0.0 || s.x > 1.0 || s.y < 0.0 || s.y > 1.0) discard;
    if (uShape == 1 && length(s - 0.5) > 0.5) discard;
    if (uShape == 2 && abs(s.x - 0.5) > 0.5 * s.y) discard;
    if (uFlip.x > 0.5) s.x = 1.0 - s.x;
    if (uFlip.y > 0.5) s.y = 1.0 - s.y;
    vec3 m = uIn * vec3(s, 1.0);
    vec2 uv = m.xy / m.z;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) discard;
    vec4 c = texture(uTex, vec2(uv.x, 1.0 - uv.y));
    frag = vec4(c.rgb, c.a * uOpacity);
}
)GLSL";

class Renderer {
public:
    Renderer() {
        prog_      = makeProgram(kFullscreenVS, kSurfaceFS);
        locTex_    = glGetUniformLocation(prog_, "uTex");
        locInvOut_ = glGetUniformLocation(prog_, "uInvOut");
        locIn_     = glGetUniformLocation(prog_, "uIn");
        locRes_    = glGetUniformLocation(prog_, "uRes");
        locShape_  = glGetUniformLocation(prog_, "uShape");
        locOpacity_= glGetUniformLocation(prog_, "uOpacity");
        locFlip_   = glGetUniformLocation(prog_, "uFlip");
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
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(prog_);
        glBindVertexArray(vao_);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(locTex_, 0);
        glUniform2f(locRes_, (float)rt.w, (float)rt.h);

        // Back-most surface first, so the front of the list ends up on top.
        for (auto it = project.surfaces.rbegin(); it != project.surfaces.rend(); ++it) {
            const Surface& s = *it;
            if (!s.visible || s.opacity <= 0.0f) continue;
            ContentSource* src = project.media.source(s.mediaId);
            if (!src) continue;

            Mat3 outH, invOut, inH;
            if (!squareToQuad(s.out, outH) || !invert(outH, invOut)) continue;
            if (!squareToQuad(s.in, inH)) continue;

            glBindTexture(GL_TEXTURE_2D, src->texture());
            glUniformMatrix3fv(locInvOut_, 1, GL_TRUE, invOut.m);   // GL_TRUE: our matrices are row-major
            glUniformMatrix3fv(locIn_,     1, GL_TRUE, inH.m);
            glUniform1i(locShape_, (int)s.shape);
            glUniform1f(locOpacity_, s.opacity);
            glUniform2f(locFlip_, s.flipH ? 1.0f : 0.0f, s.flipV ? 1.0f : 0.0f);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        glDisable(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

private:
    GLuint prog_ = 0, vao_ = 0;
    GLint locTex_ = -1, locInvOut_ = -1, locIn_ = -1, locRes_ = -1, locShape_ = -1, locOpacity_ = -1, locFlip_ = -1;
};
