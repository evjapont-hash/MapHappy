#pragma once
#include "AudioLoader.h"
#include "ConductorPanel.h"
#include "FileBrowser.h"
#include "OutputState.h"
#include "ProjectIO.h"
#include "GLUtil.h"
#include "Project.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

inline ImTextureID texId(GLuint t) { return (ImTextureID)(intptr_t)t; }
inline constexpr ImU32 kAccent    = IM_COL32(38, 198, 208, 255);
inline constexpr ImU32 kAccentDim = IM_COL32(38, 198, 208, 90);

inline void applyTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 3.0f; st.FrameRounding = 3.0f; st.ChildRounding = 3.0f;
    st.GrabRounding = 3.0f;   st.TabRounding = 3.0f;
    st.WindowBorderSize = 1.0f; st.FramePadding = ImVec2(6, 4); st.ItemSpacing = ImVec2(8, 6);

    ImVec4* c = st.Colors;
    const ImVec4 accent(0.15f, 0.78f, 0.82f, 1.0f);
    const ImVec4 accentDim(0.10f, 0.45f, 0.50f, 1.0f);
    c[ImGuiCol_WindowBg]       = ImVec4(0.135f, 0.135f, 0.145f, 1.0f);
    c[ImGuiCol_ChildBg]        = ImVec4(0.110f, 0.110f, 0.120f, 1.0f);
    c[ImGuiCol_PopupBg]        = ImVec4(0.140f, 0.140f, 0.150f, 0.98f);
    c[ImGuiCol_Border]         = ImVec4(0.26f, 0.26f, 0.28f, 1.0f);
    c[ImGuiCol_TitleBg]        = ImVec4(0.10f, 0.10f, 0.11f, 1.0f);
    c[ImGuiCol_TitleBgActive]  = ImVec4(0.10f, 0.10f, 0.11f, 1.0f);
    c[ImGuiCol_FrameBg]        = ImVec4(0.18f, 0.18f, 0.20f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.25f, 1.0f);
    c[ImGuiCol_FrameBgActive]  = ImVec4(0.25f, 0.25f, 0.28f, 1.0f);
    c[ImGuiCol_Button]         = ImVec4(0.20f, 0.20f, 0.22f, 1.0f);
    c[ImGuiCol_ButtonHovered]  = ImVec4(0.26f, 0.26f, 0.29f, 1.0f);
    c[ImGuiCol_ButtonActive]   = accentDim;
    c[ImGuiCol_Header]         = accentDim;
    c[ImGuiCol_HeaderHovered]  = ImVec4(0.12f, 0.55f, 0.60f, 1.0f);
    c[ImGuiCol_HeaderActive]   = accent;
    c[ImGuiCol_CheckMark]      = accent;
    c[ImGuiCol_SliderGrab]     = accent;
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.30f, 0.90f, 0.95f, 1.0f);
}

// The whole editor UI: toolbar, left panel (Media / Surfaces), Input + Stage views, Conductor area.
class Editor {
public:
    Editor(Project& p, RenderTarget& stage) : proj_(p), stage_(stage), conductor_(p) {
        pathBuf_[0] = '\0';
        settings_ = loadSettings();
        conductor_.onRelinkAudio = [this](int assetId) { openRelinkAudio(assetId); };
    }

    const ConductorPanel& conductor() const { return conductor_; }

    // Called when a file is dropped on the window (or chosen in the file dialog).
    void onFileDropped(const std::string& path) {
        if (lowerExtension(path) == "maphappy") requestOpenFile(path);
        else if (isAudioPath(path)) startAudioLoad(path);
        else addVideo(path, true);
    }

    // ---- project files
    std::string windowTitle() const { return "MapHappy - " + (filePath_.empty() ? std::string("Untitled") : fileNameOf(filePath_)) + (dirty_ ? " *" : ""); }
    bool dirty() const { return dirty_; }
    const std::string& filePath() const { return filePath_; }
    void markClean() { savedSnapshot_ = projectToJson(proj_, filePath_, true); dirty_ = false; }
    void refreshDirtyNow() { dirty_ = projectToJson(proj_, filePath_, true) != savedSnapshot_; }
    bool exitApproved() const { return exitApproved_; }
    void requestExit()  { if (dirty_) askUnsaved(Pending::Exit, ""); else exitApproved_ = true; }
    void requestNew()   { if (dirty_) askUnsaved(Pending::New, ""); else performPending(Pending::New, "", false); }
    void requestOpenFile(const std::string& path) { if (dirty_) askUnsaved(Pending::Open, path); else performPending(Pending::Open, path, false); }
    bool saveProject()  { if (filePath_.empty()) { openSaveAs(); return false; } return doSave(filePath_); }
    // Writes a recovery copy next to the project (or in the app folder for an untitled one). Runs by itself every minute while there are unsaved changes.
    void autosaveNow() { std::string err; writeTextFileAtomic(autosavePathFor(filePath_), projectToJson(proj_, filePath_, false), err); }
    // Offer to bring back work from a crashed session. Call once at startup.
    void startupRecovery() { if (autosaveIsNewer("")) { recoverPath_.clear(); wantRecover_ = true; } }

    // Connects the Output menu and buttons to the projector output. Call once, before the first frame.
    void attachOutput(OutputState* o) {
        out_ = o;
        if (!out_) return;
        out_->monitorName = settings_.outputMonitor; out_->monitorIndex = settings_.outputMonitorIndex;
        out_->mode = settings_.outputMode == "fullscreen" ? OutputMode::Fullscreen : settings_.outputMode == "windowed" ? OutputMode::Windowed : OutputMode::Auto;
        out_->scale = settings_.outputScale == "stretch" ? OutputScale::Stretch : OutputScale::Fit;
    }

    struct Rect { float x = 0, y = 0, w = 0, h = 0; bool valid = false; };   // where the views were drawn (used by tests)
    Rect stageRect() const { return stageRect_; }
    // Where a named control was drawn this frame ("slider:posX", "header:Transform", ...). Used by the automated UI tests.
    Rect uiRect(const std::string& name) const { auto it = rects_.find(name); return it == rects_.end() ? Rect{} : it->second; }
    Rect inputRect() const { return inputRect_; }

