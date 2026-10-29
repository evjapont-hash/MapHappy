#pragma once
#include "Params.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// The Conductor: transport, ruler, one lane per animated parameter, clips with keyframes.
//
//   Add a track      "+ Track" button
//   Create a clip    right-click an empty spot in a lane
//   Add a keyframe   double-click inside a clip
//   Move a keyframe  drag it (up/down changes the value, left/right the time)
//   Curve type       right-click a keyframe: Linear / Smooth / Hold
//   Move a clip      drag its yellow header          Resize: drag its right edge (longer = loops)
//   Duplicate/ghost  right-click a clip              Delete: Delete key or right-click menu
//   Playhead         click or drag the ruler         Play/pause: Space
//   Zoom / scroll    Ctrl + wheel / Shift + wheel
class ConductorPanel {
public:
    explicit ConductorPanel(Project& p) : proj_(p) {}

    // Where things were drawn last frame (used by the automated UI tests).
    struct Geom { float canvasX0 = 0, canvasW = 0, rulerTop = 0, laneTop0 = 0; float pps = 60.0f; double scrollT = 0; bool valid = false; };
    const Geom& geom() const { return geom_; }
    static constexpr float kLaneH = 64.0f, kHeaderH = 16.0f, kRulerH = 38.0f, kNameW = 210.0f;

    void draw(ImVec2 pos, ImVec2 size) {
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("Conductor", nullptr, kFlags);
        Transport& tr = proj_.transport;
        ImGuiIO& io = ImGui::GetIO();

        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space, false)) tr.playing = !tr.playing;

        drawTransportBar();

        const ImVec2 origin = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
        if (avail.y < kRulerH + 30.0f || avail.x < kNameW + 60.0f) { ImGui::End(); return; }
        canvasX0_ = origin.x + kNameW;
        canvasW_  = avail.x - kNameW;
        geom_.canvasX0 = canvasX0_; geom_.canvasW = canvasW_; geom_.rulerTop = origin.y;
        geom_.pps = pps_; geom_.scrollT = scrollT_; geom_.valid = true;

        drawRuler(origin);

        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + kRulerH));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##lanes", ImVec2(avail.x, avail.y - kRulerH), ImGuiChildFlags_None);
        ImGui::PopStyleVar();
        drawLanes();
        ImGui::EndChild();

        // keep the playhead in view while playing
        if (tr.playing) {
            double span = canvasW_ / pps_;
            if (tr.time > scrollT_ + span - 0.5 || tr.time < scrollT_) scrollT_ = std::max(0.0, tr.time - span * 0.1);
        }
        ImGui::End();
    }

