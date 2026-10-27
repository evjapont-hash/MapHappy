#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "Homography.h"
#include <cstdio>
#include <cmath>
extern "C" {
#include <libavformat/avformat.h>
}

// ---------------------------------------------------------------------------
// Shaders. GLSL lives in C++ raw strings so there are no extra files to ship.
// ---------------------------------------------------------------------------

// Draws one giant triangle that covers the screen. No vertex buffer needed.
static const char* kFullscreenVS = R"GLSL(#version 330 core
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

// Procedural test pattern. Rendered into an off-screen texture each frame.
static const char* kPatternFS = R"GLSL(#version 330 core
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

// For every screen pixel, use the INVERSE homography to ask "which pixel of the
// content image lands here?" Outside the quad the answer is outside [0,1], so discard.
static const char* kWarpFS = R"GLSL(#version 330 core
uniform sampler2D uTex;
uniform mat3 uInvH;
uniform vec2 uRes;
out vec4 frag;
void main() {
    vec2 p = vec2(gl_FragCoord.x / uRes.x, 1.0 - gl_FragCoord.y / uRes.y);
    vec3 q = uInvH * vec3(p, 1.0);
    vec2 uv = q.xy / q.z;                                   // the perspective divide
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) discard;
    frag = texture(uTex, vec2(uv.x, 1.0 - uv.y));
}
)GLSL";

// ---------------------------------------------------------------------------
// Small OpenGL helpers
// ---------------------------------------------------------------------------
static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader compile error:\n%s\n", log);
    }
    return s;
}

static GLuint makeProgram(const char* vsSrc, const char* fsSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Program link error:\n%s\n", log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

struct RenderTarget { GLuint fbo = 0, tex = 0; int w = 0, h = 0; };

static RenderTarget makeRenderTarget(int w, int h) {
    RenderTarget t;
    t.w = w; t.h = h;
    glGenTextures(1, &t.tex);
    glBindTexture(GL_TEXTURE_2D, t.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "Framebuffer is not complete\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return t;
}

// ---------------------------------------------------------------------------
int main() {
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    std::printf("FFmpeg libavformat version: %u\n", avformat_version());

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 720, "MapHappy", nullptr, nullptr);
    if (!window) { std::fprintf(stderr, "Window creation failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::fprintf(stderr, "GLAD load failed\n"); return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // GPU resources
    GLuint patternProg = makeProgram(kFullscreenVS, kPatternFS);
    GLuint warpProg    = makeProgram(kFullscreenVS, kWarpFS);
    GLint  locPatRes   = glGetUniformLocation(patternProg, "uRes");
    GLint  locPatTime  = glGetUniformLocation(patternProg, "uTime");
    GLint  locWarpTex  = glGetUniformLocation(warpProg, "uTex");
    GLint  locWarpInvH = glGetUniformLocation(warpProg, "uInvH");
    GLint  locWarpRes  = glGetUniformLocation(warpProg, "uRes");

    GLuint vao = 0;                       // core profile requires a VAO bound, even an empty one
    glGenVertexArrays(1, &vao);

    RenderTarget content = makeRenderTarget(1280, 720);

    // Corner positions are normalized window coordinates: (0,0) top-left, (1,1) bottom-right.
    const Vec2 kDefaultCorners[4] = {{0.2f, 0.2f}, {0.8f, 0.2f}, {0.8f, 0.8f}, {0.2f, 0.8f}};
    Vec2 corners[4];
    for (int i = 0; i < 4; i++) corners[i] = kDefaultCorners[i];
    int  dragging = -1;
    bool showHandles = true;
    float bg[3] = {0.08f, 0.08f, 0.10f};
    Mat3 invH;                            // identity until the first valid update

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGuiIO& io = ImGui::GetIO();
        float winW = io.DisplaySize.x, winH = io.DisplaySize.y;

        // ---- Corner dragging ----
        if (ImGui::IsMousePosValid()) {
            Vec2 mouse = {io.MousePos.x / winW, io.MousePos.y / winH};

            if (!io.WantCaptureMouse && ImGui::IsMouseClicked(0)) {
                float best = 20.0f;       // pick radius in pixels
                for (int i = 0; i < 4; i++) {
                    float dx = (corners[i].x - mouse.x) * winW;
                    float dy = (corners[i].y - mouse.y) * winH;
                    float dist = std::sqrt(dx * dx + dy * dy);
                    if (dist < best) { best = dist; dragging = i; }
                }
            }
            if (dragging >= 0) {
                if (ImGui::IsMouseDown(0)) {
                    Vec2 old = corners[dragging];
                    corners[dragging] = mouse;
                    if (!isConvex(corners)) corners[dragging] = old;  // reject invalid shapes
                } else {
                    dragging = -1;
                }
            }
        }

        // ---- Update the transform ----
        Mat3 h, inv;
        if (squareToQuad(corners, h) && invert(h, inv)) invH = inv;

        // ---- UI panel ----
        ImGui::Begin("MapHappy");
        ImGui::Checkbox("Show handles", &showHandles);
        if (ImGui::Button("Reset corners")) {
            for (int i = 0; i < 4; i++) corners[i] = kDefaultCorners[i];
        }
        ImGui::ColorEdit3("Background", bg);
        ImGui::Text("%.1f FPS", io.Framerate);
        ImGui::End();

        // ---- Corner handles (drawn by ImGui on top of everything) ----
        if (showHandles) {
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            ImVec2 pts[4];
            for (int i = 0; i < 4; i++) pts[i] = ImVec2(corners[i].x * winW, corners[i].y * winH);
            dl->AddPolyline(pts, 4, IM_COL32(255, 255, 255, 200), ImDrawFlags_Closed, 2.0f);
            for (int i = 0; i < 4; i++) {
                ImU32 fill = (i == dragging) ? IM_COL32(255, 200, 0, 255) : IM_COL32(0, 200, 255, 255);
                dl->AddCircleFilled(pts[i], 7.0f, fill);
                dl->AddCircle(pts[i], 7.0f, IM_COL32(0, 0, 0, 255), 0, 2.0f);
            }
        }

        ImGui::Render();

        // ---- Pass 1: draw the test pattern into the off-screen texture ----
        glBindFramebuffer(GL_FRAMEBUFFER, content.fbo);
        glViewport(0, 0, content.w, content.h);
        glUseProgram(patternProg);
        glUniform2f(locPatRes, (float)content.w, (float)content.h);
        glUniform1f(locPatTime, (float)glfwGetTime());
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // ---- Pass 2: draw that texture, warped, to the window ----
        int fbw, fbh;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbw, fbh);
        glClearColor(bg[0], bg[1], bg[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(warpProg);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, content.tex);
        glUniform1i(locWarpTex, 0);
        glUniformMatrix3fv(locWarpInvH, 1, GL_TRUE, invH.m);   // GL_TRUE: our matrix is row-major
        glUniform2f(locWarpRes, (float)fbw, (float)fbh);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // ---- UI on top ----
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}