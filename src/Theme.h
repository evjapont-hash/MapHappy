#pragma once
// The editor's look: black and white, so the only colour on screen is the show itself
// (red is kept for errors, Live and Blackout). Also the MapHappy logo.
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <vector>

inline constexpr ImU32 kAccent    = IM_COL32(255, 255, 255, 255);
inline constexpr ImU32 kAccentDim = IM_COL32(255, 255, 255, 90);

inline void applyTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 0.0f; st.FrameRounding = 2.0f; st.ChildRounding = 0.0f; st.PopupRounding = 2.0f;
    st.GrabRounding = 1.0f;   st.TabRounding = 0.0f;   st.ScrollbarRounding = 0.0f; st.GrabMinSize = 8.0f;
    st.WindowBorderSize = 1.0f; st.FramePadding = ImVec2(6, 4); st.ItemSpacing = ImVec2(8, 6);

    auto grey = [](float v, float a = 1.0f) { return ImVec4(v, v, v, a); };
    const ImVec4 white = grey(1.0f);
    ImVec4* c = st.Colors;
    c[ImGuiCol_Text]           = grey(0.94f);
    c[ImGuiCol_TextDisabled]   = grey(0.56f);
    c[ImGuiCol_WindowBg]       = grey(0.040f);
    c[ImGuiCol_ChildBg]        = grey(0.030f);
    c[ImGuiCol_PopupBg]        = grey(0.063f, 0.99f);
    c[ImGuiCol_MenuBarBg]      = grey(0.0f);
    c[ImGuiCol_Border]         = grey(0.15f);
    c[ImGuiCol_TitleBg]        = grey(0.0f);
    c[ImGuiCol_TitleBgActive]  = grey(0.0f);
    c[ImGuiCol_FrameBg]        = grey(0.110f);
    c[ImGuiCol_FrameBgHovered] = grey(0.160f);
    c[ImGuiCol_FrameBgActive]  = grey(0.200f);
    c[ImGuiCol_Button]         = grey(0.120f);
    c[ImGuiCol_ButtonHovered]  = grey(0.220f);
    c[ImGuiCol_ButtonActive]   = grey(0.320f);
    c[ImGuiCol_Header]         = grey(0.200f);
    c[ImGuiCol_HeaderHovered]  = grey(0.260f);
    c[ImGuiCol_HeaderActive]   = grey(0.340f);
    c[ImGuiCol_Separator]      = grey(0.15f);
    c[ImGuiCol_SeparatorHovered] = grey(0.40f);
    c[ImGuiCol_SeparatorActive]  = grey(0.60f);
    c[ImGuiCol_CheckMark]      = white;
    c[ImGuiCol_SliderGrab]     = grey(0.36f);          // dark enough for the value written over it
    c[ImGuiCol_SliderGrabActive] = grey(0.50f);
    c[ImGuiCol_ScrollbarBg]    = grey(0.0f, 0.0f);
    c[ImGuiCol_ScrollbarGrab]  = grey(0.22f);
    c[ImGuiCol_ScrollbarGrabHovered] = grey(0.32f);
    c[ImGuiCol_ScrollbarGrabActive]  = grey(0.45f);
    c[ImGuiCol_TextSelectedBg] = grey(1.0f, 0.25f);
    c[ImGuiCol_DragDropTarget] = white;
    c[ImGuiCol_NavCursor]      = white;
    c[ImGuiCol_ResizeGrip]     = grey(0.22f);
    c[ImGuiCol_ResizeGripHovered] = grey(0.40f);
    c[ImGuiCol_ResizeGripActive]  = grey(0.60f);
    c[ImGuiCol_ModalWindowDimBg]  = ImVec4(0, 0, 0, 0.65f);
}

// A switched-on button: white with black text. Pair with popOn().
inline void pushOn() {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.86f, 0.86f, 0.86f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.72f, 0.72f, 0.72f, 1));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 1));
}
inline void popOn() { ImGui::PopStyleColor(4); }

// A line on top of the picture, with a dark edge so that white shows on anything.
inline void overlayPolyline(ImDrawList* dl, const ImVec2* pts, int n, ImU32 col, ImDrawFlags flags, float width) {
    dl->AddPolyline(pts, n, IM_COL32(0, 0, 0, 110), flags, width + 2.0f);
    dl->AddPolyline(pts, n, col, flags, width);
}

// ---- the logo (the same two shapes as web/logo.svg), in a 100 x 100 box
inline const std::vector<std::vector<ImVec2>>& logoShapes() {
    static const std::vector<std::vector<ImVec2>> shapes = {
        {{0, 0}, {11, 11}, {11, 70}, {79, 70}, {79, 80}, {11, 80}, {11, 100}, {0, 100}},
        {{21, 21}, {50, 50}, {100, 0}, {100, 100}, {89, 100}, {89, 31}, {50, 70}, {21, 41}}};
    return shapes;
}
inline void drawLogo(ImDrawList* dl, ImVec2 pos, float size, ImU32 col) {
    for (const auto& shape : logoShapes()) {
        std::vector<ImVec2> pts;
        for (ImVec2 p : shape) pts.push_back(ImVec2(pos.x + p.x * size / 100.0f, pos.y + p.y * size / 100.0f));
        dl->AddConcavePolyFilled(pts.data(), (int)pts.size(), col);
    }
}
// The logo as a picture (white on a black square, RGBA rows from the top), for the window's icon.
inline std::vector<unsigned char> logoPixels(int size) {
    auto inside = [](const std::vector<ImVec2>& s, float x, float y) {
        bool in = false;
        for (size_t i = 0, j = s.size() - 1; i < s.size(); j = i++)
            if ((s[i].y > y) != (s[j].y > y) && x < (s[j].x - s[i].x) * (y - s[i].y) / (s[j].y - s[i].y) + s[i].x) in = !in;
        return in;
    };
    const int ss = 8;                                              // samples per pixel, each way
    std::vector<unsigned char> px((size_t)size * size * 4);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            int hits = 0;
            for (int k = 0; k < ss * ss; k++) {
                const float u = ((x + (k % ss + 0.5f) / ss) / size - 0.5f) * 140.0f + 50.0f;      // with a margin all round
                const float v = ((y + (k / ss + 0.5f) / ss) / size - 0.5f) * 140.0f + 50.0f;
                for (const auto& s : logoShapes()) if (inside(s, u, v)) { hits++; break; }
            }
            unsigned char* p = &px[((size_t)y * size + x) * 4];
            p[0] = p[1] = p[2] = (unsigned char)(hits * 255 / (ss * ss)); p[3] = 255;
        }
    return px;
}
