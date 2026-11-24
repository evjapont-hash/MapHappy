#pragma once
// A generator in the media pool: a picture made by a shader (and, for text, a small text image), redrawn every
// frame into its own texture. The parameters come from GeneratorDefs.h; parameter "x" is the uniform "u_x".
#include "ContentSource.h"
#include "GLUtil.h"
#include "GeneratorDefs.h"
#include "TextRaster.h"
#include <cmath>
#include <string>
#include <vector>

// Shared by every generator shader: the picture's size, time, and a top-left based position.
inline constexpr const char* kGenCommonFS = R"GLSL(#version 330 core
uniform vec2  uRes;        // picture size in pixels
uniform float uTime;       // seconds the generator has been playing
uniform float uPhase;      // strobe: position in the flash cycle (computed in double precision on the CPU)
uniform sampler2D uText;   // text image (coverage in .r, row 0 = top), when the generator shows text
uniform vec2  uTextSize;   // its size in pixels (0 = no text)
out vec4 frag;
vec2 pixTop() { return vec2(gl_FragCoord.x, uRes.y - gl_FragCoord.y); }   // pixels, (0,0) = top-left
vec2 uvTop()  { return pixTop() / uRes; }
// Coverage of the text image placed with its top-left at `org` (pixels) and `height` pixels tall.
float textAt(vec2 pix, vec2 org, float height) {
    if (uTextSize.y <= 0.0 || height <= 0.0) return 0.0;
    vec2 t = (pix - org) / (height / uTextSize.y);
    if (t.x < 0.0 || t.y < 0.0 || t.x > uTextSize.x || t.y > uTextSize.y) return 0.0;
    return texture(uText, t / uTextSize).r;
}
)GLSL";

inline constexpr const char* kGenSolidFS = R"GLSL(
uniform vec4 u_color;
void main() { frag = u_color; }
)GLSL";

inline constexpr const char* kGenGridFS = R"GLSL(
uniform float u_columns, u_rows, u_thickness, u_scrollX, u_scrollY;
uniform vec4  u_lineColor, u_background;
void main() {
    vec2 cells = vec2(u_columns, u_rows);
    vec2 cellPx = uRes / cells;
    vec2 g = uvTop() * cells - vec2(u_scrollX, u_scrollY) * uTime;     // position in cells
    vec2 d = abs(fract(g + 0.5) - 0.5) * cellPx;                        // pixels to the nearest line
    vec2 cov = clamp(u_thickness * 0.5 - d + 0.5, 0.0, 1.0);
    frag = mix(u_background, u_lineColor, max(cov.x, cov.y));
}
)GLSL";

inline constexpr const char* kGenLinesFS = R"GLSL(
uniform float u_count, u_thickness, u_softness, u_angle, u_speed;
uniform vec4  u_color, u_background;
void main() {
    float a = radians(u_angle);
    vec2 dir = vec2(cos(a), sin(a));
    float spacing = uRes.x / u_count;                                  // pixels between lines (at angle 0)
    float s = dot(pixTop() - uRes * 0.5, dir) / spacing - u_speed * uTime;
    float edge = (u_thickness * 0.5 - abs(fract(s) - 0.5)) * spacing;  // pixels inside the line (negative = outside)
    float soft = max(1.0, u_softness * spacing * 0.5);
    frag = mix(u_background, u_color, clamp(edge / soft + 0.5, 0.0, 1.0));
}
)GLSL";

inline constexpr const char* kGenGradientFS = R"GLSL(
uniform vec4  u_colorA, u_colorB;
uniform float u_style, u_angle, u_spin, u_repeat, u_scroll;
void main() {
    float aspect = uRes.x / uRes.y;
    vec2 q = (uvTop() - 0.5) * vec2(aspect, 1.0);
    float t;
    if (u_style < 0.5) {                                                // linear: exactly 0..1 from one side to the other
        float a = radians(u_angle + u_spin * uTime);
        vec2 dir = vec2(cos(a), sin(a));
        float extent = abs(dir.x) * aspect + abs(dir.y);               // the picture's width along dir
        t = dot(q, dir) / extent + 0.5;
    } else {                                                            // radial: 0 in the middle, 1 in the corners
        t = length(q) / (0.5 * length(vec2(aspect, 1.0)));
    }
    t = t * u_repeat - u_scroll * uTime;
    t = 1.0 - abs(mod(t, 2.0) - 1.0);                                   // mirrored repeats, so there are no hard seams
    frag = mix(u_colorA, u_colorB, t);
}
)GLSL";

inline constexpr const char* kGenStrobeFS = R"GLSL(
uniform float u_duty, u_fade;
uniform vec4  u_onColor, u_offColor;
void main() {
    float level = uPhase < u_duty ? 1.0 : (u_fade > 0.0 ? max(0.0, 1.0 - (uPhase - u_duty) / u_fade) : 0.0);
    frag = mix(u_offColor, u_onColor, level);
}
)GLSL";

