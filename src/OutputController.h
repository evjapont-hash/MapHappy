#pragma once
#include "OutputWindow.h"
#include "Project.h"
#include <functional>
#include <string>
#include <vector>

// Keeps the output window in step with OutputState every frame: opens it, closes it, moves it when the
// display or mode changes, relays its keys to the show, and copes with displays being unplugged.
class OutputController {
public:
    OutputController(GLFWwindow* editorWindow, OutputState& st) : editor_(editorWindow), st_(st) {}

    // Where display information comes from. Tests replace this with made-up displays.
    std::function<std::vector<MonitorInfo>()> monitorProvider;
    // Tests: if set, the next frame drawn into the output window is copied here (then this is cleared).
    std::vector<unsigned char>* captureNext = nullptr;

    OutputWindow& window() { return win_; }

    // Call once per frame, after the stage has been drawn. Leaves the editor's context current.
    void update(Project& p, const RenderTarget& stage) {
        glFlush();                                            // make the freshly drawn stage visible to the other window's context
        const double now = glfwGetTime();
        if (now - lastScan_ > 1.0 || firstScan_) { refreshMonitors(); lastScan_ = now; firstScan_ = false; }

        const int idx = resolveMonitor(st_, st_.monitors);
        st_.activeMonitor = idx;

        if (win_.isOpen()) handleWindowEvents(p);             // keys and the close button, even while reopening

        if (!st_.wanted || idx < 0) {
            if (st_.wanted && idx < 0) st_.message = "No display found";
            st_.wanted = false;
            if (win_.isOpen()) win_.close();
        } else {
            const bool fullscreen = wantsFullscreen(st_, st_.monitors, idx);
            const std::string key = monitorKey(st_.monitors[idx]);
            if (!win_.isOpen() || key != openedKey_ || fullscreen != openedFullscreen_) {
                const int pw = 960, ph = std::max(16, (int)(960.0 * stage.h / std::max(1, stage.w) + 0.5));
                std::string err;
                if (!win_.open(editor_, st_.monitors[idx], fullscreen, pw, ph, err)) { st_.message = err; st_.wanted = false; }
                else { openedKey_ = key; openedFullscreen_ = fullscreen; st_.message.clear(); }
            }
        }

        st_.isOpen = win_.isOpen();
        st_.isFullscreen = win_.isOpen() && win_.isFullscreen();
        if (win_.isOpen()) {
            win_.framebufferSize(st_.fbW, st_.fbH);
            std::vector<unsigned char>* cap = captureNext; captureNext = nullptr;
            win_.render(stage.tex, stage.w, stage.h, st_.blackout, st_.scale, cap);
        } else { st_.fbW = st_.fbH = 0; }

        // While the projector window is open it sets the pace (its swap waits for the projector's refresh),
        // so the editor window must not also wait. When it is closed the editor waits for its own display again.
        const bool wantVsyncOff = win_.isOpen();
        if (wantVsyncOff != mainVsyncOff_ || firstVsync_) {
            glfwSwapInterval(wantVsyncOff ? 0 : 1);
            mainVsyncOff_ = wantVsyncOff; firstVsync_ = false;
        }
    }

private:
    static std::string monitorKey(const MonitorInfo& m) {
        return m.name + "|" + std::to_string(m.x) + "," + std::to_string(m.y) + "," + std::to_string(m.w) + "x" + std::to_string(m.h);
    }

    void refreshMonitors() {
        std::vector<MonitorInfo> list = monitorProvider ? monitorProvider() : queryMonitors();
        markEditorMonitor(editor_, list);
        // an unplugged projector: close its window cleanly instead of leaving it stranded
        if (win_.isOpen()) {
            bool still = false;
            for (const auto& m : list) if (monitorKey(m) == openedKey_) still = true;
            if (!still) {
                win_.close();
                st_.wanted = false;
                st_.message = "The output display was disconnected";
            }
        }
        st_.monitors = std::move(list);
    }

    void handleWindowEvents(Project& p) {
        OutputCommands c = win_.takeCommands();
        if (c.close) st_.wanted = false;
        if (c.blackout) st_.blackout = !st_.blackout;
        if (c.playPause) p.transport.playing = !p.transport.playing;
        if (c.marker) p.timeline.addMarker(p.transport.time);
        if (win_.closeRequested()) st_.wanted = false;
    }

    GLFWwindow* editor_;
    OutputState& st_;
    OutputWindow win_;
    std::string openedKey_;
    bool openedFullscreen_ = false;
    double lastScan_ = 0.0;
    bool firstScan_ = true, mainVsyncOff_ = false, firstVsync_ = true;
};

// Makes the stage texture match the project's output size (e.g. after choosing a new resolution).
// Call between frames: it deletes the old texture.
inline void ensureStageSize(RenderTarget& rt, Project& p) {
    GLint maxTex = 4096;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    maxTex = std::min<GLint>(maxTex, 8192);                    // 8192 x 8192 x 4 bytes is already 256 MB
    p.outW = std::max(64, std::min((int)maxTex, p.outW));
    p.outH = std::max(64, std::min((int)maxTex, p.outH));
    if (rt.w != p.outW || rt.h != p.outH) {
        destroyRenderTarget(rt);
        rt = makeRenderTarget(p.outW, p.outH);
    }
}