private:
    static constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

    static constexpr ImU32 kAccent = IM_COL32(38, 198, 208, 255);
    static constexpr ImU32 kYellow = IM_COL32(236, 224, 80, 255);

    float tToX(double t) const { return canvasX0_ + (float)((t - scrollT_) * pps_); }
    double xToT(float x) const { return (x - canvasX0_) / pps_ + scrollT_; }
    void dirty() { proj_.timelineDirty = true; }

    // ---- parameter helpers ------------------------------------------------------------
    ParamInfo infoFor(const Track& t) const {
        ParamInfo pi;
        if (!paramInfo(proj_, t.path, pi)) { pi = ParamInfo{}; pi.path = t.path; pi.label = t.path; }
        return pi;
    }
    float valueToY(float v, float laneTop, const ParamInfo& pi) const {
        float top = laneTop + 2 + kHeaderH + 8, bot = laneTop + kLaneH - 8;
        float range = pi.max - pi.min; if (range < 1e-9f) range = 1.0f;
        float n = std::max(0.0f, std::min(1.0f, (v - pi.min) / range));
        return bot - n * (bot - top);
    }
    float yToValue(float y, float laneTop, const ParamInfo& pi) const {
        float top = laneTop + 2 + kHeaderH + 8, bot = laneTop + kLaneH - 8;
        float n = (bot - y) / (bot - top);
        n = std::max(0.0f, std::min(1.0f, n));
        return pi.min + n * (pi.max - pi.min);
    }
    std::string valueText(const ParamInfo& pi, float v) const {
        char b[32];
        if (pi.isBool) return v >= 0.5f ? "On" : "Off";
        if (pi.percent) std::snprintf(b, sizeof(b), "%.0f%%", v * 100.0f);
        else            std::snprintf(b, sizeof(b), "%.2f", v);
        return b;
    }

    // ---- transport bar ------------------------------------------------------------------
    void drawTransportBar() {
        Transport& tr = proj_.transport;
        if (ImGui::Button(tr.playing ? "Pause" : "Play", ImVec2(60, 0))) tr.playing = !tr.playing;
        ImGui::SameLine();
        if (ImGui::Button("Stop", ImVec2(52, 0))) { tr.playing = false; tr.time = 0.0; scrollT_ = 0.0; dirty(); }
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &tr.loop);
        ImGui::SameLine(0, 20);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", formatTimecode(tr.time).c_str());
        ImGui::SameLine(0, 20);
        if (ImGui::Button("+ Track")) ImGui::OpenPopup("##addtrack");
        if (ImGui::BeginPopup("##addtrack")) {
            std::string lastGroup; bool any = false;
            for (const ParamInfo& pi : listParams(proj_)) {
                if (proj_.timeline.findTrackByPath(pi.path)) continue;        // one track per parameter
                if (pi.group != lastGroup) { ImGui::SeparatorText(pi.group.c_str()); lastGroup = pi.group; }
                if (ImGui::Selectable(pi.label.c_str())) proj_.timeline.addTrack(pi.path);
                any = true;
            }
            if (!any) ImGui::TextDisabled("Every parameter already has a track");
            ImGui::EndPopup();
        }
        ImGui::SameLine(0, 20);
        ImGui::SetNextItemWidth(110);
        ImGui::SliderFloat("##zoom", &pps_, 10.0f, 600.0f, "Zoom %.0f px/s", ImGuiSliderFlags_Logarithmic);
    }

    // ---- ruler ------------------------------------------------------------------------------
    void drawRuler(ImVec2 origin) {
        Transport& tr = proj_.transport;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(origin, ImVec2(origin.x + kNameW + canvasW_, origin.y + kRulerH), IM_COL32(30, 30, 33, 255));
        dl->AddText(ImVec2(origin.x + 8, origin.y + 11), IM_COL32(170, 170, 175, 255), "Tracks");

        dl->PushClipRect(ImVec2(canvasX0_, origin.y), ImVec2(canvasX0_ + canvasW_, origin.y + kRulerH), true);
        static const double steps[] = {0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300};
        double step = steps[11];
        for (double s : steps) if (s * pps_ >= 70.0) { step = s; break; }
        double span = canvasW_ / pps_;
        for (double t = std::floor(scrollT_ / step) * step; t <= scrollT_ + span + step; t += step) {
            float x = tToX(t);
            dl->AddLine(ImVec2(x, origin.y + 12), ImVec2(x, origin.y + kRulerH), IM_COL32(110, 110, 115, 255));
            char buf[32];
            if (step >= 1.0) { int s = (int)std::lround(t); if (s >= 60) std::snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60); else std::snprintf(buf, sizeof(buf), "%02ds", s); }
            else std::snprintf(buf, sizeof(buf), "%.2fs", t);
            dl->AddText(ImVec2(x + 4, origin.y + 1), IM_COL32(200, 200, 205, 255), buf);
            std::snprintf(buf, sizeof(buf), "%d", (int)std::lround(t * kTimelineFps));
            dl->AddText(ImVec2(x + 4, origin.y + 19), IM_COL32(120, 120, 126, 255), buf);
            for (int k = 1; k < 4; k++) {
                float xm = x + (float)(k * step * pps_ / 4.0);
                dl->AddLine(ImVec2(xm, origin.y + 28), ImVec2(xm, origin.y + kRulerH), IM_COL32(80, 80, 85, 255));
            }
        }
        dl->PopClipRect();

        ImGui::SetCursorScreenPos(ImVec2(canvasX0_, origin.y));
        ImGui::InvisibleButton("##ruler", ImVec2(canvasW_, kRulerH));
        if (ImGui::IsItemActive() && ImGui::IsMouseDown(0)) {
            tr.time = snapToFrame(std::max(0.0, std::min(proj_.timeline.length, xToT(ImGui::GetIO().MousePos.x))));
            dirty();
        }
        float px = tToX(tr.time);
        if (px >= canvasX0_ && px <= canvasX0_ + canvasW_)
            dl->AddTriangleFilled(ImVec2(px - 6, origin.y + kRulerH - 10), ImVec2(px + 6, origin.y + kRulerH - 10), ImVec2(px, origin.y + kRulerH - 1), kAccent);
    }

    // ---- hit testing -----------------------------------------------------------------------
    struct Hit {
        enum Kind { None, Lane, ClipHeader, ClipEdge, ClipBody, Key } kind = None;
        int track = 0, clip = 0, key = -1;
        double time = 0.0;
    };

    ImVec2 keyPos(const Clip& c, const Keyframe& k, float laneTop, const ParamInfo& pi) const {
        return ImVec2(tToX(c.start + k.t), valueToY(k.v, laneTop, pi));
    }

    Hit hitTest(ImVec2 m) {
        Hit h;
        Timeline& tl = proj_.timeline;
        int ti = (int)std::floor((m.y - laneTop0_) / kLaneH);
        if (ti < 0 || ti >= (int)tl.tracks.size()) return h;
        Track& tk = tl.tracks[ti];
        ParamInfo pi = infoFor(tk);
        float laneTop = laneTop0_ + ti * kLaneH;
        h.kind = Hit::Lane; h.track = tk.id; h.time = xToT(m.x);
        for (Clip& c : tk.clips) {
            float x0 = tToX(c.start), x1 = tToX(c.end());
            if (m.x < x0 - 10 || m.x > x1 + 10) continue;
            for (size_t k = 0; k < c.data->keys.size(); k++) {
                ImVec2 kp = keyPos(c, c.data->keys[k], laneTop, pi);
                if (std::hypot(kp.x - m.x, kp.y - m.y) <= 9.0f) { h.kind = Hit::Key; h.clip = c.id; h.key = (int)k; return h; }
            }
            if (m.x < x0 || m.x > x1 || m.y < laneTop + 2 || m.y > laneTop + kLaneH - 2) continue;
            h.clip = c.id;
            if (m.x > x1 - 7)                          h.kind = Hit::ClipEdge;
            else if (m.y <= laneTop + 2 + kHeaderH)    h.kind = Hit::ClipHeader;
            else                                       h.kind = Hit::ClipBody;
            return h;
        }
        return h;
    }

    // ---- drawing the lanes ---------------------------------------------------------------
    void drawClip(ImDrawList* dl, const Track& tk, const Clip& c, float laneTop, const ParamInfo& pi) {
        const float x0 = tToX(c.start), x1 = tToX(c.end());
        if (x1 < canvasX0_ || x0 > canvasX0_ + canvasW_) return;
        const bool selected = (c.id == sel_.clip && tk.id == sel_.track);
        const float top = laneTop + 2, hb = top + kHeaderH, bot = laneTop + kLaneH - 2;

        dl->AddRectFilled(ImVec2(x0, hb), ImVec2(x1, bot), IM_COL32(30, 52, 56, 255));
        // header: solid yellow = original/duplicate, hatched = ghost
        dl->AddRectFilled(ImVec2(x0, top), ImVec2(x1, hb), kYellow);
        if (c.ghost) {
            dl->PushClipRect(ImVec2(x0, top), ImVec2(x1, hb), true);
            for (float sx = x0 - kHeaderH; sx < x1; sx += 9.0f)
                dl->AddLine(ImVec2(sx, hb), ImVec2(sx + kHeaderH, top), IM_COL32(150, 140, 30, 255), 3.0f);
            dl->PopClipRect();
        }
        dl->PushClipRect(ImVec2(x0, top), ImVec2(std::max(x0, x1 - 4), hb), true);
        dl->AddText(ImVec2(x0 + 4, top + 1), IM_COL32(20, 20, 20, 255), c.name.c_str());
        dl->PopClipRect();

        // loop boundaries
        double len = c.data->length();
        if (len > 1e-6 && c.duration > len + 1e-6)
            for (double t = len; t < c.duration - 1e-6; t += len) {
                float lx = tToX(c.start + t);
                dl->AddLine(ImVec2(lx, hb), ImVec2(lx, bot), IM_COL32(236, 224, 80, 90), 1.0f);
            }

        // the curve
        std::vector<ImVec2> pts;
        float xa = std::max(x0, canvasX0_), xb = std::min(x1, canvasX0_ + canvasW_);
        for (float x = xa; x < xb; x += 2.0f) pts.push_back(ImVec2(x, valueToY(c.valueAtLocal(xToT(x) - c.start), laneTop, pi)));
        pts.push_back(ImVec2(xb, valueToY(c.valueAtLocal(xToT(xb) - c.start), laneTop, pi)));
        if (pts.size() > 1) dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(210, 215, 220, 255), ImDrawFlags_None, 1.5f);

        // keyframes (first cycle only; later cycles are repeats)
        for (size_t k = 0; k < c.data->keys.size(); k++) {
            const Keyframe& kf = c.data->keys[k];
            ImVec2 p = keyPos(c, kf, laneTop, pi);
            bool sel = selected && sel_.key == (int)k;
            ImU32 fill = sel ? kAccent : IM_COL32(235, 235, 235, 255);
            switch (kf.interp) {
                case Interp::Linear:      // diamond
                    dl->AddQuadFilled(ImVec2(p.x, p.y - 6), ImVec2(p.x + 6, p.y), ImVec2(p.x, p.y + 6), ImVec2(p.x - 6, p.y), fill); break;
                case Interp::Hold:        // square
                    dl->AddRectFilled(ImVec2(p.x - 5, p.y - 5), ImVec2(p.x + 5, p.y + 5), fill); break;
                default:                  // circle
                    dl->AddCircleFilled(p, 5.5f, fill);
            }
            dl->AddRect(ImVec2(p.x - 0.5f, p.y - 0.5f), ImVec2(p.x + 0.5f, p.y + 0.5f), IM_COL32(0, 0, 0, 0));
        }
        if (selected) dl->AddRect(ImVec2(x0, top), ImVec2(x1, bot), kAccent, 0.0f, 0, 2.0f);
    }

    void drawLanes() {
        Timeline& tl = proj_.timeline;
        Transport& tr = proj_.transport;
        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        laneTop0_ = cp.y;
        geom_.laneTop0 = cp.y;
        bool openCtx = false;

        // --- names column, lane backgrounds, hold lines, clips
        for (int i = 0; i < (int)tl.tracks.size(); i++) {
            Track& tk = tl.tracks[i];
            const ParamInfo pi = infoFor(tk);
            const float top = cp.y + i * kLaneH, bot = top + kLaneH;
            const bool trackSel = tk.id == sel_.track;

            dl->AddRectFilled(ImVec2(cp.x, top), ImVec2(canvasX0_, bot - 1), trackSel ? IM_COL32(24, 66, 72, 255) : IM_COL32(36, 36, 40, 255));
            dl->AddRectFilled(ImVec2(canvasX0_, top), ImVec2(canvasX0_ + canvasW_, bot - 1), (i % 2) ? IM_COL32(27, 27, 30, 255) : IM_COL32(31, 31, 34, 255));

            ImGui::PushID(tk.id);
            std::string label = pi.group.empty() ? pi.label : (pi.group + " / " + pi.label);
            ImGui::PushClipRect(ImVec2(cp.x, top), ImVec2(canvasX0_ - 40, bot), true);
            ImGui::SetCursorScreenPos(ImVec2(cp.x + 8, top + 8));
            ImGui::TextUnformatted(label.c_str());
            float cur;
            if (getParam(proj_, tk.path, cur)) {
                ImGui::SetCursorScreenPos(ImVec2(cp.x + 8, top + 30));
                ImGui::TextDisabled("%s", valueText(pi, cur).c_str());
            }
            ImGui::PopClipRect();
            ImGui::SetCursorScreenPos(ImVec2(canvasX0_ - 34, top + 6));
            const bool wasMuted = tk.muted;                  // remember: clicking flips it before we pop the color
            if (wasMuted) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.30f, 0.20f, 1.0f));
            if (ImGui::SmallButton("M")) { tk.muted = !tk.muted; dirty(); }
            if (wasMuted) ImGui::PopStyleColor();
            ImGui::PopID();

            if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImVec2(cp.x, top), ImVec2(canvasX0_ - 40, bot))) {
                if (ImGui::IsMouseClicked(0)) { sel_ = Sel{}; sel_.track = tk.id; }
                if (ImGui::IsMouseClicked(1)) { ctx_ = Ctx{}; ctx_.kind = Ctx::TrackName; ctx_.track = tk.id; openCtx = true; }
            }

            dl->PushClipRect(ImVec2(canvasX0_, cp.y), ImVec2(canvasX0_ + canvasW_, wp.y + ws.y), true);
            // value held before the first clip and between/after clips
            if (!tk.clips.empty()) {
                ImU32 hold = IM_COL32(150, 155, 160, 140);
                const Clip& first = tk.clips.front();
                float y0 = valueToY(first.valueAtLocal(0.0), top, pi);
                dl->AddLine(ImVec2(canvasX0_, y0), ImVec2(tToX(first.start), y0), hold, 1.0f);
                for (size_t k = 0; k < tk.clips.size(); k++) {
                    const Clip& c = tk.clips[k];
                    float y = valueToY(c.valueAtLocal(c.duration), top, pi);
                    float xe = (k + 1 < tk.clips.size()) ? tToX(tk.clips[k + 1].start) : canvasX0_ + canvasW_;
                    dl->AddLine(ImVec2(tToX(c.end()), y), ImVec2(xe, y), hold, 1.0f);
                }
            }
            for (const Clip& c : tk.clips) drawClip(dl, tk, c, top, pi);
            dl->PopClipRect();
        }
        if (tl.tracks.empty()) {
            ImGui::SetCursorScreenPos(ImVec2(canvasX0_ + 16, cp.y + 16));
            ImGui::TextDisabled("No tracks yet. Click \"+ Track\" to animate a parameter.");
        }

        // --- the interactive canvas on top of the lanes
        ImGui::SetCursorScreenPos(ImVec2(canvasX0_, cp.y));
        ImGui::InvisibleButton("##canvas", ImVec2(canvasW_, std::max(tl.tracks.size() * kLaneH, ws.y)));
        const bool hovered = ImGui::IsItemHovered();

        if (hovered) {
            if (io.KeyCtrl && io.MouseWheel != 0.0f) {                       // zoom around the mouse
                double tm = xToT(io.MousePos.x);
                pps_ = std::max(10.0f, std::min(600.0f, pps_ * (1.0f + 0.15f * io.MouseWheel)));
                scrollT_ = std::max(0.0, tm - (io.MousePos.x - canvasX0_) / pps_);
            } else if (io.KeyShift && io.MouseWheel != 0.0f) {                // scroll sideways
                scrollT_ = std::max(0.0, scrollT_ - io.MouseWheel * 80.0 / pps_);
            }
        }

        Hit h = hovered ? hitTest(io.MousePos) : Hit{};
        if (hovered && (h.kind == Hit::ClipEdge || drag_.kind == DragKind::Resize)) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        if (hovered && ImGui::IsMouseDoubleClicked(0)) onDoubleClick(h, io.MousePos);
        else if (hovered && ImGui::IsMouseClicked(0))  onClick(h, io.MousePos);
        if (hovered && ImGui::IsMouseClicked(1)) {
            sel_.track = h.track;
            ctx_ = Ctx{}; ctx_.track = h.track; ctx_.clip = h.clip; ctx_.key = h.key; ctx_.time = h.time;
            if      (h.kind == Hit::Key)  { ctx_.kind = Ctx::KeyCtx; sel_.clip = h.clip; sel_.key = h.key; }
            else if (h.kind == Hit::ClipHeader || h.kind == Hit::ClipBody || h.kind == Hit::ClipEdge) { ctx_.kind = Ctx::ClipCtx; sel_.clip = h.clip; sel_.key = -1; }
            else if (h.kind == Hit::Lane) ctx_.kind = Ctx::LaneCtx;
            if (ctx_.kind != Ctx::None) openCtx = true;
        }
        updateDrag(io.MousePos);

        if (!io.WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) deleteSelection();

        // --- playhead
        dl->PushClipRect(ImVec2(canvasX0_, wp.y), ImVec2(canvasX0_ + canvasW_, wp.y + ws.y), true);
        float px = tToX(tr.time);
        dl->AddLine(ImVec2(px, wp.y), ImVec2(px, wp.y + ws.y), kAccent, 1.5f);
        dl->PopClipRect();

        if (openCtx) ImGui::OpenPopup("##ctx");
        drawContextMenu();
    }

    // ---- mouse interaction -----------------------------------------------------------------
    struct Sel { int track = 0, clip = 0, key = -1; };
    enum class DragKind { None, Key, Move, Resize };
    struct Drag { DragKind kind = DragKind::None; int track = 0, clip = 0, key = -1; double grab = 0.0; };
    struct Ctx { enum Kind { None, LaneCtx, ClipCtx, KeyCtx, TrackName } kind = None; int track = 0, clip = 0, key = -1; double time = 0.0; };

    // Snap a time to the frame grid, or to another clip's edge / the playhead if one is close.
    double snapTime(double t, int exceptClip) {
        double best = snapToFrame(t), bestD = 8.0 / pps_;
        auto consider = [&](double c) { if (std::fabs(c - t) < bestD) { bestD = std::fabs(c - t); best = c; } };
        consider(proj_.transport.time);
        for (const Track& tk : proj_.timeline.tracks)
            for (const Clip& c : tk.clips) if (c.id != exceptClip) { consider(c.start); consider(c.end()); }
        return best;
    }

    void onClick(const Hit& h, ImVec2 m) {
        sel_ = Sel{}; drag_ = Drag{};
        sel_.track = h.track;
        switch (h.kind) {
            case Hit::Key:
                sel_.clip = h.clip; sel_.key = h.key;
                drag_.kind = DragKind::Key; drag_.track = h.track; drag_.clip = h.clip; drag_.key = h.key;
                break;
            case Hit::ClipEdge:
                sel_.clip = h.clip;
                drag_.kind = DragKind::Resize; drag_.track = h.track; drag_.clip = h.clip;
                break;
            case Hit::ClipHeader: {
                sel_.clip = h.clip;
                Track* tk = proj_.timeline.findTrack(h.track);
                Clip* c = tk ? tk->findClip(h.clip) : nullptr;
                if (c) { drag_.kind = DragKind::Move; drag_.track = h.track; drag_.clip = h.clip; drag_.grab = xToT(m.x) - c->start; }
                break;
            }
            case Hit::ClipBody: sel_.clip = h.clip; break;
            default: break;
        }
    }

    void onDoubleClick(const Hit& h, ImVec2 m) {
        if (h.kind != Hit::ClipBody && h.kind != Hit::ClipHeader) return;
        Track* tk = proj_.timeline.findTrack(h.track);
        Clip* c = tk ? tk->findClip(h.clip) : nullptr;
        if (!c) return;
        const ParamInfo pi = infoFor(*tk);
        int ti = (int)(tk - &proj_.timeline.tracks[0]);
        double local = h.time - c->start, len = c->data->length();
        if (len > 1e-9 && local > len) local = std::fmod(local, len);       // double-clicked a repeat: edit the original cycle
        local = std::max(0.0, std::min(local, c->duration));
        local = snapToFrame(local);
        auto& keys = c->data->keys;
        for (const Keyframe& k : keys) if (std::fabs(k.t - local) < 1e-4) return;
        Keyframe nk; nk.t = local; nk.v = yToValue(m.y, laneTop0_ + ti * kLaneH, pi);
        auto it = std::upper_bound(keys.begin(), keys.end(), local, [](double t, const Keyframe& k) { return t < k.t; });
        int idx = (int)(it - keys.begin());
        keys.insert(it, nk);
        sel_.track = tk->id; sel_.clip = c->id; sel_.key = idx;
        dirty();
    }

    void updateDrag(ImVec2 m) {
        if (drag_.kind == DragKind::None) return;
        if (!ImGui::IsMouseDown(0)) { drag_ = Drag{}; return; }
        Timeline& tl = proj_.timeline;
        Track* tk = tl.findTrack(drag_.track);
        Clip* c = tk ? tk->findClip(drag_.clip) : nullptr;
        if (!c) { drag_ = Drag{}; return; }
        const double mt = xToT(m.x);

        if (drag_.kind == DragKind::Key) {
            auto& keys = c->data->keys;
            int k = drag_.key;
            if (k < 0 || k >= (int)keys.size()) { drag_ = Drag{}; return; }
            int ti = (int)(tk - &tl.tracks[0]);
            const ParamInfo pi = infoFor(*tk);
            double lo = (k > 0) ? keys[k - 1].t : 0.0;
            double hi = (k + 1 < (int)keys.size()) ? keys[k + 1].t : c->duration;
            keys[k].t = std::max(lo, std::min(hi, snapToFrame(mt - c->start)));
            keys[k].v = yToValue(m.y, laneTop0_ + ti * kLaneH, pi);
            dirty();
        } else if (drag_.kind == DragKind::Move) {
            double lo = tk->minStartFor(c->id), hi = tk->maxEndFor(c->id) - c->duration;
            double s = snapTime(mt - drag_.grab, c->id);
            double e = snapTime(mt - drag_.grab + c->duration, c->id) - c->duration;      // also try to snap the clip's end
            if (std::fabs(e - (mt - drag_.grab)) < std::fabs(s - (mt - drag_.grab))) s = e;
            c->start = std::max(lo, std::min(std::max(lo, hi), s));
            tl.extendTo(c->end());
            dirty();
        } else if (drag_.kind == DragKind::Resize) {
            double minDur = 1.0 / kTimelineFps, maxDur = tk->maxEndFor(c->id) - c->start;
            double d = snapTime(mt, c->id) - c->start;
            c->duration = std::max(minDur, std::min(maxDur, d));
            tl.extendTo(c->end());
            dirty();
        }
    }

    void deleteSelection() {
        Timeline& tl = proj_.timeline;
        Track* tk = tl.findTrack(sel_.track);
        Clip* c = tk ? tk->findClip(sel_.clip) : nullptr;
        if (!c) return;
        if (sel_.key >= 0) {
            auto& keys = c->data->keys;
            if (sel_.key < (int)keys.size() && keys.size() > 2) { keys.erase(keys.begin() + sel_.key); sel_.key = -1; dirty(); }
        } else {
            tl.removeClip(sel_.track, sel_.clip); sel_ = Sel{}; dirty();
        }
    }

    // ---- right-click menu --------------------------------------------------------------------
    void drawContextMenu() {
        if (!ImGui::BeginPopup("##ctx")) return;
        Timeline& tl = proj_.timeline;
        Track* tk = tl.findTrack(ctx_.track);
        if (!tk) { ImGui::EndPopup(); return; }
        Clip* c = tk->findClip(ctx_.clip);

        if (ctx_.kind == Ctx::LaneCtx) {
            if (ImGui::MenuItem("Create clip here")) {
                double start = snapToFrame(std::max(0.0, ctx_.time));
                double room = 1e18;
                for (const Clip& o : tk->clips) if (o.start > start) { room = std::min(room, o.start - start); }
                float v = infoFor(*tk).def; getParam(proj_, tk->path, v);
                int id = tl.addClip(tk->id, start, std::min(2.0, room), v);
                if (id) { sel_ = Sel{}; sel_.track = tk->id; sel_.clip = id; dirty(); }
            }
        } else if (ctx_.kind == Ctx::ClipCtx && c) {
            if (ImGui::MenuItem("Duplicate")) {
                int id = tl.duplicateClip(tk->id, c->id, false);
                if (id) { sel_.clip = id; sel_.key = -1; dirty(); }
            }
            if (ImGui::MenuItem("Ghost copy")) {
                int id = tl.duplicateClip(tk->id, c->id, true);
                if (id) { sel_.clip = id; sel_.key = -1; dirty(); }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete clip")) { tl.removeClip(tk->id, c->id); sel_ = Sel{}; dirty(); }
        } else if (ctx_.kind == Ctx::KeyCtx && c && ctx_.key >= 0 && ctx_.key < (int)c->data->keys.size()) {
            Keyframe& k = c->data->keys[ctx_.key];
            if (ImGui::MenuItem("Linear", nullptr, k.interp == Interp::Linear)) { k.interp = Interp::Linear; dirty(); }
            if (ImGui::MenuItem("Smooth", nullptr, k.interp == Interp::Smooth)) { k.interp = Interp::Smooth; dirty(); }
            if (ImGui::MenuItem("Hold",   nullptr, k.interp == Interp::Hold))   { k.interp = Interp::Hold;   dirty(); }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete keyframe", nullptr, false, c->data->keys.size() > 2)) {
                c->data->keys.erase(c->data->keys.begin() + ctx_.key); sel_.key = -1; dirty();
            }
        } else if (ctx_.kind == Ctx::TrackName) {
            if (ImGui::MenuItem("Delete track")) { tl.removeTrack(tk->id); sel_ = Sel{}; dirty(); }
        }
        ImGui::EndPopup();
    }

    Project& proj_;
    float    pps_ = 60.0f;                 // pixels per second
    double   scrollT_ = 0.0;               // time at the left edge of the canvas
    float    canvasX0_ = 0, canvasW_ = 100, laneTop0_ = 0;
    Sel      sel_;
    Drag     drag_;
    Ctx      ctx_;
    Geom     geom_;
};