inline constexpr const char* kGenTextFS = R"GLSL(
uniform float u_align, u_size, u_posY, u_scroll;
uniform vec4  u_color, u_background;
void main() {
    vec2 pix = pixTop();
    float h = u_size * uRes.y * max(1.0, uTextSize.y / 96.0);           // u_size is the height of ONE line
    float w = uTextSize.y > 0.0 ? uTextSize.x * h / uTextSize.y : 0.0;
    float x0 = u_align < 0.5 ? uRes.x * 0.05 : (u_align < 1.5 ? (uRes.x - w) * 0.5 : uRes.x * 0.95 - w);
    if (u_scroll != 0.0) x0 = uRes.x - mod(u_scroll * uTime * uRes.x, uRes.x + w);   // a ticker: leaves one side, comes back on the other
    float y0 = u_posY * uRes.y - h * 0.5;
    frag = mix(u_background, u_color, textAt(pix, vec2(x0, y0), h));
}
)GLSL";

inline constexpr const char* kGenTestCardFS = R"GLSL(
uniform float u_columns, u_rows, u_sweep;
uniform vec4  u_accent;
void main() {
    vec2 uv = uvTop(), pix = pixTop();
    vec3 col = vec3(0.10);

    // grid
    vec2 cellPx = uRes / vec2(u_columns, u_rows);
    vec2 d = abs(fract(uv * vec2(u_columns, u_rows) + 0.5) - 0.5) * cellPx;
    if (min(d.x, d.y) < 1.0) col = vec3(0.55);

    // colour bars and a grey ramp across the middle band
    if (uv.y > 0.62 && uv.y < 0.74) {
        vec3 bars[8] = vec3[8](vec3(1.0), vec3(1.0, 1.0, 0.0), vec3(0.0, 1.0, 1.0), vec3(0.0, 1.0, 0.0),
                               vec3(1.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0));
        col = bars[clamp(int(uv.x * 8.0), 0, 7)];
    } else if (uv.y >= 0.74 && uv.y < 0.80) {
        col = vec3(floor(uv.x * 11.0) / 10.0);
    }

    // a circle that should look round on the real surface, and a centre cross
    float r = length((uv - 0.5) * vec2(uRes.x / uRes.y, 1.0));
    float ring = abs(r - 0.42) * uRes.y;
    if (ring < 2.0) col = u_accent.rgb;
    vec2 c = abs(pix - uRes * 0.5);
    if ((c.x < 1.5 && c.y < uRes.y * 0.06) || (c.y < 1.5 && c.x < uRes.y * 0.06)) col = u_accent.rgb;

    // corner marks: red top-left, so you can always tell which way up the picture is
    float m = uRes.y * 0.07;
    if (pix.x < m && pix.y < m) col = vec3(1.0, 0.0, 0.0);
    else if (pix.x > uRes.x - m && pix.y < m) col = vec3(0.0, 1.0, 0.0);
    else if (pix.x < m && pix.y > uRes.y - m) col = vec3(0.0, 0.4, 1.0);
    else if (pix.x > uRes.x - m && pix.y > uRes.y - m) col = vec3(1.0);

    // label box
    if (uTextSize.y > 0.0) {
        float h = uRes.y * 0.07 * (uTextSize.y / 96.0);
        float w = uTextSize.x * h / uTextSize.y;
        vec2 org = vec2((uRes.x - w) * 0.5, uRes.y * 0.22 - h * 0.5);
        if (all(greaterThan(pix, org - h * 0.15)) && all(lessThan(pix, org + vec2(w, h) + h * 0.15))) col = vec3(0.0);
        col = mix(col, vec3(1.0), textAt(pix, org, h));
    }

    if (u_sweep > 0.5 && abs(uv.x - fract(uTime * 0.2)) * uRes.x < 2.0) col = vec3(0.2, 1.0, 0.4);   // moving line: shows the picture is live
    float e = min(min(pix.x, uRes.x - pix.x), min(pix.y, uRes.y - pix.y));
    if (e < 2.0) col = vec3(1.0);                                       // border, to line up the edges
    frag = vec4(col, 1.0);
}
)GLSL";

inline const char* generatorShaderBody(GenType t) {
    switch (t) {
        case GenType::TestCard: return kGenTestCardFS;
        case GenType::Grid:     return kGenGridFS;
        case GenType::Text:     return kGenTextFS;
        case GenType::Gradient: return kGenGradientFS;
        case GenType::Strobe:   return kGenStrobeFS;
        case GenType::Lines:    return kGenLinesFS;
        default:                return kGenSolidFS;
    }
}

