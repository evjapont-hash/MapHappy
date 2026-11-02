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
#include "Project.h"
#include "Renderer.h"
#include <cstdio>
#include <string>

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

        int fbw, fbh;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.07f, 0.07f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);

        editor.endFrame();                        // deferred deletions
    }

    destroyRenderTarget(stageRT);
}   // project (and every texture it owns) is destroyed here, while the OpenGL context still exists

int main() {
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1600, 900, "MapHappy", nullptr, nullptr);
    if (!window) { std::fprintf(stderr, "Window creation failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetDropCallback(window, onDrop);
    glfwSetWindowCloseCallback(window, onClose);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::fprintf(stderr, "GLAD load failed\n"); return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;         // panels are fixed, so there is nothing to remember
    applyTheme();
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
