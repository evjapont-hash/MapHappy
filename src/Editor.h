#pragma once
#include "AudioLoader.h"
#include "ConductorPanel.h"
#include "FileBrowser.h"
#include "OutputState.h"
#include "ProjectIO.h"
#include "Presets.h"
#include "GLUtil.h"
#include "Project.h"
#include "Theme.h"
#include "UndoHistory.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

inline ImTextureID texId(GLuint t) { return (ImTextureID)(intptr_t)t; }
// The whole editor UI: toolbar, left panel (Media / Surfaces), Input + Stage views, Conductor area.
class Editor {
public:
    Editor(Project& p, RenderTarget& stage) : proj_(p), stage_(stage), conductor_(p) {
        pathBuf_[0] = '\0';
        settings_ = loadSettings();
        conductor_.onRelinkAudio = [this](int assetId) { openRelinkAudio(assetId); };
    }

    const ConductorPanel& conductor() const { return conductor_; }

    // ---- live performance (busking): the number keys play presets
    bool liveMode() const { return liveMode_; }
    void setLiveMode(bool on) { liveMode_ = on; }
    const PresetPlayer& presetPlayer() const { return player_; }
    // Stores the current look in a slot (0..9; key 1..9, 0), keeping its name and fade time if it had them.
    void storePreset(int slot) {
        if (slot < 0 || slot >= kPresetSlots) return;
        Preset& old = proj_.presets[slot];
        const std::string name = old.used ? old.name : "Preset " + std::to_string(presetKeyDigit(slot));
        old = capturePreset(proj_, name, old.used ? old.fade : 0.5f);
        toast("Stored \"" + name + "\" on key " + std::to_string(presetKeyDigit(slot)));
    }
    // Plays the preset in a slot (cut or cross-fade). Returns false for an empty slot.
    bool recallPreset(int slot) {
        if (slot < 0 || slot >= kPresetSlots) return false;
        if (!proj_.presets[slot].used) { toast("Key " + std::to_string(presetKeyDigit(slot)) + " has no preset yet: Shift+" + std::to_string(presetKeyDigit(slot)) + " stores the current look"); return false; }
        player_.start(proj_, proj_.presets[slot], slot);
        return true;
    }

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
    // ---- undo: every finished edit is a step (a whole drag is one)
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }
    void undo() { pendingUndo_ = -1; }                              // done between frames (it may free textures): see endFrame()
    void redo() { pendingUndo_ = +1; }
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
    Rect uiRect(const std::string& name) const {
        auto it = rects_.find(name);
        if (it != rects_.end()) return it->second;
        const ConductorPanel::Rect c = conductor_.rect(name);
        return Rect{c.x, c.y, c.w, c.h, c.valid};
    }
    Rect inputRect() const { return inputRect_; }

    void drawUI() {
        rects_.clear();
        proj_.syncLines();
        player_.update(proj_, ImGui::GetIO().DeltaTime);              // a preset cross-fading in
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

        if (liveMode_) {                                              // the preset pads, with the Conductor's transport bar below
            const float padH = std::max(70.0f, bottomH - 64.0f);
            drawPresetPad(ImVec2(centerX, centerY + centerH), ImVec2(centerW, padH));
            conductor_.draw(ImVec2(centerX, centerY + centerH + padH), ImVec2(centerW, bottomH - padH));
        } else {
            conductor_.draw(ImVec2(centerX, centerY + centerH), ImVec2(centerW, bottomH));
        }
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
        proj_.media.applyResizes();                                // a generator given a new picture size gets its new texture

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
        if (pendingUndo_) { const int dir = pendingUndo_; pendingUndo_ = 0; stepHistory(dir); }
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
        if (active) pushOn();
        bool clicked = ImGui::Button(label, ImVec2(86, 0));
        if (active) popOn();
        return clicked;
    }

    // Outline, diagonals and handles for one surface's quad (and its shape). `isStage` = the quad is the output one.
    void drawSurfaceOverlay(const View& v, const Surface& s, const Vec2 q[4], bool selected, bool isStage) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 col = selected ? kAccent : IM_COL32(255, 255, 255, 110);
        ImVec2 p[4];
        for (int i = 0; i < 4; i++) p[i] = v.toScreen(q[i]);
        overlayPolyline(dl, p, 4, col, ImDrawFlags_Closed, selected ? 1.5f : 1.0f);

        if (s.shape != Shape::Quad && s.shape != Shape::Line) {
            std::vector<ImVec2> pts;
            for (Vec2 o : outlineInQuad(s, q)) pts.push_back(v.toScreen(o));
            if (pts.size() > 2) overlayPolyline(dl, pts.data(), (int)pts.size(), col, ImDrawFlags_Closed, 2.0f);
        }
        if (!selected) return;

        auto moveHandle = [&](ImVec2 c) {
            dl->AddCircle(c, 6.0f, kAccent, 0, 1.5f);
            dl->AddLine(ImVec2(c.x - 9, c.y), ImVec2(c.x + 9, c.y), kAccent, 1.5f);
            dl->AddLine(ImVec2(c.x, c.y - 9), ImVec2(c.x, c.y + 9), kAccent, 1.5f);
        };
        auto cornerHandle = [&](ImVec2 c, float r, ImU32 ring) {
            dl->AddCircleFilled(c, r, IM_COL32(0, 0, 0, 255));
            dl->AddCircleFilled(c, r - 1.5f, ring);
        };
        if (isStage && s.shape == Shape::Line) {                     // a line: its two ends, and a move handle in the middle
            const ImVec2 a = v.toScreen(s.lineA), b = v.toScreen(s.lineB);
            dl->AddLine(a, b, kAccentDim, 1.0f);
            cornerHandle(a, 8.0f, kAccent); cornerHandle(b, 8.0f, kAccent);
            moveHandle(ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f));
            return;
        }
        const bool freeform = isStage && s.shape == Shape::Freeform;
        dl->AddLine(p[0], p[2], kAccentDim, 1.0f);
        dl->AddLine(p[1], p[3], kAccentDim, 1.0f);
        for (int i = 0; i < 4; i++) cornerHandle(p[i], freeform ? 6.0f : 8.0f, freeform ? kAccentDim : kAccent);   // a freeform's corners warp all its points
        if (freeform) {
            Mat3 H;
            if (squareToQuad(q, H))
                for (Vec2 u : s.poly) {
                    const ImVec2 c = v.toScreen(mapPoint(H, u));
                    dl->AddRectFilled(ImVec2(c.x - 5, c.y - 5), ImVec2(c.x + 5, c.y + 5), kAccent);
                    dl->AddRect(ImVec2(c.x - 5, c.y - 5), ImVec2(c.x + 5, c.y + 5), IM_COL32(20, 20, 24, 255));
                }
        }
        moveHandle(v.toScreen(quadCenter(q)));
    }

    // A thin, fainter outline of where a transformed surface really is, drawn alongside its mapped quad.
    void drawEffectiveOutline(const View& v, const Surface& s, const Vec2 q[4]) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 col = IM_COL32(255, 255, 255, 150);
        ImVec2 p[4];
        for (int i = 0; i < 4; i++) p[i] = v.toScreen(q[i]);
        overlayPolyline(dl, p, 4, col, ImDrawFlags_Closed, 1.0f);
        if (s.shape != Shape::Quad && s.shape != Shape::Line) {
            std::vector<ImVec2> pts;
            for (Vec2 o : outlineInQuad(s, q)) pts.push_back(v.toScreen(o));
            if (pts.size() > 2) overlayPolyline(dl, pts.data(), (int)pts.size(), col, ImDrawFlags_Closed, 1.5f);
        }
    }

    enum Panel { PanelInput = 0, PanelStage = 1 };
    // Drag handles: 0..3 corners, 4 move everything, 10 / 11 a line's ends, 100 + i freeform point i.
    static constexpr int kHandleMove = 4, kHandleLineA = 10, kHandleLineB = 11, kHandlePoint = 100;
    struct Drag { int handle = -1; Panel panel = PanelInput; int surfaceId = 0; Vec2 prev{0, 0}; };

    // Freeform points on the stage (through the MAPPED quad), and a stage position back to the quad's unit square.
    static bool polyToStage(const Surface& s, std::vector<Vec2>& out) {
        Mat3 H; out.clear();
        if (!squareToQuad(s.out, H)) return false;
        for (Vec2 u : s.poly) out.push_back(mapPoint(H, u));
        return true;
    }
    static bool stageToUnit(const Surface& s, Vec2 stage, Vec2& unit) {
        Mat3 H, inv;
        if (!squareToQuad(s.out, H) || !invert(H, inv)) return false;
        unit = mapPoint(inv, stage);
        if (!std::isfinite(unit.x) || !std::isfinite(unit.y)) return false;
        unit = clampUnit(unit);                                       // points stay inside the quad: the picture ends at its edges
        return true;
    }

    // Freeform editing on the stage: double-click an edge to add a point there, right-click a point to delete it.
    void editFreeformPoints(const View& v) {
        Surface* s = proj_.selected();
        if (!s || s->shape != Shape::Freeform || !ImGui::IsItemHovered()) return;
        ImGuiIO& io = ImGui::GetIO();
        std::vector<Vec2> pts;
        if (!polyToStage(*s, pts)) return;
        auto screenDist = [&](Vec2 a) { const ImVec2 p = v.toScreen(a); return std::hypot(p.x - io.MousePos.x, p.y - io.MousePos.y); };
        if (ImGui::IsMouseClicked(1)) {
            int best = -1; float bd = 10.0f;
            for (int i = 0; i < (int)pts.size(); i++) { const float d = screenDist(pts[i]); if (d < bd) { bd = d; best = i; } }
            if (best >= 0 && s->poly.size() > 3) s->poly.erase(s->poly.begin() + best);
        }
        if (ImGui::IsMouseDoubleClicked(0) && (int)s->poly.size() < kMaxPolyPoints) {
            int best = -1; float bd = 10.0f; Vec2 at{0, 0};
            for (int i = 0; i < (int)pts.size(); i++) {            // the edge from point i to point i+1
                const ImVec2 a = v.toScreen(pts[i]), b = v.toScreen(pts[(i + 1) % pts.size()]);
                const float ex = b.x - a.x, ey = b.y - a.y, len2 = std::max(1e-6f, ex * ex + ey * ey);
                const float t = std::max(0.0f, std::min(1.0f, ((io.MousePos.x - a.x) * ex + (io.MousePos.y - a.y) * ey) / len2));
                const float d = std::hypot(a.x + ex * t - io.MousePos.x, a.y + ey * t - io.MousePos.y);
                if (d < bd) { bd = d; best = i; at = v.toNorm(ImVec2(a.x + ex * t, a.y + ey * t)); }
            }
            Vec2 unit;
            if (best >= 0 && stageToUnit(*s, at, unit)) {
                s->poly.insert(s->poly.begin() + best + 1, unit);
                drag_.handle = kHandlePoint + best + 1; drag_.panel = PanelStage; drag_.surfaceId = s->id; drag_.prev = at;
            }
        }
    }

    // Mouse handling for a view. Must be called right after the InvisibleButton covering it.
    void interact(Panel panel, const View& v) {
        ImGuiIO& io = ImGui::GetIO();
        const bool isStage = panel == PanelStage;
        const Vec2 mouse = v.toNorm(io.MousePos);
        auto screenDist = [&](Vec2 a) { const ImVec2 p = v.toScreen(a); return std::hypot(p.x - io.MousePos.x, p.y - io.MousePos.y); };

        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
            drag_.handle = -1;
            int handle = -1, surfaceId = 0;
            if (Surface* s = proj_.selected()) {
                const Vec2* q = isStage ? s->out : s->in;
                float best = 14.0f;
                if (isStage && (handle = maskHandleAt(*s, v)) >= 0) {
                    // a handle of the selected mask
                } else if (isStage && s->shape == Shape::Line) {
                    if (screenDist(s->lineA) < best) { best = screenDist(s->lineA); handle = kHandleLineA; }
                    if (screenDist(s->lineB) < best) { best = screenDist(s->lineB); handle = kHandleLineB; }
                } else {
                    std::vector<Vec2> pts;
                    if (isStage && s->shape == Shape::Freeform && polyToStage(*s, pts)) {   // points before corners: they are what you edit most
                        float bp = 10.0f;
                        for (int i = 0; i < (int)pts.size(); i++) if (screenDist(pts[i]) < bp) { bp = screenDist(pts[i]); handle = kHandlePoint + i; }
                    }
                    if (handle < 0) for (int i = 0; i < 4; i++) if (screenDist(q[i]) < best) { best = screenDist(q[i]); handle = i; }
                }
                if (handle < 0 && pointInPolygon(outlineInQuad(*s, q), mouse)) handle = kHandleMove;
                if (handle < 0 && isStage) { Vec2 eo[4]; effectiveOut(*s, proj_.stageAspect(), eo); if (pointInPolygon(outlineInQuad(*s, eo), mouse)) handle = kHandleMove; }
                if (handle >= 0) surfaceId = s->id;
            }
            if (handle < 0 && isStage) {          // clicked empty space or another surface: select the top one under the mouse
                for (const Surface& o : proj_.surfaces) {
                    Vec2 eo[4]; effectiveOut(o, proj_.stageAspect(), eo);      // click what you can SEE
                    if (proj_.isShown(o) && pointInPolygon(outlineInQuad(o, eo), mouse)) {
                        proj_.selectedSurfaceId = o.id;
                        handle = kHandleMove; surfaceId = o.id;
                        break;
                    }
                }
            }
            if (handle >= 0) { drag_.handle = handle; drag_.panel = panel; drag_.surfaceId = surfaceId; drag_.prev = mouse; }
        }
        if (isStage && !editMaskPoints(v)) editFreeformPoints(v);

        if (drag_.handle >= 0 && drag_.panel == panel) {
            Surface* s = proj_.find(drag_.surfaceId);
            if (!s || !ImGui::IsMouseDown(0)) { drag_.handle = -1; return; }
            Vec2* q = isStage ? s->out : s->in;
            if (drag_.handle == kHandleLineA || drag_.handle == kHandleLineB) {
                Vec2& end = drag_.handle == kHandleLineA ? s->lineA : s->lineB;
                const Vec2 other = drag_.handle == kHandleLineA ? s->lineB : s->lineA;
                if (screenDist(other) > 4.0f) end = mouse;                  // the two ends can't meet (a line needs a direction)
                syncLineQuad(*s, proj_.stageAspect());
            } else if (drag_.handle >= kHandleMaskCorner) {
                dragMask(*s, mouse);
            } else if (drag_.handle >= kHandlePoint) {
                const int i = drag_.handle - kHandlePoint;
                Vec2 unit;
                if (i < (int)s->poly.size() && stageToUnit(*s, mouse, unit)) s->poly[i] = unit;
            } else if (drag_.handle < 4) {
                Vec2 old = q[drag_.handle];
                q[drag_.handle] = mouse;
                if (!isConvex(q)) q[drag_.handle] = old;      // a bow-tie or dented quad has no valid projection
            } else {
                float dx = mouse.x - drag_.prev.x, dy = mouse.y - drag_.prev.y;
                for (int i = 0; i < 4; i++) { q[i].x += dx; q[i].y += dy; }
                if (isStage && s->shape == Shape::Line) { s->lineA.x += dx; s->lineA.y += dy; s->lineB.x += dx; s->lineB.y += dy; syncLineQuad(*s, proj_.stageAspect()); }
            }
            drag_.prev = mouse;
        }
    }

    // ------------------------------------------------------------------ masks (on the stage and in the inspector)
    static constexpr int kHandleMaskCorner = 1000, kHandleMaskMove = 1010, kHandleMaskPoint = 1100;
    static const ImU32 kMaskColor = IM_COL32(255, 255, 255, 255);

    Mask* selectedMask(Surface& s) { return (selectedMask_ >= 0 && selectedMask_ < (int)s.masks.size()) ? &s.masks[selectedMask_] : nullptr; }
    static Vec2 boxCorner(const Mask& m, int i) {
        return i == 0 ? m.min : i == 1 ? Vec2{m.max.x, m.min.y} : i == 2 ? m.max : Vec2{m.min.x, m.max.y};
    }
    // Mask outline (or any unit-square points) on the stage, through the surface's MAPPED quad.
    static bool unitPointsToStage(const Surface& s, const std::vector<Vec2>& unit, std::vector<Vec2>& out) {
        Mat3 H; out.clear();
        if (!squareToQuad(s.out, H)) return false;
        for (Vec2 u : unit) out.push_back(mapPoint(H, u));
        return true;
    }

    // Which handle of the selected mask is under the mouse (-1 = none).
    int maskHandleAt(Surface& s, const View& v) {
        Mask* m = selectedMask(s);
        if (!m) return -1;
        ImGuiIO& io = ImGui::GetIO();
        auto dist = [&](Vec2 a) { const ImVec2 p = v.toScreen(a); return std::hypot(p.x - io.MousePos.x, p.y - io.MousePos.y); };
        std::vector<Vec2> pts, unit;
        if (m->shape == MaskShape::Polygon) unit = m->points;
        else for (int i = 0; i < 4; i++) unit.push_back(boxCorner(*m, i));
        if (!unitPointsToStage(s, unit, pts)) return -1;
        int best = -1; float bd = 11.0f;
        for (int i = 0; i < (int)pts.size(); i++) if (dist(pts[i]) < bd) { bd = dist(pts[i]); best = i; }
        if (best >= 0) return (m->shape == MaskShape::Polygon ? kHandleMaskPoint : kHandleMaskCorner) + best;
        std::vector<Vec2> outline;
        if (unitPointsToStage(s, maskOutline(*m), outline) && pointInPolygon(outline, v.toNorm(io.MousePos))) return kHandleMaskMove;
        return -1;
    }

    void dragMask(Surface& s, Vec2 mouse) {
        Mask* m = selectedMask(s);
        Vec2 u, prev;
        if (!m || !stageToUnit(s, mouse, u)) return;
        if (drag_.handle >= kHandleMaskPoint) {
            const int i = drag_.handle - kHandleMaskPoint;
            if (i < (int)m->points.size()) m->points[i] = u;
        } else if (drag_.handle == kHandleMaskMove) {
            if (!stageToUnit(s, drag_.prev, prev)) return;
            float dx = u.x - prev.x, dy = u.y - prev.y;
            if (m->shape == MaskShape::Polygon) { for (Vec2& p : m->points) p = clampUnit(Vec2{p.x + dx, p.y + dy}); }
            else {                                                       // the box keeps its size and stays inside the surface
                dx = std::max(-m->min.x, std::min(1.0f - m->max.x, dx)); dy = std::max(-m->min.y, std::min(1.0f - m->max.y, dy));
                m->min.x += dx; m->max.x += dx; m->min.y += dy; m->max.y += dy;
            }
        } else {                                                         // a box corner: the opposite corner stays put
            const Vec2 o = boxCorner(*m, (drag_.handle - kHandleMaskCorner + 2) % 4);
            if (std::fabs(u.x - o.x) < 0.01f || std::fabs(u.y - o.y) < 0.01f) return;
            m->min = {std::min(u.x, o.x), std::min(u.y, o.y)}; m->max = {std::max(u.x, o.x), std::max(u.y, o.y)};
            const bool right = u.x > o.x, down = u.y > o.y;              // dragged past the other side: it is now another corner
            drag_.handle = kHandleMaskCorner + (down ? (right ? 2 : 3) : (right ? 1 : 0));
        }
    }

    // Polygon masks on the stage: double-click an edge to add a point, right-click a point to delete it.
    // Returns true when a polygon mask is selected (it then gets these clicks instead of a freeform outline).
    bool editMaskPoints(const View& v) {
        Surface* s = proj_.selected();
        Mask* m = s ? selectedMask(*s) : nullptr;
        if (!m || m->shape != MaskShape::Polygon) return false;
        if (!ImGui::IsItemHovered()) return true;
        ImGuiIO& io = ImGui::GetIO();
        std::vector<Vec2> pts;
        if (!unitPointsToStage(*s, m->points, pts)) return true;
        if (ImGui::IsMouseClicked(1)) {
            int best = -1; float bd = 10.0f;
            for (int i = 0; i < (int)pts.size(); i++) { const ImVec2 p = v.toScreen(pts[i]); const float d = std::hypot(p.x - io.MousePos.x, p.y - io.MousePos.y); if (d < bd) { bd = d; best = i; } }
            if (best >= 0 && m->points.size() > 3) m->points.erase(m->points.begin() + best);
        }
        if (ImGui::IsMouseDoubleClicked(0) && (int)m->points.size() < kMaxMaskPoints) {
            int best = -1; float bd = 10.0f; Vec2 at{0, 0};
            for (int i = 0; i < (int)pts.size(); i++) {
                const ImVec2 a = v.toScreen(pts[i]), b = v.toScreen(pts[(i + 1) % pts.size()]);
                const float ex = b.x - a.x, ey = b.y - a.y, len2 = std::max(1e-6f, ex * ex + ey * ey);
                const float t = std::max(0.0f, std::min(1.0f, ((io.MousePos.x - a.x) * ex + (io.MousePos.y - a.y) * ey) / len2));
                const float d = std::hypot(a.x + ex * t - io.MousePos.x, a.y + ey * t - io.MousePos.y);
                if (d < bd) { bd = d; best = i; at = v.toNorm(ImVec2(a.x + ex * t, a.y + ey * t)); }
            }
            Vec2 unit;
            if (best >= 0 && stageToUnit(*s, at, unit)) {
                m->points.insert(m->points.begin() + best + 1, unit);
                drag_.handle = kHandleMaskPoint + best + 1; drag_.panel = PanelStage; drag_.surfaceId = s->id; drag_.prev = at;
            }
        }
        return true;
    }

    // The selected surface's masks on the stage: all their outlines, and the selected one's handles.
    void drawMaskOverlay(const View& v, const Surface& s) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int i = 0; i < (int)s.masks.size(); i++) {
            const Mask& m = s.masks[i];
            std::vector<Vec2> pts;
            if (!unitPointsToStage(s, maskOutline(m), pts) || pts.size() < 2) continue;
            std::vector<ImVec2> sp; for (Vec2 p : pts) sp.push_back(v.toScreen(p));
            const bool sel = i == selectedMask_;
            ImU32 col = sel ? kMaskColor : IM_COL32(255, 255, 255, m.enabled ? 130 : 60);
            overlayPolyline(dl, sp.data(), (int)sp.size(), col, ImDrawFlags_Closed, sel ? 2.0f : 1.0f);
            if (!sel) continue;
            std::vector<Vec2> unit, hp;
            if (m.shape == MaskShape::Polygon) unit = m.points; else for (int k = 0; k < 4; k++) unit.push_back(boxCorner(m, k));
            unitPointsToStage(s, unit, hp);
            for (Vec2 h : hp) { const ImVec2 c = v.toScreen(h); dl->AddRectFilled(ImVec2(c.x - 5, c.y - 5), ImVec2(c.x + 5, c.y + 5), IM_COL32(0, 0, 0, 255)); dl->AddRect(ImVec2(c.x - 5, c.y - 5), ImVec2(c.x + 5, c.y + 5), kMaskColor, 0.0f, 0, 2.0f); }   // hollow, unlike a surface's handles
        }
    }

    // The Masks section of the surface inspector.
    void drawMasksSection(Surface& s) {
        const bool open = ImGui::CollapsingHeader("Masks");
        recordRect("header:Masks");
        if (!open) return;
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("Masks cut parts of the surface away. Select one to drag its handles on the stage.");
        ImGui::PopStyleColor();
        const bool full = (int)s.masks.size() >= kMaxMasks;
        ImGui::BeginDisabled(full);
        for (MaskShape shape : {MaskShape::Rectangle, MaskShape::Ellipse, MaskShape::Polygon}) {
            const std::string label = std::string("+ ") + maskShapeName(shape);
            if (shape != MaskShape::Rectangle) ImGui::SameLine();
            if (ImGui::Button(label.c_str())) {
                int n = 1;
                for (;; n++) { bool used = false; for (const Mask& m : s.masks) used |= m.name == "Mask " + std::to_string(n); if (!used) break; }
                s.masks.push_back(makeMask(shape, "Mask " + std::to_string(n)));
                selectedMask_ = (int)s.masks.size() - 1;
            }
            recordRect("button:" + label);
        }
        ImGui::EndDisabled();
        if (full) ImGui::TextDisabled("(at most %d masks per surface)", kMaxMasks);

        int remove = -1;
        for (int i = 0; i < (int)s.masks.size(); i++) {
            Mask& m = s.masks[i];
            ImGui::PushID(i);
            ImGui::Checkbox("##menabled", &m.enabled);
            recordRect("maskEnabled:" + m.name);
            ImGui::SameLine();
            const std::string label = m.name + (m.mode == MaskMode::ShowOnly ? "  (show only)" : "  (hide)");
            if (ImGui::Selectable(label.c_str(), i == selectedMask_)) selectedMask_ = i == selectedMask_ ? -1 : i;
            recordRect("mask:" + m.name);
            ImGui::PopID();
        }
        if (Mask* m = selectedMask(s)) {
            ImGui::Indent(8.0f);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s", m->name.c_str());
            if (ImGui::InputText("Mask name", buf, sizeof(buf)) && buf[0]) m->name = buf;
            int mode = (int)m->mode;
            const char* modes[] = {"Hide what is inside", "Show only what is inside"};
            if (ImGui::Combo("Mode##mask", &mode, modes, 2)) m->mode = (MaskMode)mode;
            recordRect("combo:Mask mode");
            float f = m->feather * 100.0f;
            if (ImGui::SliderFloat("Mask feather", &f, 0.0f, 50.0f, "%.0f%%")) m->feather = f / 100.0f;
            if (m->shape != MaskShape::Polygon) {
                float a[2] = {m->min.x, m->min.y}, b[2] = {m->max.x, m->max.y};
                if (ImGui::DragFloat2("Top-left", a, 0.002f, 0.0f, 1.0f, "%.3f")) { m->min = {std::min(a[0], m->max.x - 0.01f), std::min(a[1], m->max.y - 0.01f)}; }
                if (ImGui::DragFloat2("Bottom-right", b, 0.002f, 0.0f, 1.0f, "%.3f")) { m->max = {std::max(b[0], m->min.x + 0.01f), std::max(b[1], m->min.y + 0.01f)}; }
            } else {
                ImGui::TextDisabled("%d points. Double-click an edge to add one, right-click a point to delete it.", (int)m->points.size());
            }
            if (ImGui::Button("Delete mask")) remove = selectedMask_;
            recordRect("button:Delete mask");
            ImGui::Unindent(8.0f);
        }
        if (remove >= 0) { s.masks.erase(s.masks.begin() + remove); selectedMask_ = -1; }
    }

    // ------------------------------------------------------------------ live preset pads
    void drawPresetPad(ImVec2 pos, ImVec2 size) {
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("Live presets", nullptr, kPanel | ImGuiWindowFlags_NoScrollbar);
        ImGui::TextDisabled("Keys 1-9 and 0 play a preset.  Shift + key stores the current look.  Right-click a pad to rename it, set its fade or clear it.");
        const float gap = 6.0f;
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float w = std::max(40.0f, (avail.x - gap * (kPresetSlots - 1)) / kPresetSlots), h = std::max(36.0f, avail.y - 2.0f);
        for (int slot = 0; slot < kPresetSlots; slot++) {
            Preset& pr = proj_.presets[slot];
            const int key = presetKeyDigit(slot);
            const bool active = player_.activeSlot() == slot && pr.used;
            ImGui::PushID(slot);
            if (slot > 0) ImGui::SameLine(0, gap);
            int pushed = 0;
            if (active)       { pushOn(); pushed = 4; }
            else if (pr.used) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.22f, 0.22f, 1.0f)); pushed++; }
            else              { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]); pushed++; }   // empty: dimmed
            const std::string label = std::to_string(key) + "\n" + (pr.used ? pr.name : std::string("(empty)"));
            const bool clicked = ImGui::Button(label.c_str(), ImVec2(w, h));
            ImGui::PopStyleColor(pushed);
            recordRect("pad:" + std::to_string(key));
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            if (active && player_.fading())                             // how far the cross-fade has got
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(a.x, b.y - 4), ImVec2(a.x + (b.x - a.x) * player_.progress(), b.y), IM_COL32(0, 0, 0, 255));
            if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("##padctx"))
                ImGui::SetTooltip(pr.used ? "Click or press %d to play (fade %.1f s)" : "Click or press Shift+%d to store the current look here", key, (double)pr.fade);
            if (clicked) { if (pr.used) recallPreset(slot); else storePreset(slot); }
            if (ImGui::BeginPopupContextItem("##padctx")) {
                if (ImGui::MenuItem("Store the current look here", ("Shift+" + std::to_string(key)).c_str())) storePreset(slot);
                if (pr.used) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%s", pr.name.c_str());
                    ImGui::SetNextItemWidth(160);
                    if (ImGui::InputText("Name##preset", buf, sizeof(buf)) && buf[0]) pr.name = buf;
                    ImGui::SetNextItemWidth(160);
                    ImGui::SliderFloat("Fade##preset", &pr.fade, 0.0f, 10.0f, pr.fade <= 0.0f ? "cut" : "%.1f s");
                    recordRect("slider:presetFade");
                    if (ImGui::MenuItem("Play")) recallPreset(slot);
                    if (ImGui::MenuItem("Clear")) { pr = Preset(); if (player_.activeSlot() == slot) player_.reset(); }
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::End();
    }

    // ------------------------------------------------------------------ toolbar
    void drawToolbar(ImVec2 pos, float width, float height) {
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(ImVec2(width, height));
        ImGui::Begin("##toolbar", nullptr, kPanel | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        if (tabButton("Media", leftTab_ == 0))    leftTab_ = 0;
        recordRect("tab:Media");
        ImGui::SameLine();
        if (tabButton("Surfaces", leftTab_ == 1)) leftTab_ = 1;
        recordRect("tab:Surfaces");
        ImGui::SameLine(0, 40);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Views:");
        ImGui::SameLine();
        if (tabButton("Both", viewMode_ == 0))  viewMode_ = 0;
        ImGui::SameLine();
        if (tabButton("Input", viewMode_ == 1)) viewMode_ = 1;
        ImGui::SameLine();
        if (tabButton("Stage", viewMode_ == 2)) viewMode_ = 2;
        ImGui::SameLine(0, 40);
        const bool wasLive = liveMode_;                                 // (the click below may change it)
        if (wasLive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.20f, 0.30f, 1.0f));
        if (ImGui::Button(wasLive ? "LIVE" : "Live", ImVec2(86, 0))) liveMode_ = !liveMode_;
        recordRect("button:Live");
        if (wasLive) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Live performance mode (F5): the number keys 1-9 and 0 play your presets");
        if (out_) {
            ImGui::SameLine(std::max(width - 90.0f - 214.0f, 0.0f));
            const bool live = out_->isOpen;
            if (live) pushOn();
            if (ImGui::Button(live ? "Output: ON" : "Output", ImVec2(100, 0))) toggleOutput();
            recordRect("button:Output");
            if (live) popOn();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show or hide the projector output window (F11)");
            ImGui::SameLine();
            const bool black = out_->blackout;
            if (black) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.15f, 0.15f, 1.0f));
            if (ImGui::Button(black ? "BLACKOUT" : "Blackout", ImVec2(100, 0))) out_->blackout = !out_->blackout;
            recordRect("button:Blackout");
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
        if (proj_.selectedSurfaceId != lastSelectedSurface_) { lastSelectedSurface_ = proj_.selectedSurfaceId; selectedMask_ = -1; if (proj_.selectedSurfaceId) selectedFolderId_ = 0; }   // e.g. clicked on the stage
        if (selectedFolderId_ && !proj_.findFolder(selectedFolderId_)) selectedFolderId_ = 0;

        if (ImGui::Button("+ Quad"))     addSurfaceHere(Shape::Quad);
        recordRect("button:+ Quad");
        ImGui::SameLine();
        if (ImGui::Button("+ Circle"))   addSurfaceHere(Shape::Circle);
        recordRect("button:+ Circle");
        ImGui::SameLine();
        if (ImGui::Button("+ Triangle")) addSurfaceHere(Shape::Triangle);
        recordRect("button:+ Triangle");
        if (ImGui::Button("+ Line"))     addSurfaceHere(Shape::Line);
        recordRect("button:+ Line");
        ImGui::SameLine();
        if (ImGui::Button("+ Freeform")) addSurfaceHere(Shape::Freeform);
        recordRect("button:+ Freeform");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A polygon: drag its points on the stage, double-click an edge to add one, right-click a point to delete it");
        ImGui::SameLine();
        if (ImGui::Button("+ 3D Object")) addSurfaceHere(Shape::Object3D);
        recordRect("button:+ 3D Object");

        const bool has = proj_.selected() != nullptr || selectedFolderId_ != 0;
        ImGui::BeginDisabled(!has);
        if (ImGui::Button("Delete")) {
            if (selectedFolderId_) { proj_.removeFolder(selectedFolderId_); selectedFolderId_ = 0; }
            else proj_.removeSurface(proj_.selectedSurfaceId);
        }
        if (ImGui::IsItemHovered() && selectedFolderId_) ImGui::SetTooltip("Deletes the folder; what was in it moves up a level");
        ImGui::SameLine();
        ImGui::BeginDisabled(selectedFolderId_ != 0);
        if (ImGui::Button("Up"))      proj_.moveSurface(proj_.selectedSurfaceId, -1);
        ImGui::SameLine();
        if (ImGui::Button("Down"))    proj_.moveSurface(proj_.selectedSurfaceId, +1);
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("+ Folder")) { selectedFolderId_ = proj_.addFolder("", currentFolder()); if (Folder* f = proj_.findFolder(selectedFolderId_)) f->open = true; }
        recordRect("button:+ Folder");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A folder to organise surfaces. Drag surfaces and folders onto it; hiding it hides all it holds.");

        ImGui::BeginChild("##surfacelist", ImVec2(0, 190), ImGuiChildFlags_Borders);
        listOps_.clear();
        drawFolderContents(0);
        if (proj_.surfaces.empty() && proj_.folders.empty()) ImGui::TextDisabled("No surfaces yet. Add one above.");
        if (const ImGuiPayload* pl = ImGui::GetDragDropPayload(); pl && (pl->IsDataType("MH_SURFACE") || pl->IsDataType("MH_FOLDER"))) {
            ImGui::Selectable("(drop here for the top level)");               // only while dragging
            recordRect("drop:top");
            dropTarget(0);
        }
        ImGui::EndChild();
        applyListOps();                                                       // after the list, so nothing it drew moves under it

        if (Folder* f = proj_.findFolder(selectedFolderId_)) drawFolderInspector(*f);
        else if (Surface* s = proj_.selected()) drawSurfaceInspector(*s);
        else ImGui::TextDisabled("Select a surface to edit it.");
        ImGui::End();
    }

    // ---- folders in the Surfaces list
    struct ListOp { enum Kind { MoveSurface, MoveFolder, DeleteFolder } kind; int id; int target; };

    // The folder new surfaces and folders go into: the selected folder, or the selected surface's folder.
    int currentFolder() const {
        if (selectedFolderId_) return selectedFolderId_;
        for (const Surface& s : proj_.surfaces) if (s.id == proj_.selectedSurfaceId) return s.folderId;
        return 0;
    }
    void addSurfaceHere(Shape shape) {
        const int folder = currentFolder();
        Surface& s = proj_.addSurface(shape, defaultMedia());
        s.folderId = folder;
        selectedFolderId_ = 0; lastSelectedSurface_ = s.id;
    }

    void dropTarget(int folderId) {
        if (!ImGui::BeginDragDropTarget()) return;
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("MH_SURFACE")) listOps_.push_back({ListOp::MoveSurface, *(const int*)pl->Data, folderId});
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("MH_FOLDER"))  listOps_.push_back({ListOp::MoveFolder, *(const int*)pl->Data, folderId});
        ImGui::EndDragDropTarget();
    }

    // "Move to" submenu: the top level and every folder, except the folder being moved and what is inside it.
    void moveToMenu(ListOp::Kind kind, int id, int current, int exclude) {
        if (!ImGui::BeginMenu("Move to")) return;
        if (ImGui::MenuItem("Top level", nullptr, current == 0)) listOps_.push_back({kind, id, 0});
        for (const Folder& f : proj_.folders) {
            if (exclude && proj_.folderWithin(f.id, exclude)) continue;
            const std::string label = std::string((size_t)proj_.folderDepth(f.parentId) * 2, ' ') + f.name + "##mv" + std::to_string(f.id);
            if (ImGui::MenuItem(label.c_str(), nullptr, current == f.id)) listOps_.push_back({kind, id, f.id});
        }
        ImGui::EndMenu();
    }

    void drawFolderContents(int parent) {
        for (size_t i = 0; i < proj_.folders.size(); i++) if (proj_.folders[i].parentId == parent) drawFolderRow(proj_.folders[i].id);
        for (Surface& s : proj_.surfaces) if (s.folderId == parent) drawSurfaceRow(s);
    }

    void drawFolderRow(int folderId) {
        Folder* f = proj_.findFolder(folderId);
        if (!f) return;
        ImGui::PushID(("folder" + std::to_string(f->id)).c_str());
        ImGui::Checkbox("##fvisible", &f->visible);
        recordRect("folderVisible:" + f->name);
        ImGui::SameLine();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (f->id == selectedFolderId_) flags |= ImGuiTreeNodeFlags_Selected;
        ImGui::SetNextItemOpen(f->open, ImGuiCond_Always);
        const bool dim = !proj_.folderShown(f->parentId);
        if (dim) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        const bool open = ImGui::TreeNodeEx("##folder", flags, "[ ] %s", f->name.c_str());
        if (dim) ImGui::PopStyleColor();
        recordRect("folder:" + f->name);
        f->open = open;
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) { selectedFolderId_ = f->id; }
        if (ImGui::BeginDragDropSource()) { const int id = f->id; ImGui::SetDragDropPayload("MH_FOLDER", &id, sizeof(id)); ImGui::Text("Folder %s", f->name.c_str()); ImGui::EndDragDropSource(); }
        dropTarget(f->id);
        if (ImGui::BeginPopupContextItem("##fctx")) {
            if (ImGui::MenuItem("New folder inside")) { const int id = f->id; selectedFolderId_ = proj_.addFolder("", id); f = proj_.findFolder(id); if (f) f->open = true; }
            if (f) moveToMenu(ListOp::MoveFolder, f->id, f->parentId, f->id);
            if (f && ImGui::MenuItem("Delete folder (keeps what is in it)")) listOps_.push_back({ListOp::DeleteFolder, f->id, 0});
            ImGui::EndPopup();
        }
        if (open) {
            if (f) drawFolderContents(f->id);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void drawSurfaceRow(Surface& s) {
        ImGui::PushID(s.id);
        ImGui::Checkbox("##visible", &s.visible);
        ImGui::SameLine();
        char label[96];
        std::snprintf(label, sizeof(label), "[%s]  %s", shapeTag(s.shape), s.name.c_str());
        const bool dim = !proj_.folderShown(s.folderId);                     // hidden by a folder
        if (dim) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Selectable(label, s.id == proj_.selectedSurfaceId && !selectedFolderId_)) { proj_.selectedSurfaceId = s.id; selectedFolderId_ = 0; lastSelectedSurface_ = s.id; }
        if (dim) ImGui::PopStyleColor();
        recordRect("surface:" + s.name);
        if (ImGui::BeginDragDropSource()) { const int id = s.id; ImGui::SetDragDropPayload("MH_SURFACE", &id, sizeof(id)); ImGui::Text("%s", s.name.c_str()); ImGui::EndDragDropSource(); }
        if (ImGui::BeginPopupContextItem("##sctx")) {
            moveToMenu(ListOp::MoveSurface, s.id, s.folderId, 0);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    void applyListOps() {
        for (const ListOp& op : listOps_) {
            if (op.kind == ListOp::MoveSurface) proj_.moveSurfaceToFolder(op.id, op.target);
            else if (op.kind == ListOp::MoveFolder) { if (!proj_.moveFolder(op.id, op.target)) toast("A folder can't go inside itself"); }
            else { proj_.removeFolder(op.id); if (selectedFolderId_ == op.id) selectedFolderId_ = 0; }
        }
        listOps_.clear();
    }

    void drawFolderInspector(Folder& f) {
        ImGui::SeparatorText("Folder");
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s", f.name.c_str());
        if (ImGui::InputText("Name##folder", buf, sizeof(buf)) && buf[0]) f.name = buf;
        ImGui::Checkbox("Visible##folder", &f.visible);
        int count = 0;
        for (const Surface& s : proj_.surfaces) if (s.folderId != 0 && proj_.folderWithin(s.folderId, f.id)) count++;
        ImGui::SameLine();
        ImGui::TextDisabled("%d surface%s inside", count, count == 1 ? "" : "s");
        const Folder* parent = proj_.findFolder(f.parentId);
        if (ImGui::BeginCombo("Inside", parent ? parent->name.c_str() : "Top level")) {
            if (ImGui::Selectable("Top level", f.parentId == 0)) listOps_.push_back({ListOp::MoveFolder, f.id, 0});
            for (const Folder& o : proj_.folders) {
                if (proj_.folderWithin(o.id, f.id)) continue;
                if (ImGui::Selectable((o.name + "##in" + std::to_string(o.id)).c_str(), o.id == f.parentId)) listOps_.push_back({ListOp::MoveFolder, f.id, o.id});
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Delete folder")) listOps_.push_back({ListOp::DeleteFolder, f.id, 0});
        recordRect("button:Delete folder");
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("Deleting a folder keeps its surfaces: they move up a level. New surfaces are added to the selected folder.");
        ImGui::PopStyleColor();
        applyListOps();
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
        if (animated) ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.58f, 0.58f, 0.58f, 1.0f));
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

    // A 3D object: which model, and how it is turned and viewed (all of it animatable except the model).
    void drawObjectInspector(Surface& s) {
        if (ImGui::BeginCombo("Model", meshKindName(s.meshKind))) {
            for (int i = 0; i < kMeshKindCount; i++) {
                if (!ImGui::Selectable(meshKindName((MeshKind)i), (int)s.meshKind == i)) continue;
                if ((MeshKind)i == MeshKind::File) openChooseModel(s.id);
                else s.meshKind = (MeshKind)i;
            }
            ImGui::EndCombo();
        }
        recordRect("combo:Model");
        if (s.meshKind == MeshKind::File) {
            std::string err;
            const MeshData* m = meshFor(s.meshKind, s.meshPath, &err);
            ImGui::TextDisabled("%s", s.meshPath.empty() ? "(no file)" : fileNameOf(s.meshPath).c_str());
            if (!err.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s (showing a cube)", err.c_str());
            else ImGui::TextDisabled("%d triangles", (int)m->triangles());
            if (ImGui::Button("Choose model...")) openChooseModel(s.id);
            ImGui::SameLine();
            if (ImGui::Button("Reload")) forgetMeshFile(s.meshPath);
        }
        for (const char* k : {"rotX", "rotY", "rotZ", "spinX", "spinY", "spinZ", "objScale", "fov"}) paramSlider(s, k);
        if (ImGui::Button("Reset view")) for (const char* k : {"rotX", "rotY", "rotZ", "spinX", "spinY", "spinZ", "objScale", "fov"}) setParamToDefault(s, k);

        ImGui::SeparatorText("Lighting");
        int lm = std::max(0, std::min(kLightModelCount - 1, (int)std::lround(s.light.model)));
        if (ImGui::BeginCombo("Light model", lightModelName((LightModel)lm))) {
            for (int i = 0; i < kLightModelCount; i++) if (ImGui::Selectable(lightModelName((LightModel)i), i == lm)) s.light.model = (float)i;
            ImGui::EndCombo();
        }
        recordRect("combo:Light model");
        if (ImGui::BeginPopupContextItem("##lmctx")) {
            if (ImGui::MenuItem("Animate in the Conductor", nullptr, false, !isAnimated(s.id, "lightModel"))) animateParam(s, "lightModel");
            ImGui::EndPopup();
        }
        paramSlider(s, "shading");
        if (lm == (int)LightModel::Unlit) ImGui::TextDisabled("Unlit: the picture as it is, no lights.");
        else {
            paramSlider(s, "ambient");
            auto group = [&](const char* title, std::initializer_list<const char*> keys) {
                if (!ImGui::TreeNode(title)) return;
                for (const char* k : keys) paramSlider(s, k);
                ImGui::TreePop();
            };
            group("Material", {"kd", "ks", "shininess"});
            group("Sun (directional)", {"dirIntensity", "dirAzimuth", "dirElevation", "dirR", "dirG", "dirB"});
            group("Point light", {"pointIntensity", "pointX", "pointY", "pointZ", "pointRange", "pointR", "pointG", "pointB"});
            group("Spotlight", {"spotIntensity", "spotX", "spotY", "spotZ", "spotAngle", "spotSoftness", "spotR", "spotG", "spotB"});
            if (s.shading <= 0.0f) ImGui::TextDisabled("Turn Lighting up to see the lights.");
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("The model wears the media (the Input view picks which part), and its picture is then mapped by the quad like any surface.");
        ImGui::PopStyleColor();
    }
    void openChooseModel(int surfaceId) {
        fileAction_ = FileAction::ChooseModel; fileTarget_ = surfaceId;
        const Surface* s = proj_.find(surfaceId);
        fileBrowser_.open(FileBrowser::Mode::Open, "Choose a 3D model", {"obj"}, s && !s->meshPath.empty() ? parentOf(s->meshPath) : startFolder());
    }

    void drawSurfaceInspector(Surface& s) {
        ImGui::SeparatorText("Inspector");
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s", s.name.c_str());
        if (ImGui::InputText("Name", buf, sizeof(buf))) s.name = buf;

        if (ImGui::BeginCombo("Shape", shapeName(s.shape))) {
            for (int i = 0; i < kShapeCount; i++) if (ImGui::Selectable(shapeName((Shape)i), (int)s.shape == i)) proj_.setShape(s, (Shape)i);
            ImGui::EndCombo();
        }
        if (s.shape == Shape::Line) {
            paramSlider(s, "lineWidth");
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]); ImGui::TextWrapped("Drag the line's two ends on the stage"); ImGui::PopStyleColor();
        }
        if (s.shape == Shape::Freeform) {
            ImGui::TextDisabled("%d points", (int)s.poly.size());
            ImGui::SameLine();
            const bool resetPts = ImGui::Button("Reset outline");
            recordRect("button:Reset outline");
            if (resetPts) s.poly = defaultPolygon();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::TextWrapped("Drag points on the stage. Double-click an edge to add a point, right-click a point to delete it. The round corners warp the whole outline.");
            ImGui::PopStyleColor();
        }
        if (s.shape == Shape::Object3D) drawObjectInspector(s);

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
            for (const char* k : {"posX", "posY", "rotation", "spin", "scale", "scaleX", "scaleY"}) paramSlider(s, k);
            const bool resetT = ImGui::Button("Reset transform");
            recordRect("button:Reset transform");
            if (resetT) for (const char* k : {"posX", "posY", "rotation", "spin", "scale", "scaleX", "scaleY"}) setParamToDefault(s, k);
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
        drawMasksSection(s);
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
        if (!out_) return;
        const bool outputMenuOpen = ImGui::BeginMenu("Output");
        recordRect("menu:Output");
        if (!outputMenuOpen) return;
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
    enum class FileAction { None, OpenProject, SaveProject, AddMedia, RelinkMedia, RelinkAudio, ChooseModel };

    float drawMenuBar() {
        const float h = ImGui::GetFrameHeight();
        if (ImGui::BeginMainMenuBar()) {
            const float logo = h - 8.0f;
            const ImVec2 at = ImGui::GetCursorScreenPos();
            drawLogo(ImGui::GetWindowDrawList(), ImVec2(at.x + 2.0f, ImGui::GetWindowPos().y + 4.0f), logo, kAccent);
            ImGui::Dummy(ImVec2(logo + 6.0f, 0));
            const bool fileMenuOpen = ImGui::BeginMenu("File");
            recordRect("menu:File");
            if (fileMenuOpen) {
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
            const bool editMenuOpen = ImGui::BeginMenu("Edit");
            recordRect("menu:Edit");
            if (editMenuOpen) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo())) undo();
                recordRect("item:Undo");
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo())) redo();
                recordRect("item:Redo");
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
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", toast_.c_str());
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
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) liveMode_ = !liveMode_;
        if (liveMode_ && !io.KeyCtrl && !io.KeyAlt) {                  // 1..9, 0 (top row or keypad) play; with Shift they store
            for (int d = 0; d <= 9; d++) {
                if (!ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_0 + d), false) && !ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_Keypad0 + d), false)) continue;
                if (io.KeyShift) storePreset(presetSlotForDigit(d)); else recallPreset(presetSlotForDigit(d));
            }
        }
        if (!io.KeyCtrl) return;
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) { if (io.KeyShift) openSaveAs(); else saveProject(); }
        else if (ImGui::IsKeyPressed(ImGuiKey_O, false)) openOpenDialog();
        else if (ImGui::IsKeyPressed(ImGuiKey_N, false)) requestNew();
        else if (ImGui::IsKeyPressed(ImGuiKey_Z, true)) { if (io.KeyShift) redo(); else undo(); }
        else if (ImGui::IsKeyPressed(ImGuiKey_Y, true)) redo();
    }

    // Notices edits, and writes an autosave a minute after the project first has unsaved changes.
    void tickProjectState() {
        const double now = ImGui::GetTime();
        const bool busy = editing();
        if (now - lastDirtyCheck_ > 0.5 || (wasEditing_ && !busy)) {    // twice a second, and as soon as a drag or typing ends
            lastDirtyCheck_ = now;
            dirty_ = projectToJson(proj_, filePath_, true) != savedSnapshot_;
            if (!busy) history_.commit(snapshot());
        }
        wasEditing_ = busy;
        if (!dirty_) { lastAutosave_ = now; return; }
        if (now - lastAutosave_ > 60.0) { lastAutosave_ = now; autosaveNow(); }
    }

    // In the middle of an edit (dragging, typing, a preset fading in)? Then it isn't an undo step yet.
    bool editing() const { const ImGuiIO& io = ImGui::GetIO(); return io.MouseDown[0] || io.MouseDown[1] || io.WantTextInput || ImGui::IsAnyItemActive() || player_.fading(); }
    UndoHistory::State snapshot() const { return {projectToJson(proj_, "", true), projectToJson(proj_, "", false)}; }
    void stepHistory(int dir) {
        if (editing()) return;
        history_.commit(snapshot());                                  // an edit made this very moment counts too
        const UndoHistory::State* s = dir < 0 ? history_.stepBack() : history_.stepForward();
        if (!s) { toast(dir < 0 ? "Nothing to undo" : "Nothing to redo"); return; }
        LoadReport rep;
        if (!restoreProject(proj_, s->full, rep)) { showError("Couldn't undo.\n" + rep.error); return; }
        player_.reset();
        conductor_.clearSelection();
        if (!proj_.media.find(selectedMediaId_)) selectedMediaId_ = 0;
        if (!proj_.findFolder(selectedFolderId_)) selectedFolderId_ = 0;
        const Surface* sel = proj_.selected();
        if (!sel || selectedMask_ >= (int)sel->masks.size()) selectedMask_ = -1;
        drag_ = Drag{};
        proj_.media.applyResizes();
        startAudioReloads(rep.audioToLoad);
        proj_.syncLines();
        proj_.timelineDirty = true;
        history_.restored(projectToJson(proj_, "", true));
        refreshDirtyNow();
        toast(dir < 0 ? "Undone" : "Redone");
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
            case FileAction::ChooseModel:
                if (Surface* s = proj_.find(fileTarget_)) { forgetMeshFile(path); s->meshKind = MeshKind::File; s->meshPath = path; settings_.lastFolder = parentOf(path); saveSettings(settings_); }
                break;
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

    void resetUiAfterLoad() { history_.clear(); player_.reset(); conductor_.resetSelection(); selectedMediaId_ = 0; selectedFolderId_ = 0; selectedMask_ = -1; status_.clear(); }

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
                if (r.ok) { a->buf = r.asset.buf; a->peakMin = std::move(r.asset.peakMin); a->peakMax = std::move(r.asset.peakMax); a->missing = false; if (!proj_.audioStatusIsError) proj_.audioStatus.clear(); }
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
                                   sel ? ImVec4(1, 1, 1, 1) : ImVec4(0, 0, 0, 1)))
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

    void drawAddGeneratorButton() {
        const bool clicked = ImGui::Button("+ Generator");
        recordRect("button:+ Generator");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a test card, grid, text, colour, gradient, strobe or line pattern");
        if (clicked) ImGui::OpenPopup("##addgen");
        if (ImGui::BeginPopup("##addgen")) {
            for (const GenTypeInfo& t : genTypes())
                if (ImGui::MenuItem(t.label)) selectedMediaId_ = proj_.media.addGenerator(t.type);
            ImGui::EndPopup();
        }
    }

    // Every parameter of a generator, straight from its table in GeneratorDefs.h.
    void drawGeneratorInspector(GeneratorSource& g) {
        GenSettings& s = g.settings;
        const GenTypeInfo& info = genTypeInfo(s.type);
        ImGui::SeparatorText(info.label);
        for (const GenParamDef& d : info.params) {
            ImGui::PushID(d.key);
            switch (d.kind) {
                case GenParamKind::Float: { float v = s.num(d.key); if (ImGui::SliderFloat(d.label, &v, d.min, d.max, d.fmt)) s.setNum(d.key, v); } break;
                case GenParamKind::Int:   { int v = (int)std::lround(s.num(d.key)); if (ImGui::SliderInt(d.label, &v, (int)d.min, (int)d.max)) s.setNum(d.key, (float)v); } break;
                case GenParamKind::Bool:  { bool v = s.num(d.key) >= 0.5f; if (ImGui::Checkbox(d.label, &v)) s.setNum(d.key, v ? 1.0f : 0.0f); } break;
                case GenParamKind::Color: { GenColor c = s.color(d.key); if (ImGui::ColorEdit4(d.label, c.data(), ImGuiColorEditFlags_AlphaBar)) s.setColor(d.key, c); } break;
                case GenParamKind::Choice: {
                    int v = (int)std::lround(s.num(d.key));
                    if (ImGui::BeginCombo(d.label, d.choices[std::max(0, std::min((int)d.choices.size() - 1, v))].label)) {
                        for (int i = 0; i < (int)d.choices.size(); i++) if (ImGui::Selectable(d.choices[i].label, i == v)) s.setNum(d.key, (float)i);
                        ImGui::EndCombo();
                    }
                } break;
                case GenParamKind::Text: {
                    char buf[2048];
                    std::snprintf(buf, sizeof(buf), "%s", s.text(d.key).c_str());
                    if (ImGui::InputTextMultiline(d.label, buf, sizeof(buf), ImVec2(0, ImGui::GetTextLineHeight() * 3.5f))) s.setText(d.key, buf);
                } break;
            }
            { const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();         // the control itself, not its label (like the surface sliders)
              const float w = d.kind == GenParamKind::Bool ? b.x - a.x : std::min(b.x - a.x, ImGui::CalcItemWidth());
              rects_[std::string("gen:") + d.key] = Rect{a.x, a.y, w, b.y - a.y, true}; }
            ImGui::PopID();
        }
        int wh[2] = {s.width, s.height};
        if (ImGui::InputInt2("Size (px)", wh, ImGuiInputTextFlags_EnterReturnsTrue)) s.setSize(wh[0], wh[1]);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The generator's own resolution. Type a size and press Enter.");
        const bool match = ImGui::Button("Match output");
        recordRect("button:Match output");
        if (match) s.setSize(proj_.outW, proj_.outH);
        ImGui::SameLine();
        if (ImGui::Button("Reset parameters")) { const int w = s.width, h = s.height; s = GenSettings::defaults(s.type); s.setSize(w, h); }
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
        drawAddGeneratorButton();
        drawMediaThumbs(MediaKind::Generator);
        ImGui::SeparatorText("Videos");
        drawMediaThumbs(MediaKind::Video);

        if (MediaItem* m = proj_.media.find(selectedMediaId_)) {
            ImGui::SeparatorText("Inspector");
            char nameBuf[128];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", m->name.c_str());
            if (ImGui::InputText("Name##media", nameBuf, sizeof(nameBuf)) && nameBuf[0]) m->name = nameBuf;
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
            if (!m->missing) {
                ImGui::SetNextItemWidth(150);
                ImGui::SliderFloat("Speed##media", &m->speed, 0.0f, 4.0f, "%.2fx");
                recordRect("slider:mediaSpeed");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("How fast it plays: 0 = frozen, 1 = normal, 2 = twice as fast");
                ImGui::Checkbox("Playing", &m->playing);
            }
            if (m->video()) {
                ImGui::SameLine();
                ImGui::Checkbox("Loop", &m->loop);
                ImGui::SameLine();
                if (ImGui::Button("Restart")) { std::string p = m->path; m->video()->open(p); }
            }
            if (GeneratorSource* g = m->generator()) {
                ImGui::SameLine();
                if (ImGui::Button("Restart")) g->restart();
                drawGeneratorInspector(*g);
            }
            if (Surface* s = proj_.selected()) {
                const bool use = ImGui::Button("Use on selected surface");
                recordRect("button:Use on selected surface");
                if (use) s->mediaId = m->id;
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
                dl->AddRect(v.min, mx, IM_COL32(51, 51, 51, 255));
                drawSurfaceOverlay(v, *s, s->in, true, false);
                if (!inTransformIsIdentity(*s)) { Vec2 ei[4]; effectiveIn(*s, m->aspect(), ei); drawEffectiveOutline(v, *s, ei); }
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
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "  LIVE%s on %s", out_->isFullscreen ? " (fullscreen)" : " (window)", where);
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
            dl->AddRect(v.min, mx, IM_COL32(51, 51, 51, 255));
            const float stageAspect = (float)proj_.outW / (float)std::max(1, proj_.outH);
            for (Surface& s : proj_.surfaces)
                if (proj_.isShown(s) && s.id != proj_.selectedSurfaceId) { Vec2 eo[4]; effectiveOut(s, stageAspect, eo); drawSurfaceOverlay(v, s, eo, false, true); }
            if (Surface* sel = proj_.selected()) {
                drawSurfaceOverlay(v, *sel, sel->out, true, true);                    // the mapped quad, with its handles
                drawMaskOverlay(v, *sel);
                if (!outTransformIsIdentity(*sel)) { Vec2 eo[4]; effectiveOut(*sel, stageAspect, eo); drawEffectiveOutline(v, *sel, eo); }
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
    int           selectedFolderId_ = 0, lastSelectedSurface_ = 0;
    int           selectedMask_ = -1;                               // index into the selected surface's masks
    bool          liveMode_ = false;                                // number keys play presets
    PresetPlayer  player_;   // a selected folder shows the folder inspector
    std::vector<ListOp> listOps_;
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
    bool          dirty_ = false, exitApproved_ = false, wasEditing_ = false;
    UndoHistory   history_;
    int           pendingUndo_ = 0;                                 // -1 undo, +1 redo, at the end of this frame
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
