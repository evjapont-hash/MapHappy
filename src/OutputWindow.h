#pragma once
#include "GLUtil.h"
#include "OutputState.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <string>
#include <vector>

// Reads the displays from GLFW (positions and sizes in screen coordinates).
inline std::vector<MonitorInfo> queryMonitors() {
    std::vector<MonitorInfo> out;
    int n = 0;
    GLFWmonitor** m = glfwGetMonitors(&n);
    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    for (int i = 0; i < n; i++) {
        MonitorInfo mi;
        const char* nm = glfwGetMonitorName(m[i]);
        mi.name = nm ? nm : "Display";
        glfwGetMonitorPos(m[i], &mi.x, &mi.y);
        if (const GLFWvidmode* vm = glfwGetVideoMode(m[i])) { mi.w = vm->width; mi.h = vm->height; }
        mi.primary = (m[i] == primary);
        out.push_back(mi);
    }
    return out;
}

// Marks which display carries the editor window (the one containing the middle of that window).
inline void markEditorMonitor(GLFWwindow* editor, std::vector<MonitorInfo>& mons) {
    for (auto& m : mons) m.hasEditor = false;
    if (!editor || mons.empty()) return;
    int wx, wy, ww, wh;
    glfwGetWindowPos(editor, &wx, &wy); glfwGetWindowSize(editor, &ww, &wh);
    const int cx = wx + ww / 2, cy = wy + wh / 2;
    for (auto& m : mons) if (cx >= m.x && cx < m.x + m.w && cy >= m.y && cy < m.y + m.h) { m.hasEditor = true; return; }
    for (auto& m : mons) if (m.primary) { m.hasEditor = true; return; }
    mons[0].hasEditor = true;
}

// Keys pressed while the output window has focus. Esc/F11 close it, B blacks it out, Space and M
// work the transport, so a show can be driven even if the projector window grabbed the keyboard.
struct OutputCommands { bool close = false, blackout = false, playPause = false, marker = false; };

// The second window. It shares OpenGL objects with the editor window, so the stage texture is drawn
// straight into it with no copying. Borderless and sized to a display = fullscreen on that display.
class OutputWindow {
public:
    ~OutputWindow() { close(); }