    void drawUI() {
        rects_.clear();
        pollAudio();
        tickProjectState();
        const float menuH = drawMenuBar();
        handleShortcuts();
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const ImVec2 origin(vp->Pos.x, vp->Pos.y + menuH), size(vp->Size.x, vp->Size.y - menuH);
        const float toolbarH = 44.0f;
        const float leftW    = std::min(340.0f, size.x * 0.32f);
        const float bottomH  = std::max(120.0f, size.y * 0.26f);
        const float centerX  = origin.x + leftW;
        const float centerW  = size.x - leftW;
        const float centerY  = origin.y + toolbarH;
        const float centerH  = size.y - toolbarH - bottomH;

        drawToolbar(origin, size.x, toolbarH);

        ImVec2 leftPos(origin.x, centerY), leftSize(leftW, size.y - toolbarH);
        if (leftTab_ == 0) drawMediaPanel(leftPos, leftSize);
        else               drawSurfacesPanel(leftPos, leftSize);

        if (viewMode_ == 0) {
            drawInputPanel(ImVec2(centerX, centerY), ImVec2(centerW * 0.5f, centerH));
            drawStagePanel(ImVec2(centerX + centerW * 0.5f, centerY), ImVec2(centerW * 0.5f, centerH));
        } else if (viewMode_ == 1) {
            drawInputPanel(ImVec2(centerX, centerY), ImVec2(centerW, centerH));
        } else {
            drawStagePanel(ImVec2(centerX, centerY), ImVec2(centerW, centerH));
        }

        conductor_.draw(ImVec2(centerX, centerY + centerH), ImVec2(centerW, bottomH));
        drawDialogs();
    }

    // Call once per frame AFTER the UI has been drawn. Changes that delete GPU textures
    // wait until here, so no widget drawn earlier this frame points at a deleted texture.
    void endFrame() {
        for (int id : pendingRemoveMedia_) {
            proj_.removeMedia(id);
            if (selectedMediaId_ == id) selectedMediaId_ = 0;
        }
        pendingRemoveMedia_.clear();

        if (relinkMediaId_) {                                      // swapping a video's source frees its old texture
            std::string err;
            if (!proj_.media.relink(relinkMediaId_, relinkMediaPath_, err)) status_ = "Couldn't relink: " + err;
            else status_.clear();
            relinkMediaId_ = 0;
        }
        const Pending a = deferred_; deferred_ = Pending::None;
        if (a == Pending::New)       newProject();
        else if (a == Pending::Open) openProjectNow(deferredPath_);
        else if (a == Pending::Exit) exitApproved_ = true;
        if (deferredRecover_) { deferredRecover_ = false; recoverNow(); }
    }

private:
    // ------------------------------------------------------------------ helpers
    static constexpr ImGuiWindowFlags kPanel = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

    struct View {   // where an image sits on screen, and conversion between screen pixels and 0..1
        ImVec2 min{0, 0}, size{1, 1};
        bool valid = false;
        ImVec2 toScreen(Vec2 n) const { return ImVec2(min.x + n.x * size.x, min.y + n.y * size.y); }
        Vec2   toNorm(ImVec2 s) const { return Vec2{(s.x - min.x) / size.x, (s.y - min.y) / size.y}; }
    };

    // Fits an image of the given aspect ratio into the space left in the current window.
    static View fitView(float aspect, float margin) {
        View v;
        ImVec2 cur = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
        float aw = avail.x - 2 * margin, ah = avail.y - 2 * margin;
        if (aw < 16 || ah < 16 || aspect <= 0) return v;
        float w = aw, h = w / aspect;
        if (h > ah) { h = ah; w = h * aspect; }
        v.min  = ImVec2(cur.x + (avail.x - w) * 0.5f, cur.y + (avail.y - h) * 0.5f);
        v.size = ImVec2(w, h);
        v.valid = true;
        return v;
    }

