#pragma once
// A harness that runs the REAL editor (Editor, Conductor, renderer, ...) in a window and drives it with scripted
// mouse and keyboard events, one step per frame, then checks the result.
//
// Lessons built in:
//  * ImGui's GLFW backend re-reads the real mouse pointer every frame, so our fake pointer position is injected
//    AFTER it, and re-sent every frame, like a real mouse resting where it was left.
//  * Anything that deletes GPU textures happens between frames (Editor::endFrame), same as in the app.
//  * Positions are looked up lazily (when a step runs), so layout changes never break a script.
#include "GlTestSupport.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "Editor.h"
#include "Renderer.h"
#include "Params.h"
#include "AudioSync.h"
#include "OutputController.h"
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <thread>

class UiTest {
public:
    using Pos = std::function<ImVec2()>;

    GlContext gl;
    TempDir home;                                   // settings, autosaves and projects go here, never into the real user folders
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<Project> project;
    RenderTarget stage;
    std::unique_ptr<Editor> editor;
    OutputState outState;
    std::unique_ptr<OutputController> outCtl;
    AudioMixer mixer;                               // stands in for the sound card when simulateAudio is on
    bool simulateAudio = false;
    std::vector<float> captured; bool capturing = false;
    static constexpr double kDt = 1.0 / 60.0;

    explicit UiTest(int w = 1600, int h = 900) : gl(w, h), home("ui") {
        if (!gl.ok) return;
        setTestEnv("HOME", home.str()); setTestEnv("XDG_CONFIG_HOME", home.file("cfg")); setTestEnv("APPDATA", home.file("cfg"));
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().MouseDoubleClickTime = 5.0f;     // the virtual display renders slowly; keep double-clicks reliable
        applyTheme();
        ImGui_ImplGlfw_InitForOpenGL(gl.window, true);
        ImGui_ImplOpenGL3_Init("#version 330");
        renderer = std::make_unique<Renderer>();
        project = std::make_unique<Project>();
        stage = makeRenderTarget(project->outW, project->outH);
        editor = std::make_unique<Editor>(*project, stage);
    }
    ~UiTest() {
        if (!gl.ok) return;
        outCtl.reset(); editor.reset(); destroyRenderTarget(stage); project.reset(); renderer.reset();
        ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
    }
    bool ok() const { return gl.ok; }

    // A flat-coloured picture to put on a surface.
    int addSolidMedia(unsigned char r, unsigned char g, unsigned char b) {
        auto m = std::make_unique<MediaItem>(); m->id = project->media.nextId(); project->media.ensureNextIdAbove(m->id);
        m->name = "solid"; m->kind = MediaKind::Generator; m->source = std::make_unique<SolidSource>(r, g, b, 8, 8);
        const int id = m->id; project->media.items.push_back(std::move(m)); return id;
    }
    // Pretend the app was closed and started again (settings and autosaves on disk are kept).
    void restart() {
        outCtl.reset(); editor.reset(); destroyRenderTarget(stage); project.reset();
        project = std::make_unique<Project>();
        stage = makeRenderTarget(project->outW, project->outH);
        editor = std::make_unique<Editor>(*project, stage);
        editor->markClean();                         // like main(): a freshly started app has nothing unsaved
    }
    // The same, but as a step in the script, at its place in the sequence. (Calling restart() while a script is being BUILT
    // would restart immediately, before any of the steps around it have run.)
    void restartStep() { act([this] { restart(); }); }
    ImGuiIO& io() { return ImGui::GetIO(); }

    // Show the projector output (needs a virtual desktop at least 3840 wide for the two made-up displays).
    void enableOutput(std::function<std::vector<MonitorInfo>()> monitors) {
        outCtl = std::make_unique<OutputController>(gl.window, outState);
        outCtl->monitorProvider = std::move(monitors);
        editor->attachOutput(&outState);
    }

    // ---------------------------------------------------------------- script
    void act(std::function<void()> fn)                    { steps_.push_back({Step::Act, std::move(fn), nullptr, "", 1, 0}); }
    void wait(int frames)                                  { steps_.push_back({Step::Wait, nullptr, nullptr, "", frames, 0}); }
    void check(const char* name, std::function<bool()> c)  { steps_.push_back({Step::Check, nullptr, std::move(c), name, 1, 0}); }
    void waitUntil(const char* name, std::function<bool()> c, int maxFrames = 400) { steps_.push_back({Step::WaitUntil, nullptr, std::move(c), name, maxFrames, 0}); }

