#pragma once
#include "ContentSource.h"
#include "GLUtil.h"

// Procedural test pattern, rendered into an off-screen texture every frame.
inline constexpr const char* kPatternFS = R"GLSL(#version 330 core
uniform vec2 uRes;
uniform float uTime;
out vec4 frag;
void main() {
    // uv: (0,0) = top-left of the image, (1,1) = bottom-right
    vec2 uv = vec2(gl_FragCoord.x, uRes.y - gl_FragCoord.y) / uRes;
    vec3 col = vec3(0.08);

    if (uv.y < 0.15) {
        vec3 bars[7] = vec3[7](vec3(1.0), vec3(1.0, 1.0, 0.0), vec3(0.0, 1.0, 1.0),
                               vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 1.0),
                               vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0));
        col = bars[clamp(int(uv.x * 7.0), 0, 6)];
    } else {
        vec2 f = abs(fract(uv * vec2(16.0, 9.0)) - 0.5);
        if (max(f.x, f.y) > 0.47) col = vec3(0.6);          // grid lines
    }

    float d = length((uv - 0.5) * vec2(uRes.x / uRes.y, 1.0));
    if (abs(d - 0.3) < 0.006) col = vec3(1.0, 0.6, 0.0);    // circle

    // Red square under the top-left corner so you can always tell orientation
    if (uv.x < 0.08 && uv.y > 0.15 && uv.y < 0.15 + 0.08 * uRes.x / uRes.y)
        col = vec3(1.0, 0.0, 0.0);

    if (abs(uv.x - fract(uTime * 0.2)) < 0.003) col = vec3(0.2, 1.0, 0.4); // moving line

    float e = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    if (e < 0.006) col = vec3(1.0);                         // border

    frag = vec4(col, 1.0);
}
)GLSL";

class PatternSource : public ContentSource {
public:
    explicit PatternSource(int w = 1280, int h = 720) {
        prog_    = makeProgram(kFullscreenVS, kPatternFS);
        locRes_  = glGetUniformLocation(prog_, "uRes");
        locTime_ = glGetUniformLocation(prog_, "uTime");
        glGenVertexArrays(1, &vao_);     // core profile needs a VAO bound, even an empty one
        target_ = makeRenderTarget(w, h);
    }
    ~PatternSource() override {
        destroyRenderTarget(target_);
        glDeleteVertexArrays(1, &vao_);
        glDeleteProgram(prog_);
    }

    void update(double dt) override {
        time_ += dt;
        glBindFramebuffer(GL_FRAMEBUFFER, target_.fbo);
        glViewport(0, 0, target_.w, target_.h);
        glUseProgram(prog_);
        glUniform2f(locRes_, (float)target_.w, (float)target_.h);
        glUniform1f(locTime_, (float)time_);
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    GLuint texture() const override { return target_.tex; }
    int    width()   const override { return target_.w; }
    int    height()  const override { return target_.h; }

private:
    GLuint prog_ = 0, vao_ = 0;
    GLint  locRes_ = -1, locTime_ = -1;
    RenderTarget target_;
    double time_ = 0.0;
};
