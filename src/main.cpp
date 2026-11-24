#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "Audio.h"
#include "AudioSync.h"
#include "Editor.h"
#include "GLUtil.h"
#include "Params.h"
#include "OutputController.h"
#include "Project.h"
#include "Renderer.h"
#include "UiFont.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// MapHappy can't start: say why (on Windows there is no console to say it in).
static int cannotStart(const char* why) {
    std::fprintf(stderr, "%s\n", why);
#ifdef _WIN32
    MessageBoxA(nullptr, why, "MapHappy", MB_OK | MB_ICONERROR);
#endif
    return 1;
}

// Path of a file dropped onto the window. GLFW calls this from glfwPollEvents().
static std::string g_droppedPath;
static bool g_closeRequested = false;
static void onClose(GLFWwindow* w) { glfwSetWindowShouldClose(w, GLFW_FALSE); g_closeRequested = true; }   // the editor decides (it may ask about unsaved changes)
static void onDrop(GLFWwindow*, int count, const char** paths) {
    if (count > 0) g_droppedPath = paths[0];
}

// Everything that owns OpenGL objects lives in here, so it is all destroyed
// BEFORE main() tears down the OpenGL context.
static void run(GLFWwindow* window) {
    Renderer renderer;
    Project  project;
    RenderTarget stageRT = makeRenderTarget(project.outW, project.outH);
    Editor editor(project, stageRT);

    AudioEngine audio;                                    // the sound card; also the show's master clock
    {
        std::string err;
        if (!audio.start(err)) std::fprintf(stderr, "Audio is off: %s\n", err.c_str());
    }
    std::shared_ptr<const MixSnapshot> lastMix;

    project.addSurface(Shape::Quad);     // start with one surface so there is something to see
    editor.markClean();

    OutputState outState;                                 // the projector output window: its settings and status
    OutputController outCtl(window, outState);
    editor.attachOutput(&outState);
    editor.startupRecovery();            // offers to bring back work from a crashed session
    std::string lastTitle;
    double lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        double now = glfwGetTime();
        double dt  = now - lastTime;
        lastTime   = now;

        if (!g_droppedPath.empty()) { editor.onFileDropped(g_droppedPath); g_droppedPath.clear(); }
        if (g_closeRequested) { g_closeRequested = false; editor.requestExit(); }
        if (editor.exitApproved()) glfwSetWindowShouldClose(window, GLFW_TRUE);
        const std::string title = editor.windowTitle();
        if (title != lastTitle) { glfwSetWindowTitle(window, title.c_str()); lastTitle = title; }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        editor.drawUI();
        ImGui::Render();

        if (audio.available()) syncMixSnapshot(project, audio.mixer(), lastMix);
        driveTransport(project.transport, audio.available() ? &audio.mixer() : nullptr, dt, project.timeline.length);   // the master clock
        if (project.transport.playing || project.timelineDirty) {    // timeline writes its values into the project
            applyTimeline(project);
            project.timelineDirty = false;
        }
        project.media.update(dt);                 // advance generators and video
        renderer.renderStage(stageRT, project);   // draw all surfaces into the stage texture
        outCtl.update(project, stageRT);          // show it on the projector (also handles that window's keys)

        int fbw, fbh;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);

        editor.endFrame();                        // deferred deletions
        ensureStageSize(stageRT, project);        // a new output resolution recreates the stage texture (between frames, so nothing points at the old one)

        // The projector window's swap waits for its display; if it can't (minimised, hidden) don't spin the CPU
        if (outState.isOpen && glfwGetTime() - now < 0.004) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    destroyRenderTarget(stageRT);
}   // project (and every texture it owns) is destroyed here, while the OpenGL context still exists

int main() {
    if (!glfwInit()) return cannotStart("MapHappy couldn't start its window system (GLFW).");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1600, 900, "MapHappy", nullptr, nullptr);
    if (!window) { glfwTerminate(); return cannotStart("MapHappy couldn't open its window. It needs a graphics card and driver with OpenGL 3.3."); }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    {                                             // the logo, for the title bar and the task bar (macOS uses the app bundle's instead)
        std::vector<unsigned char> small = logoPixels(16), middle = logoPixels(32), big = logoPixels(64);
        GLFWimage icons[3] = {{16, 16, small.data()}, {32, 32, middle.data()}, {64, 64, big.data()}};
#ifndef __APPLE__
        glfwSetWindowIcon(window, 3, icons);
#else
        (void)icons;
#endif
    }
    glfwSetDropCallback(window, onDrop);
    glfwSetWindowCloseCallback(window, onClose);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        return cannotStart("MapHappy couldn't load OpenGL. Try updating the graphics driver.");
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;         // panels are fixed, so there is nothing to remember
    applyTheme();
    loadUiFont();
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