    void mouseTo(Pos p)  { act([this, p] { mouse_ = p(); io().AddMousePosEvent(mouse_.x, mouse_.y); }); }
    void mouseTo(float x, float y) { mouseTo([=] { return ImVec2(x, y); }); }
    void down(int b = 0) { act([this, b] { io().AddMouseButtonEvent(b, true); }); }
    void up(int b = 0)   { act([this, b] { io().AddMouseButtonEvent(b, false); }); }
    void click(Pos p, int b = 0)  { mouseTo(p); down(b); up(b); wait(2); }
    void rclick(Pos p)            { click(p, 1); }
    void dclick(Pos p)            { mouseTo(p); down(); up(); down(); up(); wait(2); }
    void drag(Pos from, Pos to)   {
        mouseTo(from); down();
        mouseTo([=] { ImVec2 a = from(), b = to(); return ImVec2((a.x + b.x) / 2, (a.y + b.y) / 2); });
        mouseTo(to); up(); wait(2);
    }
    void key(ImGuiKey k)  { act([this, k] { io().AddKeyEvent(k, true); }); act([this, k] { io().AddKeyEvent(k, false); }); wait(1); }
    void ctrl(ImGuiKey k, bool shift = false) {
        act([this, shift] { io().AddKeyEvent(ImGuiMod_Ctrl, true); if (shift) io().AddKeyEvent(ImGuiMod_Shift, true); });
        act([this, k] { io().AddKeyEvent(k, true); });
        act([this, k] { io().AddKeyEvent(k, false); io().AddKeyEvent(ImGuiMod_Ctrl, false); io().AddKeyEvent(ImGuiMod_Shift, false); });
        wait(1);
    }
    void holdCtrl(bool on) { act([this, on] { io().AddKeyEvent(ImGuiMod_Ctrl, on); }); }
    void typeText(const std::string& t) { act([this, t] { io().AddInputCharactersUTF8(t.c_str()); }); wait(1); }
    void wheel(float notches) { for (int i = 0; i < 4; i++) act([this, notches] { io().AddMouseWheelEvent(0, notches / 4.0f); }); wait(2); }

    // ---------------------------------------------------------------- lazily evaluated positions
    static Pos at(float x, float y) { return [=] { return ImVec2(x, y); }; }
    Pos uiCenter(const std::string& name, float fx = 0.5f) {                 // a control the editor reported drawing ("slider:posX", "button:+ Track", ...)
        return [this, name, fx] { Editor::Rect r = editor->uiRect(name); return r.valid ? ImVec2(r.x + r.w * fx, r.y + r.h * 0.5f) : ImVec2(-1, -1); };
    }
    Pos stagePos(float fx, float fy) { return [this, fx, fy] { Editor::Rect r = editor->stageRect(); return ImVec2(r.x + r.w * fx, r.y + r.h * fy); }; }
    Pos inputPos(float fx, float fy) { return [this, fx, fy] { Editor::Rect r = editor->inputRect(); return ImVec2(r.x + r.w * fx, r.y + r.h * fy); }; }
    // Popup windows: ImGui names them "<label>###Menu_NN" (menus; NN = nesting depth), "##Popup_..." (popups, context menus), "##Combo_..." (dropdowns).
    static ImGuiWindow* topWindow(const char* prefix) {
        ImGuiWindow* best = nullptr;
        for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows) if (w->WasActive && std::strncmp(w->Name, prefix, std::strlen(prefix)) == 0) best = w;
        return best;
    }
    static ImGuiWindow* menuWindow(int level) {
        char suffix[16]; std::snprintf(suffix, sizeof(suffix), "###Menu_%02d", level);
        for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows) if (w->WasActive && std::strstr(w->Name, suffix)) return w;
        return nullptr;
    }
    static bool windowOpen(const char* title) { ImGuiWindow* w = ImGui::FindWindowByName(title); return w && w->WasActive; }
    static Pos popupItem(const char* prefix, float offset, float dx = 40) { return [=] { ImGuiWindow* w = topWindow(prefix); return w ? ImVec2(w->Pos.x + dx, w->Pos.y + 15 + offset) : ImVec2(-1, -1); }; }
    static Pos menuItem(int level, float offset, float dx = 40)         { return [=] { ImGuiWindow* w = menuWindow(level); return w ? ImVec2(w->Pos.x + dx, w->Pos.y + 15 + offset) : ImVec2(-1, -1); }; }
    // The buttons of an auto-sized modal dialog sit in one row at its bottom: 100 px wide, 8 px apart.
    static Pos modalButton(const char* title, int index) { return [=] { ImGuiWindow* w = ImGui::FindWindowByName(title); return w ? ImVec2(w->Pos.x + 8 + 50 + index * 108, w->Pos.y + w->Size.y - 8 - 10.5f) : ImVec2(-1, -1); }; }

    Px stagePixel(float fx, float fy) {                                       // fractions of the stage, from the top-left
        unsigned char d[4];
        glBindFramebuffer(GL_FRAMEBUFFER, stage.fbo);
        glReadPixels((int)(fx * stage.w), stage.h - 1 - (int)(fy * stage.h), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, d);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return Px{d[0], d[1], d[2]};
    }
    int fbWidth() const { int w, h; glfwGetFramebufferSize(gl.window, &w, &h); return w; }
    int fbHeight() const { int w, h; glfwGetFramebufferSize(gl.window, &w, &h); return h; }

    // ---------------------------------------------------------------- running
    void warmup(int frames = 6) { for (int i = 0; i < frames; i++) frame(nullptr); }
    void run() {
        int guard = 0;
        while (!steps_.empty() && guard++ < 200000) frame(&steps_.front());
        for (int i = 0; i < 3; i++) frame(nullptr);
    }