    static bool tabButton(const char* label, bool active) {
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.45f, 0.50f, 1.0f));
        bool clicked = ImGui::Button(label, ImVec2(86, 0));
        if (active) ImGui::PopStyleColor();
        return clicked;
    }

    // Outline, diagonals and handles for one quad (and its shape mask).
    void drawQuadOverlay(const View& v, Shape shape, const Vec2 q[4], bool selected) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 col = selected ? kAccent : IM_COL32(210, 210, 215, 110);
        ImVec2 p[4];
        for (int i = 0; i < 4; i++) p[i] = v.toScreen(q[i]);
        dl->AddPolyline(p, 4, col, ImDrawFlags_Closed, selected ? 1.5f : 1.0f);

        if (shape != Shape::Quad) {
            std::vector<ImVec2> pts;
            for (Vec2 o : outlineInQuad(shape, q)) pts.push_back(v.toScreen(o));
            if (pts.size() > 2) dl->AddPolyline(pts.data(), (int)pts.size(), col, ImDrawFlags_Closed, 2.0f);
        }
        if (!selected) return;

        dl->AddLine(p[0], p[2], kAccentDim, 1.0f);
        dl->AddLine(p[1], p[3], kAccentDim, 1.0f);
        for (int i = 0; i < 4; i++) {
            dl->AddCircleFilled(p[i], 8.0f, IM_COL32(20, 20, 24, 200));
            dl->AddCircle(p[i], 8.0f, kAccent, 0, 2.0f);
        }
        ImVec2 c = v.toScreen(quadCenter(q));        // the "move" handle
        dl->AddCircle(c, 6.0f, kAccent, 0, 1.5f);
        dl->AddLine(ImVec2(c.x - 9, c.y), ImVec2(c.x + 9, c.y), kAccent, 1.5f);
        dl->AddLine(ImVec2(c.x, c.y - 9), ImVec2(c.x, c.y + 9), kAccent, 1.5f);
    }

    // A thin light-yellow outline of where a transformed surface really is, drawn alongside its mapped quad.
    void drawEffectiveOutline(const View& v, Shape shape, const Vec2 q[4]) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 col = IM_COL32(255, 238, 130, 240);        // readable against both black and orange pictures
        ImVec2 p[4];
        for (int i = 0; i < 4; i++) p[i] = v.toScreen(q[i]);
        dl->AddPolyline(p, 4, col, ImDrawFlags_Closed, 1.0f);
        if (shape != Shape::Quad) {
            std::vector<ImVec2> pts;
            for (Vec2 o : outlineInQuad(shape, q)) pts.push_back(v.toScreen(o));
            if (pts.size() > 2) dl->AddPolyline(pts.data(), (int)pts.size(), col, ImDrawFlags_Closed, 1.5f);
        }
    }

    enum Panel { PanelInput = 0, PanelStage = 1 };
    struct Drag { int handle = -1; Panel panel = PanelInput; int surfaceId = 0; Vec2 prev{0, 0}; };

    // Mouse handling for a view. Must be called right after the InvisibleButton covering it.
    // handles 0..3 = corners, 4 = move the whole quad.
    void interact(Panel panel, const View& v) {
        ImGuiIO& io = ImGui::GetIO();
        const bool isStage = panel == PanelStage;
        const Vec2 mouse = v.toNorm(io.MousePos);

        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
            drag_.handle = -1;
            int handle = -1, surfaceId = 0;
            if (Surface* s = proj_.selected()) {
                const Vec2* q = isStage ? s->out : s->in;
                float best = 14.0f;
                for (int i = 0; i < 4; i++) {
                    ImVec2 sp = v.toScreen(q[i]);
                    float d = std::hypot(sp.x - io.MousePos.x, sp.y - io.MousePos.y);
                    if (d < best) { best = d; handle = i; }
                }
                if (handle < 0 && pointInPolygon(outlineInQuad(s->shape, q), mouse)) handle = 4;
                if (handle < 0 && isStage) { Vec2 eo[4]; effectiveOut(*s, (float)proj_.outW / (float)std::max(1, proj_.outH), eo); if (pointInPolygon(outlineInQuad(s->shape, eo), mouse)) handle = 4; }
                if (handle >= 0) surfaceId = s->id;
            }
            if (handle < 0 && isStage) {          // clicked empty space or another surface: select the top one under the mouse
                for (const Surface& o : proj_.surfaces) {
                    Vec2 eo[4]; effectiveOut(o, (float)proj_.outW / (float)std::max(1, proj_.outH), eo);      // click what you can SEE
                    if (o.visible && pointInPolygon(outlineInQuad(o.shape, eo), mouse)) {
                        proj_.selectedSurfaceId = o.id;
                        handle = 4; surfaceId = o.id;
                        break;
                    }
                }
            }
            if (handle >= 0) { drag_.handle = handle; drag_.panel = panel; drag_.surfaceId = surfaceId; drag_.prev = mouse; }
        }

        if (drag_.handle >= 0 && drag_.panel == panel) {
            Surface* s = proj_.find(drag_.surfaceId);
            if (!s || !ImGui::IsMouseDown(0)) { drag_.handle = -1; return; }
            Vec2* q = isStage ? s->out : s->in;
            if (drag_.handle < 4) {
                Vec2 old = q[drag_.handle];
                q[drag_.handle] = mouse;
                if (!isConvex(q)) q[drag_.handle] = old;      // a bow-tie or dented quad has no valid projection
            } else {
                float dx = mouse.x - drag_.prev.x, dy = mouse.y - drag_.prev.y;
                for (int i = 0; i < 4; i++) { q[i].x += dx; q[i].y += dy; }
            }
            drag_.prev = mouse;
        }
    }

    // ------------------------------------------------------------------ toolbar
    void drawToolbar(ImVec2 pos, float width, float height) {
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(ImVec2(width, height));
        ImGui::Begin("##toolbar", nullptr, kPanel | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        if (tabButton("Media", leftTab_ == 0))    leftTab_ = 0;
        ImGui::SameLine();
        if (tabButton("Surfaces", leftTab_ == 1)) leftTab_ = 1;
        ImGui::SameLine(0, 40);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Views:");
        ImGui::SameLine();
        if (tabButton("Both", viewMode_ == 0))  viewMode_ = 0;
        ImGui::SameLine();
        if (tabButton("Input", viewMode_ == 1)) viewMode_ = 1;
        ImGui::SameLine();
        if (tabButton("Stage", viewMode_ == 2)) viewMode_ = 2;
        if (out_) {
            ImGui::SameLine(std::max(width - 90.0f - 214.0f, 0.0f));
            const bool live = out_->isOpen;
            if (live) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.45f, 0.50f, 1.0f));
            if (ImGui::Button(live ? "Output: ON" : "Output", ImVec2(100, 0))) toggleOutput();
            if (live) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show or hide the projector output window (F11)");
            ImGui::SameLine();
            const bool black = out_->blackout;
            if (black) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.15f, 0.15f, 1.0f));
            if (ImGui::Button(black ? "BLACKOUT" : "Blackout", ImVec2(100, 0))) out_->blackout = !out_->blackout;
            if (black) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cut the output to black (B). Works from the output window too.");
        }
        ImGui::SameLine(std::max(width - 90.0f, 0.0f));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%.0f FPS", ImGui::GetIO().Framerate);
        ImGui::End();
    }

    // ------------------------------------------------------------------ Surfaces tab
    void drawSurfacesPanel(ImVec2 pos, ImVec2 size) {
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("Surfaces", nullptr, kPanel);

        if (ImGui::Button("+ Quad"))     proj_.addSurface(Shape::Quad, defaultMedia());
        ImGui::SameLine();
        if (ImGui::Button("+ Circle"))   proj_.addSurface(Shape::Circle, defaultMedia());
        ImGui::SameLine();
        if (ImGui::Button("+ Triangle")) proj_.addSurface(Shape::Triangle, defaultMedia());

        const bool has = proj_.selected() != nullptr;
        ImGui::BeginDisabled(!has);
        if (ImGui::Button("Delete"))  proj_.removeSurface(proj_.selectedSurfaceId);
        ImGui::SameLine();
        if (ImGui::Button("Up"))      proj_.moveSurface(proj_.selectedSurfaceId, -1);
        ImGui::SameLine();
        if (ImGui::Button("Down"))    proj_.moveSurface(proj_.selectedSurfaceId, +1);
        ImGui::EndDisabled();

        ImGui::BeginChild("##surfacelist", ImVec2(0, 190), ImGuiChildFlags_Borders);
        for (Surface& s : proj_.surfaces) {
            ImGui::PushID(s.id);
            ImGui::Checkbox("##visible", &s.visible);
            ImGui::SameLine();
            char label[96];
            std::snprintf(label, sizeof(label), "[%s]  %s", shapeTag(s.shape), s.name.c_str());
            if (ImGui::Selectable(label, s.id == proj_.selectedSurfaceId)) proj_.selectedSurfaceId = s.id;
            ImGui::PopID();
        }
        if (proj_.surfaces.empty()) ImGui::TextDisabled("No surfaces yet. Add one above.");
        ImGui::EndChild();

        if (Surface* s = proj_.selected()) drawSurfaceInspector(*s);
        else ImGui::TextDisabled("Select a surface to edit it.");
        ImGui::End();
    }

    void recordRect(const std::string& name) { const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax(); rects_[name] = Rect{a.x, a.y, b.x - a.x, b.y - a.y, true}; }

    // ---- animatable parameters in the inspector
    bool isAnimated(int surfaceId, const char* key) { return proj_.timeline.findTrackByPath(surfaceParamPath(surfaceId, key)) != nullptr; }

    static void setParamToDefault(Surface& s, const char* key) { if (const ParamDef* d = findParamDef(key)) d->set(s, d->def); }

    // Starts animating a parameter: makes its track, with a first 2 second clip at the playhead that holds the
    // current value (so nothing jumps), and shows it in the Conductor.
    void animateParam(Surface& s, const char* key) {
        const std::string path = surfaceParamPath(s.id, key);
        const bool had = proj_.timeline.findTrackByPath(path) != nullptr;
        const int tid = proj_.timeline.addTrack(path);
        Track* t = proj_.timeline.findTrack(tid);
        int cid = 0;
        if (!had && t && t->clips.empty()) {
            const ParamDef* d = findParamDef(key);
            const double start = Timeline::freeStart(*t, snapToFrame(proj_.transport.time), 2.0);
            cid = proj_.timeline.addClip(tid, start, 2.0, d ? d->get(s) : 0.0f);
        }
        if (cid) conductor_.selectClip(tid, cid); else conductor_.selectTrack(tid);
        proj_.timelineDirty = true;
    }

    // One slider for one parameter. A gold grab and a * mean a timeline is driving it. Double-click (or Ctrl+click)
    // types an exact value; right-click offers "Animate in the Conductor" and "Reset to default".
    void paramSlider(Surface& s, const char* key) {
        const ParamDef* d = findParamDef(key);
        if (!d) return;
        const float scale = d->format == ParamFormat::Percent ? 100.0f : 1.0f;
        const char* fmt = d->format == ParamFormat::Percent ? "%.0f%%" : d->format == ParamFormat::Degrees ? "%.1f deg" : "%.2f";
        float v = d->get(s) * scale;
        const bool animated = isAnimated(s.id, key);
        ImGui::PushID(key);
        if (animated) ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.95f, 0.75f, 0.2f, 1.0f));
        const std::string label = std::string(d->label) + (animated ? " *" : "") + "###slider";
        if (ImGui::SliderFloat(label.c_str(), &v, d->min * scale, d->max * scale, fmt)) d->set(s, std::max(d->min, std::min(d->max, v / scale)));
        if (animated) ImGui::PopStyleColor();
        { const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax(); rects_[std::string("slider:") + key] = Rect{a.x, a.y, ImGui::CalcItemWidth(), b.y - a.y, true}; }   // just the slider frame, not its label
        if (ImGui::BeginPopupContextItem("##pctx")) {
            if (!animated) { if (ImGui::MenuItem("Animate in the Conductor")) animateParam(s, key); }
            else {
                if (ImGui::MenuItem("Show in the Conductor")) { Track* t = proj_.timeline.findTrackByPath(surfaceParamPath(s.id, key)); if (t) conductor_.selectTrack(t->id); }
                if (ImGui::MenuItem("Remove animation")) { if (Track* t = proj_.timeline.findTrackByPath(surfaceParamPath(s.id, key))) { proj_.timeline.removeTrack(t->id); proj_.timelineDirty = true; } }
            }
            if (ImGui::MenuItem("Reset to default")) d->set(s, d->def);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    void drawSurfaceInspector(Surface& s) {
        ImGui::SeparatorText("Inspector");
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s", s.name.c_str());
        if (ImGui::InputText("Name", buf, sizeof(buf))) s.name = buf;

        int shape = (int)s.shape;
        const char* shapes[] = {"Quad", "Circle", "Triangle"};
        if (ImGui::Combo("Shape", &shape, shapes, 3)) { s.shape = (Shape)shape; proj_.resetInput(s); }

        if (ImGui::CollapsingHeader("Media", ImGuiTreeNodeFlags_DefaultOpen)) {
            MediaItem* cur = proj_.media.find(s.mediaId);
            if (ImGui::BeginCombo("Source", cur ? cur->name.c_str() : "(none)")) {
                for (auto& m : proj_.media.items) {
                    ImGui::PushID(m->id);
                    if (ImGui::Selectable(m->name.c_str(), m->id == s.mediaId)) s.mediaId = m->id;
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if (cur) {
                float w = std::min(120.0f, ImGui::GetContentRegionAvail().x);
                ImGui::Image(texId(cur->source->texture()), ImVec2(w, w / cur->aspect()), ImVec2(0, 1), ImVec2(1, 0));
            }
        }
        if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
            paramSlider(s, "opacity");
            int blend = (int)s.blend;
            const char* blends[] = {"Over", "Add", "Multiply", "Screen"};
            if (ImGui::Combo("Blend", &blend, blends, 4)) s.blend = (Blend)blend;
            recordRect("combo:Blend");
            paramSlider(s, "feather");
            ImGui::Checkbox("Flip horizontal", &s.flipH);
            ImGui::SameLine();
            ImGui::Checkbox("Flip vertical", &s.flipV);
        }
        const bool openTransform = ImGui::CollapsingHeader("Transform");
        recordRect("header:Transform");
        if (openTransform) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]); ImGui::TextWrapped("Moves the mapped shape without changing how it is mapped"); ImGui::PopStyleColor();
            for (const char* k : {"posX", "posY", "rotation", "scale", "scaleX", "scaleY"}) paramSlider(s, k);
            const bool resetT = ImGui::Button("Reset transform");
            recordRect("button:Reset transform");
            if (resetT) for (const char* k : {"posX", "posY", "rotation", "scale", "scaleX", "scaleY"}) setParamToDefault(s, k);
        }
        const bool openContent = ImGui::CollapsingHeader("Content");
        recordRect("header:Content");
        if (openContent) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]); ImGui::TextWrapped("Scrolls, zooms and spins the picture inside the shape"); ImGui::PopStyleColor();
            for (const char* k : {"contentX", "contentY", "contentScale", "contentRotation"}) paramSlider(s, k);
            if (ImGui::Button("Reset content")) for (const char* k : {"contentX", "contentY", "contentScale", "contentRotation"}) setParamToDefault(s, k);
        }
        const bool openColor = ImGui::CollapsingHeader("Color");
        recordRect("header:Color");
        if (openColor) {
            float tint[3] = {s.tintR, s.tintG, s.tintB};
            if (ImGui::ColorEdit3("Tint", tint)) { s.tintR = tint[0]; s.tintG = tint[1]; s.tintB = tint[2]; }
            if (ImGui::BeginPopupContextItem("##tintctx")) {
                for (const char* k : {"tintR", "tintG", "tintB"}) {
                    const ParamDef* d = findParamDef(k);
                    if (d && ImGui::MenuItem((std::string("Animate ") + d->label).c_str(), nullptr, false, !isAnimated(s.id, k))) animateParam(s, k);
                }
                ImGui::EndPopup();
            }
            for (const char* k : {"hue", "saturation", "contrast", "brightness"}) paramSlider(s, k);
            if (ImGui::Button("Reset colour")) for (const char* k : {"tintR", "tintG", "tintB", "hue", "saturation", "contrast", "brightness"}) setParamToDefault(s, k);
        }
        if (ImGui::CollapsingHeader("Geometry")) {
            if (ImGui::Button("Reset input")) proj_.resetInput(s);
            ImGui::SameLine();
            if (ImGui::Button("Reset output")) {
                const Vec2 d[4] = {{0.36f, 0.25f}, {0.64f, 0.25f}, {0.64f, 0.75f}, {0.36f, 0.75f}};
                for (int i = 0; i < 4; i++) s.out[i] = d[i];
            }
        }
    }



    // ------------------------------------------------------------------ projector output
    void toggleOutput() { if (out_) { out_->wanted = !out_->wanted; if (out_->wanted) out_->message.clear(); } }

    void saveOutputSettings() {
        if (!out_) return;
        settings_.outputMonitor = out_->monitorName; settings_.outputMonitorIndex = out_->monitorIndex;
        settings_.outputMode = out_->mode == OutputMode::Fullscreen ? "fullscreen" : out_->mode == OutputMode::Windowed ? "windowed" : "auto";
        settings_.outputScale = out_->scale == OutputScale::Stretch ? "stretch" : "fit";
        saveSettings(settings_);
    }

    void setOutputResolution(int w, int h) {
        proj_.outW = std::max(64, std::min(8192, w));
        proj_.outH = std::max(64, std::min(8192, h));
        toast("Output size " + std::to_string(proj_.outW) + " x " + std::to_string(proj_.outH));
    }

    // Matches the stage to the projector's own pixels, so the picture is shown 1:1 with no scaling.
    void matchOutputResolution() {
        if (!out_) return;
        if (out_->isFullscreen && out_->fbW > 0) { setOutputResolution(out_->fbW, out_->fbH); return; }
        const int i = out_->activeMonitor;
        if (i >= 0 && i < (int)out_->monitors.size() && out_->monitors[i].w > 0) setOutputResolution(out_->monitors[i].w, out_->monitors[i].h);
        else toast("No display to match");
    }

    void drawOutputMenu() {
        if (!out_ || !ImGui::BeginMenu("Output")) return;
        if (ImGui::MenuItem("Show output window", "F11", out_->wanted)) toggleOutput();
        if (ImGui::MenuItem("Blackout", "B", out_->blackout)) out_->blackout = !out_->blackout;
        ImGui::Separator();

        if (ImGui::BeginMenu("Display")) {
            if (out_->monitors.empty()) ImGui::TextDisabled("No displays found");
            for (int i = 0; i < (int)out_->monitors.size(); i++) {
                const MonitorInfo& m = out_->monitors[i];
                std::string label = std::to_string(i + 1) + ": " + m.name + "  (" + std::to_string(m.w) + " x " + std::to_string(m.h) + ")";
                if (m.hasEditor) label += "  - this window";
                if (ImGui::MenuItem((label + "##mon" + std::to_string(i)).c_str(), nullptr, i == out_->activeMonitor)) {
                    out_->monitorName = m.name; out_->monitorIndex = i; saveOutputSettings();
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Mode")) {
            if (ImGui::MenuItem("Automatic (fullscreen on another display)", nullptr, out_->mode == OutputMode::Auto)) { out_->mode = OutputMode::Auto; saveOutputSettings(); }
            if (ImGui::MenuItem("Always fullscreen", nullptr, out_->mode == OutputMode::Fullscreen)) { out_->mode = OutputMode::Fullscreen; saveOutputSettings(); }
            if (ImGui::MenuItem("Preview window", nullptr, out_->mode == OutputMode::Windowed)) { out_->mode = OutputMode::Windowed; saveOutputSettings(); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Scaling")) {
            if (ImGui::MenuItem("Fit (keep the picture's shape)", nullptr, out_->scale == OutputScale::Fit)) { out_->scale = OutputScale::Fit; saveOutputSettings(); }
            if (ImGui::MenuItem("Stretch (fill the screen)", nullptr, out_->scale == OutputScale::Stretch)) { out_->scale = OutputScale::Stretch; saveOutputSettings(); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Resolution")) {
            if (ImGui::MenuItem("Match the output display")) matchOutputResolution();
            ImGui::Separator();
            struct Preset { const char* name; int w, h; };
            static const Preset presets[] = {{"1280 x 720", 1280, 720}, {"1920 x 1080", 1920, 1080}, {"2560 x 1440", 2560, 1440}, {"3840 x 2160", 3840, 2160}};
            for (const Preset& pr : presets)
                if (ImGui::MenuItem(pr.name, nullptr, proj_.outW == pr.w && proj_.outH == pr.h)) setOutputResolution(pr.w, pr.h);
            ImGui::Separator();
            if (ImGui::MenuItem("Custom...")) { customW_ = proj_.outW; customH_ = proj_.outH; wantCustomSize_ = true; }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    void drawOutputDialogs() {
        if (wantCustomSize_) { ImGui::OpenPopup("Output size"); wantCustomSize_ = false; }
        centerNextWindow();
        if (ImGui::BeginPopupModal("Output size", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::TextDisabled("The resolution the show is drawn at (64 to 8192). Use your projector's own\nresolution for a sharp picture.");
            ImGui::SetNextItemWidth(110); ImGui::InputInt("Width", &customW_, 0, 0);
            ImGui::SetNextItemWidth(110); ImGui::InputInt("Height", &customH_, 0, 0);
            if (ImGui::Button("Apply", ImVec2(100, 0))) { setOutputResolution(customW_, customH_); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // ------------------------------------------------------------------ project files
    enum class Pending { None, New, Open, Exit };
    enum class FileAction { None, OpenProject, SaveProject, AddMedia, RelinkMedia, RelinkAudio };

    float drawMenuBar() {
        const float h = ImGui::GetFrameHeight();
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New", "Ctrl+N")) requestNew();
                if (ImGui::MenuItem("Open...", "Ctrl+O")) openOpenDialog();
                if (ImGui::BeginMenu("Open Recent", !settings_.recent.empty())) {
                    for (const std::string& r : settings_.recent)
                        if (ImGui::MenuItem((fileNameOf(r) + "##" + r).c_str())) requestOpenFile(r);
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Save", "Ctrl+S")) saveProject();
                if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) openSaveAs();
                ImGui::Separator();
                if (ImGui::MenuItem("Add media...")) openAddMedia();
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) requestExit();
                ImGui::EndMenu();
            }
            drawOutputMenu();
            // project name in the middle, a short message (e.g. "Saved") on the right
            std::string name = filePath_.empty() ? std::string("Untitled") : fileNameOf(filePath_);
            if (dirty_) name += " *";
            const float w = ImGui::GetWindowWidth();
            ImGui::SameLine(std::max(120.0f, (w - ImGui::CalcTextSize(name.c_str()).x) * 0.5f));
            ImGui::TextDisabled("%s", name.c_str());
            if (!toast_.empty() && ImGui::GetTime() < toastUntil_) {
                ImGui::SameLine(std::max(120.0f, w - ImGui::CalcTextSize(toast_.c_str()).x - 16.0f));
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), "%s", toast_.c_str());
            }
            ImGui::EndMainMenuBar();
        }
        return h;
    }

    void handleShortcuts() {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput || fileBrowser_.isOpen() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
        if (out_) {
            if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) toggleOutput();
            if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_B, false)) out_->blackout = !out_->blackout;
        }
        if (!io.KeyCtrl) return;
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) { if (io.KeyShift) openSaveAs(); else saveProject(); }
        else if (ImGui::IsKeyPressed(ImGuiKey_O, false)) openOpenDialog();
        else if (ImGui::IsKeyPressed(ImGuiKey_N, false)) requestNew();
    }

    // Notices edits, and writes an autosave a minute after the project first has unsaved changes.
    void tickProjectState() {
        const double now = ImGui::GetTime();
        if (now - lastDirtyCheck_ > 0.5) {
            lastDirtyCheck_ = now;
            dirty_ = projectToJson(proj_, filePath_, true) != savedSnapshot_;
        }
        if (!dirty_) { lastAutosave_ = now; return; }
        if (now - lastAutosave_ > 60.0) { lastAutosave_ = now; autosaveNow(); }
    }

    void toast(const std::string& msg) { toast_ = msg; toastUntil_ = ImGui::GetTime() + 3.0; }
    void showError(const std::string& msg) { errorText_ = msg; wantError_ = true; }

    void rememberFile(const std::string& path) {
        addRecentFile(settings_, path);
        settings_.lastFolder = parentOf(path);
        saveSettings(settings_);
    }
    std::string startFolder() const {
        if (!filePath_.empty()) return parentOf(filePath_);
        return settings_.lastFolder.empty() ? documentsDirectory() : settings_.lastFolder;
    }

    bool doSave(const std::string& path) {
        std::string err;
        if (!saveProjectToFile(proj_, path, err)) { showError("Couldn't save the project.\n" + err); return false; }
        deleteAutosave(filePath_); deleteAutosave("");
        filePath_ = path;
        rememberFile(path);
        markClean();
        toast("Saved");
        return true;
    }

    void openOpenDialog() { fileAction_ = FileAction::OpenProject; fileBrowser_.open(FileBrowser::Mode::Open, "Open project", {"maphappy"}, startFolder()); }
    void openSaveAs() {
        fileAction_ = FileAction::SaveProject;
        fileBrowser_.open(FileBrowser::Mode::Save, "Save project as", {"maphappy"}, startFolder(), filePath_.empty() ? "Untitled" : fileNameOf(filePath_), "maphappy");
    }
    void openAddMedia() {
        fileAction_ = FileAction::AddMedia;
        fileBrowser_.open(FileBrowser::Mode::Open, "Add media", {"mp4", "mov", "m4v", "mkv", "webm", "avi", "wav", "mp3", "flac"}, startFolder());
    }
    void openRelinkMedia(int id) {
        fileAction_ = FileAction::RelinkMedia; fileTarget_ = id;
        MediaItem* m = proj_.media.find(id);
        fileBrowser_.open(FileBrowser::Mode::Open, "Find the video", {"mp4", "mov", "m4v", "mkv", "webm", "avi"}, m && !m->path.empty() ? parentOf(m->path) : startFolder());
    }
    void openRelinkAudio(int id) {
        fileAction_ = FileAction::RelinkAudio; fileTarget_ = id;
        AudioAsset* a = proj_.findAsset(id);
        fileBrowser_.open(FileBrowser::Mode::Open, "Find the song", {"wav", "mp3", "flac"}, a && !a->path.empty() ? parentOf(a->path) : startFolder());
    }

    void onFileChosen(const std::string& path) {
        switch (fileAction_) {
            case FileAction::OpenProject: requestOpenFile(path); break;
            case FileAction::SaveProject:
                if (doSave(path) && afterSave_ != Pending::None) { performPending(afterSave_, afterSavePath_, false); }
                afterSave_ = Pending::None;
                break;
            case FileAction::AddMedia: onFileDropped(path); settings_.lastFolder = parentOf(path); saveSettings(settings_); break;
            case FileAction::RelinkMedia: relinkMediaId_ = fileTarget_; relinkMediaPath_ = path; break;
            case FileAction::RelinkAudio:
                if (AudioAsset* a = proj_.findAsset(fileTarget_)) { a->path = path; a->missing = false; a->loading = true; audioLoader_.start(path, a->id); }
                break;
            default: break;
        }
    }

    void askUnsaved(Pending a, const std::string& path) { pending_ = a; pendingPath_ = path; wantUnsaved_ = true; }
    // `discard` = the user chose to throw away their unsaved changes, so the autosave of them goes too.
    void performPending(Pending a, const std::string& path, bool discard) {
        if (discard) { deleteAutosave(filePath_); deleteAutosave(""); }
        deferred_ = a; deferredPath_ = path;
    }

    void resetUiAfterLoad() { conductor_.resetSelection(); selectedMediaId_ = 0; status_.clear(); }

    void newProject() {
        proj_.resetToEmpty();
        proj_.addSurface(Shape::Quad);
        filePath_.clear();
        resetUiAfterLoad();
        markClean();
    }

    void showWarnings(const std::vector<std::string>& w) { if (!w.empty()) { loadWarnings_ = w; wantWarnings_ = true; } }
    void startAudioReloads(const std::vector<int>& ids) {
        for (int id : ids) if (AudioAsset* a = proj_.findAsset(id)) audioLoader_.start(a->path, id);
    }

    void openProjectNow(const std::string& path) {
        LoadReport rep;
        if (!loadProjectFromFile(proj_, path, rep)) { showError("Couldn't open the project.\n" + rep.error); return; }   // the open project is untouched
        filePath_ = path;
        resetUiAfterLoad();
        rememberFile(path);
        startAudioReloads(rep.audioToLoad);
        markClean();
        showWarnings(rep.warnings);
        if (autosaveIsNewer(path)) { recoverPath_ = path; wantRecover_ = true; }
    }

    void recoverNow() {
        std::string text;
        if (!readTextFile(autosavePathFor(recoverPath_), text)) { showError("Couldn't read the autosave."); return; }
        LoadReport rep;
        const std::string base = recoverPath_.empty() ? std::string() : parentOf(recoverPath_);
        if (!loadProjectFromJson(proj_, text, base, rep)) { showError("Couldn't recover the autosave.\n" + rep.error); return; }
        filePath_ = recoverPath_;
        resetUiAfterLoad();
        startAudioReloads(rep.audioToLoad);
        savedSnapshot_ = "";                  // the recovered work has never been saved
        dirty_ = true;
        showWarnings(rep.warnings);
        toast("Recovered unsaved work");
    }

    static void centerNextWindow() {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    }

    void drawDialogs() {
        drawOutputDialogs();
        // file browser (one instance serves every purpose)
        std::string chosen;
        const bool wasOpen = fileBrowser_.isOpen();
        if (fileBrowser_.draw(chosen)) onFileChosen(chosen);
        else if (wasOpen && !fileBrowser_.isOpen()) afterSave_ = Pending::None;       // cancelled: forget what was waiting on the save

        constexpr ImGuiWindowFlags kDlg = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;

        if (wantUnsaved_) { ImGui::OpenPopup("Unsaved changes"); wantUnsaved_ = false; }
        centerNextWindow();
        if (ImGui::BeginPopupModal("Unsaved changes", nullptr, kDlg)) {
            ImGui::Text("Save the changes to \"%s\" before continuing?", filePath_.empty() ? "Untitled" : fileNameOf(filePath_).c_str());
            ImGui::Spacing();
            if (ImGui::Button("Save", ImVec2(100, 0))) {
                afterSave_ = pending_; afterSavePath_ = pendingPath_;
                if (filePath_.empty()) openSaveAs();
                else if (doSave(filePath_)) { performPending(afterSave_, afterSavePath_, false); afterSave_ = Pending::None; }
                pending_ = Pending::None;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Don't save", ImVec2(100, 0))) { performPending(pending_, pendingPath_, true); pending_ = Pending::None; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0))) { pending_ = Pending::None; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

        if (wantError_) { ImGui::OpenPopup("Something went wrong"); wantError_ = false; }
        centerNextWindow();
        if (ImGui::BeginPopupModal("Something went wrong", nullptr, kDlg)) {
            ImGui::PushTextWrapPos(460.0f);
            ImGui::TextUnformatted(errorText_.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            if (ImGui::Button("OK", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (wantWarnings_) { ImGui::OpenPopup("Opened with problems"); wantWarnings_ = false; }
        centerNextWindow();
        if (ImGui::BeginPopupModal("Opened with problems", nullptr, kDlg)) {
            ImGui::TextUnformatted("The project opened, but some things need your attention:");
            ImGui::BeginChild("##warnlist", ImVec2(520, std::min(220.0f, 24.0f * (float)loadWarnings_.size() + 8.0f)), ImGuiChildFlags_Borders);
            ImGui::PushTextWrapPos(500.0f);
            for (const std::string& w : loadWarnings_) ImGui::BulletText("%s", w.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
            ImGui::TextDisabled("Missing videos and songs can be relinked from the Media tab and the Conductor's inspector.");
            if (ImGui::Button("OK", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (wantRecover_) { ImGui::OpenPopup("Recover unsaved work?"); wantRecover_ = false; }
        centerNextWindow();
        if (ImGui::BeginPopupModal("Recover unsaved work?", nullptr, kDlg)) {
            ImGui::TextUnformatted(recoverPath_.empty() ? "MapHappy found unsaved work from a previous session." : "MapHappy found newer unsaved changes for this project\n(it may have closed unexpectedly).");
            ImGui::Spacing();
            if (ImGui::Button("Recover", ImVec2(100, 0))) { deferredRecover_ = true; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Discard", ImVec2(100, 0))) { deleteAutosave(recoverPath_); ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    }

    // ------------------------------------------------------------------ audio loading
    void startAudioLoad(const std::string& path) {
        audioLoader_.start(path);
        proj_.audioStatus = "Loading " + baseName(path) + "...";
        proj_.audioStatusIsError = false;
    }
    // A finished song becomes an asset and lands on an audio track at the playhead.
    void pollAudio() {
        AudioLoader::Result r;
        while (audioLoader_.poll(r)) {
            if (r.assetId > 0) {                                   // a song a loaded project remembers
                AudioAsset* a = proj_.findAsset(r.assetId);
                if (!a) continue;
                a->loading = false;
                if (r.ok) { a->buf = r.asset.buf; a->peakMin = std::move(r.asset.peakMin); a->peakMax = std::move(r.asset.peakMax); a->missing = false; }
                else { a->missing = true; proj_.audioStatus = "Couldn't load " + a->name + ": " + r.error; proj_.audioStatusIsError = true; }
            } else if (r.ok) {                                     // a new song: it lands on an audio track at the playhead
                int id = proj_.addAudioAsset(std::move(r.asset));
                Project::Placement pl = proj_.placeAudio(id);
                if (pl.clipId) conductor_.selectAudioClip(pl.trackId, pl.clipId);
                proj_.audioStatus.clear();
                proj_.audioStatusIsError = false;
            } else {
                proj_.audioStatus = r.error;
                proj_.audioStatusIsError = true;
            }
        }
        if (audioLoader_.busy()) { proj_.audioStatus = "Loading " + audioLoader_.busyName() + "..."; proj_.audioStatusIsError = false; }
    }

    // ------------------------------------------------------------------ Media tab
    void addVideo(const std::string& path, bool assignToSelected) {
        int id = proj_.media.addVideo(path, status_);
        if (!id) return;
        selectedMediaId_ = id;
        if (assignToSelected) if (Surface* s = proj_.selected()) s->mediaId = id;
    }

    int defaultMedia() const { return proj_.media.find(selectedMediaId_) ? selectedMediaId_ : MediaPool::kPatternId; }

    void drawMediaThumbs(MediaKind kind) {
        const float thumbW = 96.0f, thumbH = 54.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.x + 8.0f;
        int perRow = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (thumbW + spacing)));
        int n = 0;
        for (auto& m : proj_.media.items) {
            if (m->kind != kind) continue;
            if (n % perRow != 0) ImGui::SameLine();
            ImGui::PushID(m->id);
            ImGui::BeginGroup();
            bool sel = m->id == selectedMediaId_;
            if (ImGui::ImageButton("##thumb", texId(m->source->texture()), ImVec2(thumbW, thumbH), ImVec2(0, 1), ImVec2(1, 0),
                                   sel ? ImVec4(0.15f, 0.78f, 0.82f, 1.0f) : ImVec4(0, 0, 0, 1)))
                selectedMediaId_ = m->id;
            std::string shortName = m->name.size() > 14 ? m->name.substr(0, 13) + "~" : m->name;
            if (m->missing) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", shortName.c_str());
            else ImGui::TextUnformatted(shortName.c_str());
            ImGui::EndGroup();
            ImGui::PopID();
            n++;
        }
        if (n == 0) ImGui::TextDisabled("None yet");
    }

    void drawMediaPanel(ImVec2 pos, ImVec2 size) {
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("Media", nullptr, kPanel);

        if (ImGui::Button("Add media...")) openAddMedia();
        ImGui::SameLine();
        ImGui::TextDisabled("or drag files onto the window");
        if (!status_.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "Error: %s", status_.c_str());

        ImGui::SeparatorText("Generators");
        drawMediaThumbs(MediaKind::Generator);
        ImGui::SeparatorText("Videos");
        drawMediaThumbs(MediaKind::Video);

        if (MediaItem* m = proj_.media.find(selectedMediaId_)) {
            ImGui::SeparatorText("Inspector");
            ImGui::Text("%s", m->name.c_str());
            if (m->missing) {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "File not found");
                ImGui::TextDisabled("%s", m->path.c_str());
                if (ImGui::Button("Relink...")) openRelinkMedia(m->id);
            } else {
                ImGui::TextDisabled("%dx%d", m->source->width(), m->source->height());
            }
            if (VideoSource* v = m->video()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%.2f fps", v->fps());
                ImGui::TextDisabled("%s", m->path.c_str());
            }
            if (!m->missing) ImGui::Checkbox("Playing", &m->playing);
            if (m->video()) {
                ImGui::SameLine();
                ImGui::Checkbox("Loop", &m->loop);
                ImGui::SameLine();
                if (ImGui::Button("Restart")) { std::string p = m->path; m->video()->open(p); }
            }
            if (Surface* s = proj_.selected()) {
                if (ImGui::Button("Use on selected surface")) s->mediaId = m->id;
            }
            if (m->id != MediaPool::kPatternId) {
                ImGui::SameLine();
                if (ImGui::Button("Remove")) pendingRemoveMedia_.push_back(m->id);
            }
        }
        ImGui::End();
    }

    // ------------------------------------------------------------------ Input view
    void drawInputPanel(ImVec2 pos, ImVec2 size) {
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("Input", nullptr, kPanel);
        Surface* s = proj_.selected();
        MediaItem* m = s ? proj_.media.find(s->mediaId) : nullptr;
        if (!s || !m) {
            ImGui::TextDisabled("Select a surface to choose which part of the media it shows.");
        } else {
            ImGui::TextDisabled("%s  -  %s", s->name.c_str(), m->name.c_str());
            View v = fitView(m->aspect(), 18.0f);
            inputRect_ = Rect{v.min.x, v.min.y, v.size.x, v.size.y, v.valid};
            if (v.valid) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 mx(v.min.x + v.size.x, v.min.y + v.size.y);
                dl->AddRectFilled(v.min, mx, IM_COL32(0, 0, 0, 255));
                dl->AddImage(texId(m->source->texture()), v.min, mx, ImVec2(0, 1), ImVec2(1, 0));
                dl->AddRect(v.min, mx, IM_COL32(90, 90, 95, 255));
                drawQuadOverlay(v, s->shape, s->in, true);
                if (!inTransformIsIdentity(*s)) { Vec2 ei[4]; effectiveIn(*s, m->aspect(), ei); drawEffectiveOutline(v, s->shape, ei); }
                ImGui::InvisibleButton("##input", ImGui::GetContentRegionAvail());
                interact(PanelInput, v);
            }
        }
        ImGui::End();
    }

    // ------------------------------------------------------------------ Stage view
    void drawStagePanel(ImVec2 pos, ImVec2 size) {
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("Stage", nullptr, kPanel);
        ImGui::TextDisabled("Output %dx%d", proj_.outW, proj_.outH);
        if (out_) {
            if (out_->isOpen) {
                ImGui::SameLine();
                const char* where = (out_->activeMonitor >= 0 && out_->activeMonitor < (int)out_->monitors.size()) ? out_->monitors[out_->activeMonitor].name.c_str() : "";
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), "  LIVE%s on %s", out_->isFullscreen ? " (fullscreen)" : " (window)", where);
            }
            if (out_->blackout) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "  BLACKOUT"); }
            if (!out_->message.empty()) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "  %s", out_->message.c_str()); }
        }
        View v = fitView((float)proj_.outW / (float)proj_.outH, 18.0f);
        stageRect_ = Rect{v.min.x, v.min.y, v.size.x, v.size.y, v.valid};
        if (v.valid) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 mx(v.min.x + v.size.x, v.min.y + v.size.y);
            dl->AddRectFilled(v.min, mx, IM_COL32(0, 0, 0, 255));
            dl->AddImage(texId(stage_.tex), v.min, mx, ImVec2(0, 1), ImVec2(1, 0));
            dl->AddRect(v.min, mx, IM_COL32(90, 90, 95, 255));
            const float stageAspect = (float)proj_.outW / (float)std::max(1, proj_.outH);
            for (Surface& s : proj_.surfaces)
                if (s.visible && s.id != proj_.selectedSurfaceId) { Vec2 eo[4]; effectiveOut(s, stageAspect, eo); drawQuadOverlay(v, s.shape, eo, false); }
            if (Surface* sel = proj_.selected()) {
                drawQuadOverlay(v, sel->shape, sel->out, true);                       // the mapped quad, with its handles
                if (!outTransformIsIdentity(*sel)) { Vec2 eo[4]; effectiveOut(*sel, stageAspect, eo); drawEffectiveOutline(v, sel->shape, eo); }
            }
            ImGui::InvisibleButton("##stage", ImGui::GetContentRegionAvail());
            interact(PanelStage, v);
        }
        ImGui::End();
    }

    // ------------------------------------------------------------------ state
    Project&      proj_;
    RenderTarget& stage_;
    int           leftTab_  = 1;          // 0 = Media, 1 = Surfaces
    int           viewMode_ = 0;          // 0 = both, 1 = input only, 2 = stage only
    int           selectedMediaId_ = 0;
    char          pathBuf_[512];
    std::string   status_;
    Drag          drag_;
    ConductorPanel conductor_;
    AudioLoader   audioLoader_;
    std::vector<int> pendingRemoveMedia_;

    // project file state
    FileBrowser   fileBrowser_;
    FileAction    fileAction_ = FileAction::None;
    int           fileTarget_ = 0;
    AppSettings   settings_;
    std::string   filePath_, savedSnapshot_;
    bool          dirty_ = false, exitApproved_ = false;
    double        lastDirtyCheck_ = 0.0, lastAutosave_ = 0.0, toastUntil_ = 0.0;
    std::string   toast_;
    Pending       pending_ = Pending::None, afterSave_ = Pending::None, deferred_ = Pending::None;
    std::string   pendingPath_, afterSavePath_, deferredPath_, recoverPath_, errorText_, relinkMediaPath_;
    int           relinkMediaId_ = 0;
    bool          wantUnsaved_ = false, wantError_ = false, wantWarnings_ = false, wantRecover_ = false, deferredRecover_ = false;
    std::vector<std::string> loadWarnings_;
    Rect          stageRect_, inputRect_;
    std::map<std::string, Rect> rects_;
    OutputState*  out_ = nullptr;
    int           customW_ = 1920, customH_ = 1080;
    bool          wantCustomSize_ = false;
};
