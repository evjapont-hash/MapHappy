#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "Homography.h"
#include "GLUtil.h"
#include "PatternSource.h"
#include "VideoSource.h"
#include <cmath>
#include <cstdio>
#include <string>

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

// Path of a file dropped onto the window. GLFW calls this from glfwPollEvents().
static std::string g_droppedPath;
static void onDrop(GLFWwindow*, int count, const char** paths) {
    if (count > 0) g_droppedPath = paths[0];
}

enum class Source { Pattern, Video };

// Everything that owns OpenGL objects lives in here, so it is all destroyed
// BEFORE main() tears down the OpenGL context.
static void run(GLFWwindow* window) {
    GLuint warpProg    = makeProgram(kFullscreenVS, kWarpFS);
    GLint  locWarpTex  = glGetUniformLocation(warpProg, "uTex");
    GLint  locWarpInvH = glGetUniformLocation(warpProg, "uInvH");
    GLint  locWarpRes  = glGetUniformLocation(warpProg, "uRes");
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);

    PatternSource pattern;
    VideoSource   video;
    Source current = Source::Pattern;
    bool   playing = true, loop = true;
    char   pathBuf[512] = "";

    auto loadVideo = [&](std::string p) {
        // "Copy as path" in Windows Explorer wraps the path in quotes; strip them.
        while (!p.empty() && (p.front() == '"' || p.front() == ' ')) p.erase(p.begin());
        while (!p.empty() && (p.back()  == '"' || p.back()  == ' ')) p.pop_back();
        std::snprintf(pathBuf, sizeof(pathBuf), "%s", p.c_str());
        video.setLoop(loop);
        if (video.open(p)) current = Source::Video;
        else if (!video.isOpen()) current = Source::Pattern;
    };

    // Corner positions are normalized window coordinates: (0,0) top-left, (1,1) bottom-right.
    const Vec2 kDefaultCorners[4] = {{0.2f, 0.2f}, {0.8f, 0.2f}, {0.8f, 0.8f}, {0.2f, 0.8f}};
    Vec2 corners[4];
    for (int i = 0; i < 4; i++) corners[i] = kDefaultCorners[i];
    int   dragging = -1;
    bool  showHandles = true;
    float bg[3] = {0.08f, 0.08f, 0.10f};
    Mat3  invH;
    double lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        double now = glfwGetTime();
        double dt  = now - lastTime;
        lastTime   = now;

        if (!g_droppedPath.empty()) { loadVideo(g_droppedPath); g_droppedPath.clear(); }

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
                    if (!isConvex(corners)) corners[dragging] = old;
                } else {
                    dragging = -1;
                }
            }
        }

        // ---- Update the transform ----
        Mat3 h, inv;
        if (squareToQuad(corners, h) && invert(h, inv)) invH = inv;

        // ---- UI panel ----
        ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
        ImGui::Begin("MapHappy");

        ImGui::SeparatorText("Source");
        if (ImGui::RadioButton("Test pattern", current == Source::Pattern)) current = Source::Pattern;
        ImGui::SameLine();
        if (ImGui::RadioButton("Video", current == Source::Video) && video.isOpen()) current = Source::Video;

        ImGui::InputText("##path", pathBuf, sizeof(pathBuf));
        ImGui::SameLine();
        if (ImGui::Button("Load")) loadVideo(pathBuf);
        ImGui::TextDisabled("Paste a path, or drag a video file onto the window");

        if (!video.error().empty()) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", video.error().c_str());
        if (video.isOpen()) {
            ImGui::Text("%dx%d  %.2f fps  %.1f s", video.width(), video.height(), video.fps(), video.time());
            ImGui::Checkbox("Playing", &playing);
            ImGui::SameLine();
            ImGui::Checkbox("Loop", &loop);
            ImGui::SameLine();
            if (ImGui::Button("Restart")) loadVideo(video.path());
        }

        ImGui::SeparatorText("Surface");
        ImGui::Checkbox("Show handles", &showHandles);
        ImGui::SameLine();
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

        // ---- Update whichever source is active ----
        ContentSource* active = &pattern;
        double sourceDt = dt;
        if (current == Source::Video && video.isOpen()) {
            active = &video;
            video.setLoop(loop);
            sourceDt = playing ? dt : 0.0;     // pausing just stops the clock
        }
        active->update(sourceDt);

        // ---- Draw the active source's texture, warped, to the window ----
        int fbw, fbh;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbw, fbh);
        glClearColor(bg[0], bg[1], bg[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(warpProg);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, active->texture());
        glUniform1i(locWarpTex, 0);
        glUniformMatrix3fv(locWarpInvH, 1, GL_TRUE, invH.m);   // GL_TRUE: our matrix is row-major
        glUniform2f(locWarpRes, (float)fbw, (float)fbh);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // ---- UI on top ----
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(warpProg);
}   // pattern and video are destroyed here, while the OpenGL context still exists

int main() {
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }

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
    glfwSetDropCallback(window, onDrop);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::fprintf(stderr, "GLAD load failed\n"); return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    run(window);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
