// The projector output window, on two made-up 1920x1080 displays laid side by side on a virtual 3840x1080 desktop.
// Window creation, placement, shared-texture drawing and key handling are all real.
#include "GlTestSupport.h"
#include "OutputController.h"
#include <chrono>
#include <thread>

static Px at(const std::vector<unsigned char>& rb, int w, int x, int y) { const size_t i = ((size_t)y * w + x) * 3; return {rb[i], rb[i + 1], rb[i + 2]}; }   // y = 0 is the BOTTOM row

// bottom-left red, bottom-right blue, top-left green, top-right yellow
static void paintStage(const RenderTarget& rt) {
    glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo); glViewport(0, 0, rt.w, rt.h);
    glEnable(GL_SCISSOR_TEST);
    const int hw = rt.w / 2, hh = rt.h / 2;
    glScissor(0, 0, hw, hh);                 glClearColor(1, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glScissor(hw, 0, rt.w - hw, hh);         glClearColor(0, 0, 1, 1); glClear(GL_COLOR_BUFFER_BIT);
    glScissor(0, hh, hw, rt.h - hh);         glClearColor(0, 1, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glScissor(hw, hh, rt.w - hw, rt.h - hh); glClearColor(1, 1, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

int main() {
    GlContext gl(1600, 900, 100, 60); if (!gl.ok) return skipTest("no OpenGL 3.3 context available");
    GLFWwindow* editor = gl.window;
    std::vector<MonitorInfo> two = {{"Laptop", 0, 0, 1920, 1080, true, true}, {"Projector", 1920, 0, 1920, 1080, false, false}};
    {   // pure logic
        OutputState s;
        expect("automatic choice prefers the display without the editor", resolveMonitor(s, two) == 1);
        expect("with one display that display is chosen, with none there is no choice", resolveMonitor(s, {two[0]}) == 0 && resolveMonitor(s, {}) == -1);
        s.monitorName = "Laptop"; expect("a saved display name wins", resolveMonitor(s, two) == 0);
        std::vector<MonitorInfo> twins = {{"HDMI", 0, 0, 1920, 1080, true, true}, {"HDMI", 1920, 0, 1920, 1080, false, false}, {"HDMI", 3840, 0, 1280, 720, false, false}};
        s.monitorName = "HDMI"; s.monitorIndex = 2; expect("identical names: the saved index picks between them", resolveMonitor(s, twins) == 2);
        s.monitorIndex = 7; expect("a stale index falls back to the first display with that name", resolveMonitor(s, twins) == 0);
        s.monitorName = "Gone"; expect("a display that is no longer there falls back to automatic", resolveMonitor(s, two) == 1);
        OutputState m; m.mode = OutputMode::Auto;
        expect("auto mode: fullscreen on a display without the editor, windowed on the editor's own", wantsFullscreen(m, two, 1) && !wantsFullscreen(m, two, 0));
        m.mode = OutputMode::Fullscreen; expect("forced fullscreen is honoured", wantsFullscreen(m, two, 0));
        m.mode = OutputMode::Windowed;   expect("forced windowed is honoured", !wantsFullscreen(m, two, 1));
        ViewRect r = fitViewport(1920, 1080, 1920, 1080, OutputScale::Fit);
        expect("a picture of the same shape fills the window", r.x == 0 && r.y == 0 && r.w == 1920 && r.h == 1080);
        r = fitViewport(1920, 1080, 800, 800, OutputScale::Fit);
        expect("a square picture is centred with side bars", r.w == 1080 && r.h == 1080 && r.x == 420 && r.y == 0);
        r = fitViewport(1920, 1080, 3840, 800, OutputScale::Fit);
        expect("a very wide picture gets top and bottom bars", r.w == 1920 && r.h == 400 && r.y == 340);
        r = fitViewport(1920, 1080, 800, 800, OutputScale::Stretch);
        expect("stretch fills the window", r.x == 0 && r.y == 0 && r.w == 1920 && r.h == 1080);
        expect("a zero-size window can't divide by zero", fitViewport(0, 0, 100, 100, OutputScale::Fit).w == 0);
        const auto real = queryMonitors();
        expect("the real display query returns at least one usable display", !real.empty() && real[0].w > 0 && real[0].h > 0 && !real[0].name.empty());
    }

    // The real window needs a desktop wide enough to hold the second made-up display
    const auto real = queryMonitors();
    if (real.empty() || real[0].w < 3840) { std::printf("   (desktop is %d px wide: skipping the window tests, they need 3840)\n", real.empty() ? 0 : real[0].w); return finishTest(); }

    Project proj; RenderTarget stage = makeRenderTarget(1280, 720); paintStage(stage);
    OutputState st; st.wanted = true;
    OutputController ctl(editor, st);
    ctl.monitorProvider = [&] { return two; };
    std::vector<unsigned char> rb;

    ctl.captureNext = &rb; ctl.update(proj, stage);
    expect("asking for output opens a window and the editor's context is current again", st.isOpen && ctl.window().isOpen() && glfwGetCurrentContext() == editor);
    expect("the projector display was chosen, fullscreen (auto mode)", st.activeMonitor == 1 && st.isFullscreen && st.monitors[0].hasEditor && !st.monitors[1].hasEditor);
    GLFWwindow* w = ctl.window().handle();
    int wx, wy, ww, wh; glfwGetWindowPos(w, &wx, &wy); glfwGetWindowSize(w, &ww, &wh);
    expect("the window sits exactly on the projector's display", wx == 1920 && wy == 0 && ww == 1920 && wh == 1080);
    expect("it is borderless, stays on top, and hides the mouse pointer", glfwGetWindowAttrib(w, GLFW_DECORATED) == GLFW_FALSE && glfwGetWindowAttrib(w, GLFW_FLOATING) == GLFW_TRUE && glfwGetInputMode(w, GLFW_CURSOR) == GLFW_CURSOR_HIDDEN);
    expect("its real pixel size is reported", st.fbW == 1920 && st.fbH == 1080);
    expect("the captured frame has the window's size", rb.size() == (size_t)1920 * 1080 * 3);
    expect("the stage reaches the projector the right way up (red bottom-left, blue bottom-right, green top-left, yellow top-right)",
           isColor(at(rb, 1920, 200, 200), 255, 0, 0) && isColor(at(rb, 1920, 1700, 200), 0, 0, 255) && isColor(at(rb, 1920, 200, 900), 0, 255, 0) && isColor(at(rb, 1920, 1700, 900), 255, 255, 0));
    expect("the quadrant seam is exactly where it should be", isColor(at(rb, 1920, 958, 200), 255, 0, 0) && isColor(at(rb, 1920, 962, 200), 0, 0, 255));

    RenderTarget square = makeRenderTarget(800, 800); paintStage(square);
    st.scale = OutputScale::Fit; ctl.captureNext = &rb; ctl.update(proj, square);
    expect("Fit: a square picture leaves black side bars", isColor(at(rb, 1920, 100, 500), 0, 0, 0, 0) && isColor(at(rb, 1920, 1800, 500), 0, 0, 0, 0));
    expect("Fit: the picture sits in the middle, undistorted", isColor(at(rb, 1920, 700, 200), 255, 0, 0) && isColor(at(rb, 1920, 1200, 200), 0, 0, 255) && isColor(at(rb, 1920, 700, 800), 0, 255, 0) && isColor(at(rb, 1920, 1200, 800), 255, 255, 0));
    expect("Fit: the picture's edges are crisp", isColor(at(rb, 1920, 421, 200), 255, 0, 0) && isColor(at(rb, 1920, 419, 200), 0, 0, 0, 0) && isColor(at(rb, 1920, 1499, 200), 0, 0, 255) && isColor(at(rb, 1920, 1501, 200), 0, 0, 0, 0));
    st.scale = OutputScale::Stretch; ctl.captureNext = &rb; ctl.update(proj, square);
    expect("Stretch: the picture fills the whole window", isColor(at(rb, 1920, 100, 200), 255, 0, 0) && isColor(at(rb, 1920, 1800, 900), 255, 255, 0));
    st.scale = OutputScale::Fit;

    st.blackout = true; ctl.captureNext = &rb; ctl.update(proj, stage);
    bool black = true; for (unsigned char c : rb) if (c) { black = false; break; }
    expect("blackout shows pure black on every pixel", black);
    st.blackout = false; ctl.captureNext = &rb; ctl.update(proj, stage);
    expect("lifting the blackout brings the picture straight back", isColor(at(rb, 1920, 200, 200), 255, 0, 0));

    OutputWindow::onKey(w, GLFW_KEY_B, 0, GLFW_PRESS, 0); ctl.update(proj, stage);
    expect("B in the output window toggles blackout", st.blackout);
    OutputWindow::onKey(w, GLFW_KEY_B, 0, GLFW_RELEASE, 0); ctl.update(proj, stage);
    expect("releasing a key does nothing", st.blackout);
    OutputWindow::onKey(w, GLFW_KEY_B, 0, GLFW_PRESS, 0); ctl.update(proj, stage);
    expect("B again lifts it", !st.blackout);
    const bool was = proj.transport.playing; OutputWindow::onKey(w, GLFW_KEY_SPACE, 0, GLFW_PRESS, 0); ctl.update(proj, stage);
    expect("Space in the output window plays and pauses the show", proj.transport.playing != was);
    proj.transport.time = 12.5; OutputWindow::onKey(w, GLFW_KEY_M, 0, GLFW_PRESS, 0); ctl.update(proj, stage);
    expect("M in the output window drops a marker at the playhead", proj.timeline.markers.size() == 1 && proj.timeline.markers[0].time == 12.5);

    st.mode = OutputMode::Windowed; ctl.update(proj, stage);
    w = ctl.window().handle(); glfwGetWindowPos(w, &wx, &wy); glfwGetWindowSize(w, &ww, &wh);
    expect("windowed mode: a normal framed window of preview size, placed on the chosen display", !st.isFullscreen && glfwGetWindowAttrib(w, GLFW_DECORATED) == GLFW_TRUE && glfwGetWindowAttrib(w, GLFW_FLOATING) == GLFW_FALSE && ww == 960 && wh == 540 && wx >= 1920);
    st.mode = OutputMode::Auto; st.monitorName = "Laptop"; ctl.update(proj, stage);
    expect("auto mode on the editor's own display stays windowed (never covers the editor)", st.isOpen && !st.isFullscreen && st.activeMonitor == 0);
    st.mode = OutputMode::Fullscreen; ctl.update(proj, stage); glfwGetWindowPos(ctl.window().handle(), &wx, &wy);
    expect("forced fullscreen on the editor's display covers it", st.isFullscreen && wx == 0 && wy == 0);
    st.mode = OutputMode::Auto; st.monitorName = "Projector"; ctl.update(proj, stage);
    expect("moving back to the projector reopens it there, fullscreen", st.isFullscreen && st.activeMonitor == 1);

    OutputWindow::onKey(ctl.window().handle(), GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0); ctl.update(proj, stage);
    expect("Esc in the output window closes it", !st.wanted && !st.isOpen && !ctl.window().isOpen());
    st.wanted = true; ctl.update(proj, stage);
    expect("it can be opened again", st.isOpen);
    OutputWindow::onClose(ctl.window().handle()); ctl.update(proj, stage);
    expect("the window's own close button closes it too", !st.wanted && !st.isOpen);
    st.wanted = true; ctl.update(proj, stage);
    OutputWindow::onKey(ctl.window().handle(), GLFW_KEY_F11, 0, GLFW_PRESS, 0); ctl.update(proj, stage);
    expect("F11 closes it as well", !st.isOpen);

    st.wanted = true; ctl.update(proj, stage);
    ctl.monitorProvider = [&] { return std::vector<MonitorInfo>{two[0]}; };
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));           // displays are rescanned once a second
    ctl.update(proj, stage);
    expect("unplugging the projector closes the output window cleanly and says why", !st.isOpen && !st.wanted && st.message.find("disconnected") != std::string::npos);
    ctl.monitorProvider = [&] { return std::vector<MonitorInfo>{}; };
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    st.wanted = true; ctl.update(proj, stage);
    expect("with no displays at all, asking for output fails gracefully", !st.wanted && !st.isOpen && st.message.find("No display") != std::string::npos);

    proj.outW = 2560; proj.outH = 1440; ensureStageSize(stage, proj);
    expect("a new output resolution recreates the stage at that size", stage.w == 2560 && stage.h == 1440 && stage.tex && stage.fbo);
    proj.outW = 100000; proj.outH = 5; ensureStageSize(stage, proj);
    expect("absurd sizes are clamped to what the GPU can do", stage.w <= 8192 && stage.w >= 64 && stage.h == 64 && proj.outW == stage.w);
    expect("no OpenGL errors", glGetError() == GL_NO_ERROR);
    destroyRenderTarget(stage); destroyRenderTarget(square); ctl.window().close();
    return finishTest();
}
