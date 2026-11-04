#pragma once
#include <algorithm>
#include <string>
#include <vector>

// What the user asked for and what the output window is doing. The editor's Output menu changes
// this; the OutputController (which owns the actual window) reads it and fills in the status fields.

enum class OutputMode  { Auto = 0, Fullscreen = 1, Windowed = 2 };   // Auto: fullscreen only when it won't cover the editor
enum class OutputScale { Fit = 0, Stretch = 1 };                     // Fit keeps the picture's shape (black bars); Stretch fills the screen

struct MonitorInfo {
    std::string name;
    int x = 0, y = 0, w = 0, h = 0;         // position and size in screen coordinates
    bool primary = false;
    bool hasEditor = false;                  // the main MapHappy window is on this display
};

struct OutputState {
    // --- what the user chose
    bool wanted = false;                     // output window should be open
    bool blackout = false;                   // show black instead of the picture (panic button)
    OutputMode mode = OutputMode::Auto;
    OutputScale scale = OutputScale::Fit;
    std::string monitorName;                 // preferred display; "" = pick automatically
    int monitorIndex = -1;                   // used with the name when several displays share one

    // --- filled in by the controller
    std::vector<MonitorInfo> monitors;
    int activeMonitor = -1;                  // index into monitors of the display in use (or that would be used)
    bool isOpen = false;
    bool isFullscreen = false;
    int fbW = 0, fbH = 0;                    // real pixel size of the output window
    std::string message;                     // e.g. "Display disconnected"
};

// Which display to use. A saved name wins (the nth one if several share it), then a saved index,
// otherwise the first display that isn't carrying the editor window (the projector), else any.
inline int resolveMonitor(const OutputState& st, const std::vector<MonitorInfo>& mons) {
    if (mons.empty()) return -1;
    if (!st.monitorName.empty()) {
        int first = -1;
        for (int i = 0; i < (int)mons.size(); i++)
            if (mons[i].name == st.monitorName) { if (first < 0) first = i; if (i == st.monitorIndex) return i; }
        if (first >= 0) return first;
    } else if (st.monitorIndex >= 0 && st.monitorIndex < (int)mons.size()) {
        return st.monitorIndex;
    }
    for (int i = 0; i < (int)mons.size(); i++) if (!mons[i].hasEditor) return i;
    for (int i = 0; i < (int)mons.size(); i++) if (mons[i].primary) return i;
    return 0;
}

// Fullscreen only when the user said so, or (Auto) when the chosen display is not the one the editor is on.
inline bool wantsFullscreen(const OutputState& st, const std::vector<MonitorInfo>& mons, int monitor) {
    if (st.mode == OutputMode::Fullscreen) return true;
    if (st.mode == OutputMode::Windowed) return false;
    return monitor >= 0 && monitor < (int)mons.size() && !mons[monitor].hasEditor;
}

// Where the picture goes inside a window: the whole window (Stretch) or the largest centered rectangle
// with the picture's shape (Fit). Pixel coordinates, origin at the bottom-left as OpenGL wants.
struct ViewRect { int x = 0, y = 0, w = 0, h = 0; };
inline ViewRect fitViewport(int winW, int winH, int picW, int picH, OutputScale scale) {
    ViewRect r{0, 0, winW, winH};
    if (scale == OutputScale::Stretch || picW <= 0 || picH <= 0 || winW <= 0 || winH <= 0) return r;
    double s = std::min((double)winW / picW, (double)winH / picH);
    r.w = std::max(1, (int)(picW * s + 0.5)); r.h = std::max(1, (int)(picH * s + 0.5));
    r.x = (winW - r.w) / 2; r.y = (winH - r.h) / 2;
    return r;
}