class GeneratorSource : public ContentSource {
public:
    explicit GeneratorSource(const GenSettings& s) : settings(s) {
        settings.setSize(settings.width, settings.height);
        const std::string fs = std::string(kGenCommonFS) + generatorShaderBody(settings.type);
        prog_ = makeProgram(kFullscreenVS, fs.c_str());
        glGenVertexArrays(1, &vao_);
        target_ = makeRenderTarget(settings.width, settings.height);
        render();                                     // so it has a picture before the first update
    }
    ~GeneratorSource() override {
        destroyRenderTarget(target_);
        if (textTex_) glDeleteTextures(1, &textTex_);
        glDeleteVertexArrays(1, &vao_);
        glDeleteProgram(prog_);
    }
    GeneratorSource(const GeneratorSource&) = delete;
    GeneratorSource& operator=(const GeneratorSource&) = delete;

    // Edited directly by the inspector. A new width/height takes effect at applyResize().
    GenSettings settings;

    void update(double dt) override { time_ += dt; render(); }
    GLuint texture() const override { return target_.tex; }
    int width()  const override { return target_.w; }
    int height() const override { return target_.h; }

    double time() const { return time_; }
    void setTime(double t) { time_ = t; render(); }
    void restart() { setTime(0.0); }

    // True when the settings ask for a different picture size than the texture has.
    bool wantsResize() const { return settings.width != target_.w || settings.height != target_.h; }
    // Recreates the texture at the new size. Frees the old texture, so call it between frames.
    void applyResize() {
        if (!wantsResize()) return;
        destroyRenderTarget(target_);
        target_ = makeRenderTarget(settings.width, settings.height);
        textKey_ = "\x01";                            // the test card's resolution label changes too
        render();
    }

    // The text shown (for the tests and the inspector).
    std::string currentText() const {
        if (settings.type == GenType::Text) return settings.text("text");
        if (settings.type == GenType::TestCard) {
            std::string s = settings.text("label");
            if (settings.num("showResolution") >= 0.5f) s += (s.empty() ? "" : "\n") + std::to_string(settings.width) + " x " + std::to_string(settings.height);
            return s;
        }
        return "";
    }

private:
    void updateText() {
        if (settings.type != GenType::Text && settings.type != GenType::TestCard) return;
        const TextFont font = (settings.type == GenType::Text && settings.num("font") >= 0.5f) ? TextFont::Pixel : TextFont::Sans;
        const TextAlign align = settings.type == GenType::Text ? (TextAlign)(int)std::lround(settings.num("align")) : TextAlign::Center;
        const std::string text = currentText();
        const std::string key = text + '\0' + (char)('0' + (int)font) + (char)('0' + (int)align);
        if (key == textKey_) return;
        textKey_ = key;
        const TextImage img = rasterizeText(text, font, align);
        textW_ = img.w; textH_ = img.h;
        if (img.empty()) return;
        if (!textTex_) glGenTextures(1, &textTex_);
        glBindTexture(GL_TEXTURE_2D, textTex_);
        GLint oldAlign = 4; glGetIntegerv(GL_UNPACK_ALIGNMENT, &oldAlign);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, img.w, img.h, 0, GL_RED, GL_UNSIGNED_BYTE, img.alpha.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, oldAlign);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    void render() {
        updateText();
        glBindFramebuffer(GL_FRAMEBUFFER, target_.fbo);
        glViewport(0, 0, target_.w, target_.h);
        glDisable(GL_BLEND);
        glUseProgram(prog_);
        glUniform2f(glGetUniformLocation(prog_, "uRes"), (float)target_.w, (float)target_.h);
        glUniform1f(glGetUniformLocation(prog_, "uTime"), (float)time_);
        glUniform1f(glGetUniformLocation(prog_, "uPhase"), (float)strobePhase(time_, settings.num("rate")));
        const bool hasText = textTex_ && textH_ > 0;
        glUniform2f(glGetUniformLocation(prog_, "uTextSize"), hasText ? (float)textW_ : 0.0f, hasText ? (float)textH_ : 0.0f);
        glUniform1i(glGetUniformLocation(prog_, "uText"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hasText ? textTex_ : 0);
        for (const GenParamDef& d : genTypeInfo(settings.type).params) {
            if (d.kind == GenParamKind::Text) continue;
            const GLint loc = glGetUniformLocation(prog_, (std::string("u_") + d.key).c_str());
            if (loc < 0) continue;                                       // a parameter this shader doesn't need
            if (d.kind == GenParamKind::Color) { const GenColor c = settings.color(d.key); glUniform4f(loc, c[0], c[1], c[2], c[3]); }
            else glUniform1f(loc, settings.num(d.key));
        }
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    GLuint prog_ = 0, vao_ = 0, textTex_ = 0;
    RenderTarget target_;
    double time_ = 0.0;
    std::string textKey_ = "\x01";
    int textW_ = 0, textH_ = 0;
};