    bool open(GLFWwindow* share, const MonitorInfo& mon, bool fullscreen, int previewW, int previewH, std::string& error) {
        close();
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);                  // position it first, then show (no flash in the wrong place)
        glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);             // don't minimise when focus moves to the editor's display
        glfwWindowHint(GLFW_DECORATED, fullscreen ? GLFW_FALSE : GLFW_TRUE);
        glfwWindowHint(GLFW_FLOATING,  fullscreen ? GLFW_TRUE  : GLFW_FALSE);
        glfwWindowHint(GLFW_RESIZABLE, fullscreen ? GLFW_FALSE : GLFW_TRUE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, fullscreen ? GLFW_FALSE : GLFW_TRUE);   // the editor keeps the keyboard

        const int w = fullscreen ? mon.w : previewW, h = fullscreen ? mon.h : previewH;
        win_ = glfwCreateWindow(std::max(16, w), std::max(16, h), "MapHappy Output", nullptr, share);
        glfwDefaultWindowHints();
        if (!win_) { error = "Couldn't create the output window"; return false; }
        glfwSetWindowPos(win_, fullscreen ? mon.x : mon.x + 60, fullscreen ? mon.y : mon.y + 60);
        glfwSetWindowUserPointer(win_, this);
        glfwSetKeyCallback(win_, &OutputWindow::onKey);
        glfwSetWindowCloseCallback(win_, &OutputWindow::onClose);
        if (fullscreen) glfwSetInputMode(win_, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);   // no mouse pointer on the projector
        fullscreen_ = fullscreen;
        cmd_ = OutputCommands{}; closeRequested_ = false;
        glfwShowWindow(win_);

        GLFWwindow* prev = glfwGetCurrentContext();
        glfwMakeContextCurrent(win_);
        glfwSwapInterval(1);                                       // the projector paces the show
        if (!prog_) prog_ = makeProgram(kFullscreenVS, kBlitFS);   // shared with the editor's context
        glGenVertexArrays(1, &vao_);                               // vertex array objects are NOT shared, so this window needs its own
        glfwMakeContextCurrent(prev);
        return true;
    }

    void close() {
        if (!win_) return;
        GLFWwindow* prev = glfwGetCurrentContext();
        glfwMakeContextCurrent(win_);
        if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
        glfwMakeContextCurrent(prev == win_ ? nullptr : prev);
        glfwDestroyWindow(win_);
        win_ = nullptr;
    }

    bool isOpen() const { return win_ != nullptr; }
    bool isFullscreen() const { return fullscreen_; }
    GLFWwindow* handle() const { return win_; }
    bool closeRequested() { bool r = closeRequested_; closeRequested_ = false; return r; }
    OutputCommands takeCommands() { OutputCommands c = cmd_; cmd_ = OutputCommands{}; return c; }
    void framebufferSize(int& w, int& h) const { w = h = 0; if (win_) glfwGetFramebufferSize(win_, &w, &h); }

    // Draws the stage into this window and presents it. Leaves the editor's context current again.
    // `readback` (optional): receives the window's pixels (RGB, bottom row first) just before presenting.
    void render(GLuint stageTex, int stageW, int stageH, bool blackout, OutputScale scale, std::vector<unsigned char>* readback = nullptr) {
        if (!win_) return;
        GLFWwindow* prev = glfwGetCurrentContext();
        glfwMakeContextCurrent(win_);
        int fw = 0, fh = 0;
        glfwGetFramebufferSize(win_, &fw, &fh);
        if (fw > 0 && fh > 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST);
            glViewport(0, 0, fw, fh);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);                           // everything outside the picture (and blackout) is pure black
            if (!blackout && stageTex) {
                ViewRect r = fitViewport(fw, fh, stageW, stageH, scale);
                glViewport(r.x, r.y, r.w, r.h);
                glUseProgram(prog_);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, stageTex);
                glUniform1i(glGetUniformLocation(prog_, "uTex"), 0);
                glUniform4f(glGetUniformLocation(prog_, "uRect"), (float)r.x, (float)r.y, (float)r.w, (float)r.h);
                glBindVertexArray(vao_);
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
            if (readback) {
                readback->assign((size_t)fw * fh * 3, 0);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadBuffer(GL_BACK);
                glReadPixels(0, 0, fw, fh, GL_RGB, GL_UNSIGNED_BYTE, readback->data());
            }
        }
        glfwSwapBuffers(win_);
        glfwMakeContextCurrent(prev);
    }

    // Key handling is public so tests can call it directly.
    static void onKey(GLFWwindow* w, int key, int, int action, int) {
        if (action != GLFW_PRESS) return;
        OutputWindow* self = static_cast<OutputWindow*>(glfwGetWindowUserPointer(w));
        if (!self) return;
        if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_F11) self->cmd_.close = true;
        else if (key == GLFW_KEY_B)     self->cmd_.blackout = true;
        else if (key == GLFW_KEY_SPACE) self->cmd_.playPause = true;
        else if (key == GLFW_KEY_M)     self->cmd_.marker = true;
    }
    static void onClose(GLFWwindow* w) {
        if (OutputWindow* self = static_cast<OutputWindow*>(glfwGetWindowUserPointer(w))) self->closeRequested_ = true;
        glfwSetWindowShouldClose(w, GLFW_FALSE);
    }

private:
    // Copies the stage texture into the rectangle the picture occupies. The picture's texture coordinates come
    // from where each pixel sits inside that rectangle; at 1:1 size each pixel lands exactly on a texel centre.
    static constexpr const char* kBlitFS = R"GLSL(#version 330 core
uniform sampler2D uTex;
uniform vec4 uRect;      // x, y, width, height of the picture inside the window, in pixels
out vec4 frag;
void main() {
    vec2 uv = (gl_FragCoord.xy - uRect.xy) / uRect.zw;
    frag = vec4(texture(uTex, uv).rgb, 1.0);
}
)GLSL";

    GLFWwindow* win_ = nullptr;
    GLuint vao_ = 0;
    inline static GLuint prog_ = 0;      // one blit shader for the whole app (program objects are shared between the windows)
    bool fullscreen_ = false, closeRequested_ = false;
    OutputCommands cmd_;
};