private:
    struct Step { enum Kind { Act, Wait, Check, WaitUntil } kind; std::function<void()> fn; std::function<bool()> cond; std::string name; int frames; int waited; };

    void frame(Step* step) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame();
        if (mouse_.x >= 0) io().AddMousePosEvent(mouse_.x, mouse_.y);        // after the backend's own pointer poll, so ours wins
        if (step && step->kind == Step::Act) step->fn();
        ImGui::NewFrame();
        editor->drawUI();
        ImGui::Render();

        if (simulateAudio) syncMixSnapshot(*project, mixer, lastMix_);
        driveTransport(project->transport, simulateAudio ? &mixer : nullptr, kDt, project->timeline.length);
        if (project->transport.playing || project->timelineDirty) { applyTimeline(*project); project->timelineDirty = false; }
        if (simulateAudio) {                                                  // the audio thread delivers one frame's worth of sound
            std::vector<float> out(2 * 480);
            uint32_t n = (uint32_t)(kDt * kAudioRate);
            while (n > 0) { uint32_t c = std::min<uint32_t>(n, 480); mixer.render(out.data(), c); if (capturing) captured.insert(captured.end(), out.begin(), out.begin() + c * 2); n -= c; }
        }
        project->media.update(kDt);
        renderer->renderStage(stage, *project);
        if (outCtl) outCtl->update(*project, stage);

        const int w = fbWidth(), h = fbHeight();
        glViewport(0, 0, w, h); glClearColor(0.07f, 0.07f, 0.08f, 1); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (step && step->kind == Step::Check && !getTestEnv("MAPHAPPY_TEST_SHOTS").empty()) { /* shots are taken explicitly via shot() */ }
        glfwSwapBuffers(gl.window);
        editor->endFrame();
        ensureStageSize(stage, *project);

        if (step) {
            switch (step->kind) {
                case Step::Act:       steps_.pop_front(); break;
                case Step::Check:     expect(step->name.c_str(), step->cond()); steps_.pop_front(); break;
                case Step::Wait:      if (--step->frames <= 0) steps_.pop_front(); break;
                case Step::WaitUntil:
                    if (step->cond()) steps_.pop_front();
                    else if (++step->waited >= step->frames) { expect((step->name + " (timed out waiting)").c_str(), false); steps_.pop_front(); }
                    break;
            }
        }
        const GLenum e = glGetError();
        if (e) { std::printf("FAIL  OpenGL error 0x%x during the test\n", e); testing_::failures()++; }
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }

    std::deque<Step> steps_;
    ImVec2 mouse_{-1, -1};
    std::shared_ptr<const MixSnapshot> lastMix_;
};
