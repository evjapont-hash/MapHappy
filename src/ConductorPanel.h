#pragma once
#include "Params.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

// The Conductor: transport, ruler, one lane per track, clips with keyframes, audio with waveform.
//
//   Add a track      "+ Track"  (parameters, or an audio track)       Audio: drag a WAV/MP3/FLAC onto the window
//   Create a clip    right-click an empty spot in a parameter lane
//   Add a keyframe   double-click inside a clip     Move it: drag     Curve type: right-click it
//   Move a clip      drag its header                Resize: drag its right edge (longer = loops)
//   Duplicate/ghost  right-click a clip             Split audio: right-click > Split at playhead
//   Markers          "Marker" button or M while playing (tap along with the music); right-click a flag to remove
//   Snapping         "Snap" toggles snapping to beats, markers, clip edges and the playhead
//   Playhead         click or drag the ruler        Play/pause: Space        Zoom / scroll: Ctrl / Shift + wheel
class ConductorPanel {
public:
    explicit ConductorPanel(Project& p) : proj_(p) {}

    struct Geom { float canvasX0 = 0, canvasW = 0, rulerTop = 0, laneTop0 = 0; float pps = 60.0f; double scrollT = 0; bool valid = false; };
    const Geom& geom() const { return geom_; }
    static constexpr float kLaneH = 64.0f, kAudioLaneH = 104.0f, kHeaderH = 16.0f, kRulerH = 38.0f, kNameW = 210.0f, kInspW = 250.0f;

    void selectAudioClip(int trackId, int clipId) { sel_ = Sel{}; sel_.track = trackId; sel_.audioClip = clipId; }
    void resetSelection() { sel_ = Sel{}; drag_ = Drag{}; scrollT_ = 0.0; }
    std::function<void(int)> onRelinkAudio;      // asked to find a missing song again (the editor shows the file dialog)

    void draw(ImVec2 pos, ImVec2 size) {
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("Conductor", nullptr, kFlags);
        Transport& tr = proj_.transport;
        ImGuiIO& io = ImGui::GetIO();

        const bool keysFree = !io.WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        if (keysFree && ImGui::IsKeyPressed(ImGuiKey_Space, false)) tr.playing = !tr.playing;
        if (keysFree && ImGui::IsKeyPressed(ImGuiKey_M, false)) proj_.timeline.addMarker(tr.time);

        drawTransportBar();

        const ImVec2 origin = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
        const float inspW = inspectorOpen_ ? kInspW : 0.0f;
        if (avail.y < kRulerH + 30.0f || avail.x < kNameW + inspW + 60.0f) { ImGui::End(); return; }
        canvasX0_ = origin.x + kNameW;
        canvasW_  = avail.x - kNameW - inspW;
        geom_.canvasX0 = canvasX0_; geom_.canvasW = canvasW_; geom_.rulerTop = origin.y;
        geom_.pps = pps_; geom_.scrollT = scrollT_; geom_.valid = true;

        drawRuler(origin);

        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + kRulerH));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##lanes", ImVec2(avail.x - inspW, avail.y - kRulerH), ImGuiChildFlags_None);
        ImGui::PopStyleVar();
        drawLanes();
        ImGui::EndChild();

        if (inspectorOpen_) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + avail.x - inspW, origin.y));
            drawInspector(avail.y);
        }

        drawRulerContextMenu();

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
    static constexpr ImU32 kSalmon = IM_COL32(255, 176, 154, 255);
    static constexpr ImU32 kBeat   = IM_COL32(90, 225, 110, 255);
    static constexpr ImU32 kMarker = IM_COL32(255, 170, 40, 255);

    float tToX(double t) const { return canvasX0_ + (float)((t - scrollT_) * pps_); }
    double xToT(float x) const { return (x - canvasX0_) / pps_ + scrollT_; }
    void dirty() { proj_.timelineDirty = true; }

    // ---- parameter helpers ------------------------------------------------------------
    ParamInfo infoFor(const Track& t) const {
        ParamInfo pi;
        if (!paramInfo(proj_, t.path, pi)) { pi = ParamInfo{}; pi.path = t.path; pi.label = t.name.empty() ? t.path : t.name; }
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
        float n = std::max(0.0f, std::min(1.0f, (bot - y) / (bot - top)));
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
        if (ImGui::Button("Stop", ImVec2(52, 0))) { tr.playing = false; tr.time = 0.0; tr.seeked = true; scrollT_ = 0.0; dirty(); }
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &tr.loop);
        ImGui::SameLine(0, 20);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", formatTimecode(tr.time).c_str());
        ImGui::SameLine(0, 20);
        if (ImGui::Button("+ Track")) ImGui::OpenPopup("##addtrack");
        if (ImGui::BeginPopup("##addtrack")) {
            std::string lastGroup;
            for (const ParamInfo& pi : listParams(proj_)) {
                if (proj_.timeline.findTrackByPath(pi.path)) continue;        // one track per parameter
                if (pi.group != lastGroup) { ImGui::SeparatorText(pi.group.c_str()); lastGroup = pi.group; }
                if (ImGui::Selectable(pi.label.c_str())) proj_.timeline.addTrack(pi.path);
            }
            ImGui::SeparatorText("Audio");
            if (ImGui::Selectable("Audio track")) proj_.timeline.addAudioTrack();
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Snap", &snap_);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap to beats, markers, clip edges and the playhead");
        ImGui::SameLine();
        if (ImGui::Button("Marker")) proj_.timeline.addMarker(tr.time);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drop a marker at the playhead (M). Tap it along with the music while playing.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::SliderFloat("##zoom", &pps_, 10.0f, 600.0f, "Zoom %.0f px/s", ImGuiSliderFlags_Logarithmic);
        ImGui::SameLine();
        int ms = (int)std::lround(tr.avOffset * 1000.0);
        ImGui::SetNextItemWidth(90);
        if (ImGui::DragInt("##av", &ms, 1.0f, -300, 300, "A/V %d ms")) { tr.avOffset = ms / 1000.0; tr.seeked = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sound/picture sync. Raise it if the picture is ahead of the sound,\nlower it if the picture lags.");
        ImGui::SameLine();
        ImGui::Checkbox("Inspector", &inspectorOpen_);
        if (!proj_.audioStatus.empty()) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            if (proj_.audioStatusIsError) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", proj_.audioStatus.c_str());
            else ImGui::TextDisabled("%s", proj_.audioStatus.c_str());
        }
    }

    // ---- ruler ------------------------------------------------------------------------------
    void drawRuler(ImVec2 origin) {
        Transport& tr = proj_.transport;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(origin, ImVec2(canvasX0_ + canvasW_, origin.y + kRulerH), IM_COL32(30, 30, 33, 255));
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
        // markers: small flags
        for (const Marker& m : proj_.timeline.markers) {
            float x = tToX(m.time);
            if (x < canvasX0_ - 8 || x > canvasX0_ + canvasW_ + 8) continue;
            dl->AddLine(ImVec2(x, origin.y + 4), ImVec2(x, origin.y + kRulerH), kMarker, 1.5f);
            dl->AddTriangleFilled(ImVec2(x, origin.y + 4), ImVec2(x + 10, origin.y + 8), ImVec2(x, origin.y + 12), kMarker);
        }
        dl->PopClipRect();

        ImGui::SetCursorScreenPos(ImVec2(canvasX0_, origin.y));
        ImGui::InvisibleButton("##ruler", ImVec2(canvasW_, kRulerH));
        const ImVec2 mp = ImGui::GetIO().MousePos;
        if (ImGui::IsItemActive() && ImGui::IsMouseDown(0)) {
            tr.time = snapToFrame(std::max(0.0, std::min(proj_.timeline.length, xToT(mp.x))));
            tr.seeked = true;
            dirty();
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
            for (const Marker& m : proj_.timeline.markers)
                if (std::fabs(tToX(m.time) - mp.x) < 9.0f) { rulerCtxMarker_ = m.id; ImGui::OpenPopup("##rulerctx"); break; }
        }
        float px = tToX(tr.time);
        if (px >= canvasX0_ && px <= canvasX0_ + canvasW_)
            dl->AddTriangleFilled(ImVec2(px - 6, origin.y + kRulerH - 10), ImVec2(px + 6, origin.y + kRulerH - 10), ImVec2(px, origin.y + kRulerH - 1), kAccent);
    }

    void drawRulerContextMenu() {
        if (!ImGui::BeginPopup("##rulerctx")) return;
        if (ImGui::MenuItem("Delete marker")) { proj_.timeline.removeMarker(rulerCtxMarker_); }
        ImGui::EndPopup();
    }

    // ---- hit testing -----------------------------------------------------------------------
    struct Hit {
        enum Kind { None, Lane, ClipHeader, ClipEdge, ClipBody, Key } kind = None;
        int track = 0, clip = 0, key = -1;
        bool audio = false;
        double time = 0.0;
    };

    ImVec2 keyPos(const Clip& c, const Keyframe& k, float laneTop, const ParamInfo& pi) const {
        return ImVec2(tToX(c.start + k.t), valueToY(k.v, laneTop, pi));
    }
    float laneTopOf(const Track* tk) const {
        int ti = (int)(tk - &proj_.timeline.tracks[0]);
        return (ti >= 0 && ti < (int)laneTop_.size()) ? laneTop_[ti] : 0.0f;
    }

    Hit hitTest(ImVec2 m) {
        Hit h;
        Timeline& tl = proj_.timeline;
        int ti = -1;
        for (size_t i = 0; i < laneTop_.size(); i++) if (m.y >= laneTop_[i] && m.y < laneTop_[i] + laneH_[i]) { ti = (int)i; break; }
        if (ti < 0 || ti >= (int)tl.tracks.size()) return h;
        Track& tk = tl.tracks[ti];
        const float laneTop = laneTop_[ti], laneH = laneH_[ti];
        h.kind = Hit::Lane; h.track = tk.id; h.time = xToT(m.x); h.audio = tk.kind == TrackKind::Audio;

        if (tk.kind == TrackKind::Audio) {
            for (AudioClip& c : tk.audioClips) {
                float x0 = tToX(c.start), x1 = tToX(c.end());
                if (m.x < x0 || m.x > x1 || m.y < laneTop + 2 || m.y > laneTop + laneH - 2) continue;
                h.clip = c.id;
                if (m.x > x1 - 7)                       h.kind = Hit::ClipEdge;
                else if (m.y <= laneTop + 2 + kHeaderH) h.kind = Hit::ClipHeader;
                else                                    h.kind = Hit::ClipBody;
                return h;
            }
            return h;
        }

        ParamInfo pi = infoFor(tk);
        for (Clip& c : tk.clips) {
            float x0 = tToX(c.start), x1 = tToX(c.end());
            if (m.x < x0 - 10 || m.x > x1 + 10) continue;
            for (size_t k = 0; k < c.data->keys.size(); k++) {
                ImVec2 kp = keyPos(c, c.data->keys[k], laneTop, pi);
                if (std::hypot(kp.x - m.x, kp.y - m.y) <= 9.0f) { h.kind = Hit::Key; h.clip = c.id; h.key = (int)k; return h; }
            }
            if (m.x < x0 || m.x > x1 || m.y < laneTop + 2 || m.y > laneTop + laneH - 2) continue;
            h.clip = c.id;
            if (m.x > x1 - 7)                          h.kind = Hit::ClipEdge;
            else if (m.y <= laneTop + 2 + kHeaderH)    h.kind = Hit::ClipHeader;
            else                                       h.kind = Hit::ClipBody;
            return h;
        }
        return h;
    }

    // ---- beats ---------------------------------------------------------------------------------
    // Calls f(timelineTime, beatIndexInFile) for each beat of an audio clip between t0 and t1.
    template <class F> void forEachBeat(const AudioClip& c, const AudioAsset& a, double t0, double t1, F&& f) const {
        if (a.bpm <= 0.0) return;
        const double period = 60.0 / a.bpm;
        double lo = std::max(t0, c.start), hi = std::min(t1, c.end());
        if (hi <= lo) return;
        long k0 = (long)std::ceil((lo - c.start + c.offset - a.beatOffset) / period - 1e-9);
        long k1 = (long)std::floor((hi - c.start + c.offset - a.beatOffset) / period + 1e-9);
        if (k1 - k0 > 4000) return;
        for (long k = k0; k <= k1; k++) f(c.start - c.offset + a.beatOffset + (double)k * period, k);
    }

    // ---- drawing the lanes ---------------------------------------------------------------
    void drawAudioClip(ImDrawList* dl, const Track& tk, const AudioClip& c, float laneTop) {
        const float x0 = tToX(c.start), x1 = tToX(c.end());
        const float cx1 = canvasX0_ + canvasW_;
        if (x1 < canvasX0_ || x0 > cx1) return;
        const bool selected = (c.id == sel_.audioClip && tk.id == sel_.track);
        const float top = laneTop + 2, hb = top + kHeaderH, bot = laneTop + kAudioLaneH - 2;
        const AudioAsset* a = proj_.findAsset(c.assetId);

        dl->AddRectFilled(ImVec2(x0, hb), ImVec2(x1, bot), IM_COL32(36, 84, 92, 255));
        dl->AddRectFilled(ImVec2(x0, top), ImVec2(x1, hb), kSalmon);
        dl->PushClipRect(ImVec2(x0, top), ImVec2(std::max(x0, x1 - 4), hb), true);
        std::string title = a ? a->name : c.name;
        if (a && a->missing) title += "  (file missing)";
        else if (a && a->loading) title += "  (loading...)";
        dl->AddText(ImVec2(x0 + 4, top + 1), IM_COL32(40, 20, 20, 255), title.c_str());
        dl->PopClipRect();

        // waveform
        if (a && !a->peakMax.empty()) {
            const float bodyTop = hb + 2, bodyBot = bot - 2, mid = (bodyTop + bodyBot) * 0.5f, amp = (bodyBot - bodyTop) * 0.5f - 1.0f;
            const long nb = (long)a->peakMax.size();
            const float xa = std::max(x0, canvasX0_), xb = std::min(x1, cx1);
            const ImU32 wave = IM_COL32(132, 196, 205, 255);
            for (float x = xa; x < xb; x += 1.0f) {
                double t0 = std::max(xToT(x) - c.start, 0.0) + c.offset;
                double t1 = std::min(xToT(x + 1.0f) - c.start, c.duration) + c.offset;
                long b0 = (long)(t0 * kAudioRate / kPeakBlock), b1 = (long)(t1 * kAudioRate / kPeakBlock);
                b0 = std::max(0L, std::min(nb - 1, b0)); b1 = std::max(b0, std::min(nb - 1, b1));
                long stride = std::max(1L, (b1 - b0) / 48);
                float lo = 1e9f, hi = -1e9f;
                for (long b = b0; b <= b1; b += stride) { lo = std::min(lo, a->peakMin[b]); hi = std::max(hi, a->peakMax[b]); }
                lo = std::max(-1.0f, lo * c.gain); hi = std::min(1.0f, hi * c.gain);
                if (hi < lo) continue;
                dl->AddLine(ImVec2(x + 0.5f, mid - hi * amp), ImVec2(x + 0.5f, mid - lo * amp + 1.0f), wave);
            }
            // beat grid: green lines, brighter every fourth beat
            if (a->bpm > 0.0 && 60.0 / a->bpm * pps_ >= 4.0) {
                dl->PushClipRect(ImVec2(xa, hb), ImVec2(xb, bot), true);
                forEachBeat(c, *a, xToT(canvasX0_), xToT(cx1), [&](double t, long k) {
                    float x = tToX(t);
                    bool bar = ((k % 4) + 4) % 4 == 0;
                    dl->AddLine(ImVec2(x, hb), ImVec2(x, bot), bar ? IM_COL32(90, 225, 110, 235) : IM_COL32(90, 225, 110, 120), bar ? 1.5f : 1.0f);
                });
                dl->PopClipRect();
            }
        }
        if (selected) dl->AddRect(ImVec2(x0, top), ImVec2(x1, bot), kAccent, 0.0f, 0, 2.0f);
    }

    void drawClip(ImDrawList* dl, const Track& tk, const Clip& c, float laneTop, const ParamInfo& pi) {
        const float x0 = tToX(c.start), x1 = tToX(c.end());
        if (x1 < canvasX0_ || x0 > canvasX0_ + canvasW_) return;
        const bool selected = (c.id == sel_.clip && tk.id == sel_.track);
        const float top = laneTop + 2, hb = top + kHeaderH, bot = laneTop + kLaneH - 2;

        dl->AddRectFilled(ImVec2(x0, hb), ImVec2(x1, bot), IM_COL32(30, 52, 56, 255));
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

        double len = c.data->length();
        if (len > 1e-6 && c.duration > len + 1e-6)
            for (double t = len; t < c.duration - 1e-6; t += len) {
                float lx = tToX(c.start + t);
                dl->AddLine(ImVec2(lx, hb), ImVec2(lx, bot), IM_COL32(236, 224, 80, 90), 1.0f);
            }

        std::vector<ImVec2> pts;
        float xa = std::max(x0, canvasX0_), xb = std::min(x1, canvasX0_ + canvasW_);
        for (float x = xa; x < xb; x += 2.0f) pts.push_back(ImVec2(x, valueToY(c.valueAtLocal(xToT(x) - c.start), laneTop, pi)));
        pts.push_back(ImVec2(xb, valueToY(c.valueAtLocal(xToT(xb) - c.start), laneTop, pi)));
        if (pts.size() > 1) dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(210, 215, 220, 255), ImDrawFlags_None, 1.5f);

        for (size_t k = 0; k < c.data->keys.size(); k++) {
            const Keyframe& kf = c.data->keys[k];
            ImVec2 p = keyPos(c, kf, laneTop, pi);
            bool sel = selected && sel_.key == (int)k;
            ImU32 fill = sel ? kAccent : IM_COL32(235, 235, 235, 255);
            switch (kf.interp) {
                case Interp::Linear:
                    dl->AddQuadFilled(ImVec2(p.x, p.y - 6), ImVec2(p.x + 6, p.y), ImVec2(p.x, p.y + 6), ImVec2(p.x - 6, p.y), fill); break;
                case Interp::Hold:
                    dl->AddRectFilled(ImVec2(p.x - 5, p.y - 5), ImVec2(p.x + 5, p.y + 5), fill); break;
                default:
                    dl->AddCircleFilled(p, 5.5f, fill);
            }
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
        const float cx1 = canvasX0_ + canvasW_;
        geom_.laneTop0 = cp.y;
        bool openCtx = false;

        // lane positions for this frame (audio lanes are taller)
        laneTop_.clear(); laneH_.clear();
        float y = cp.y;
        for (const Track& t : tl.tracks) {
            float h = (t.kind == TrackKind::Audio) ? kAudioLaneH : kLaneH;
            laneTop_.push_back(y); laneH_.push_back(h); y += h;
        }
        const float totalH = y - cp.y;

        // --- names column, lane backgrounds, hold lines, clips
        for (int i = 0; i < (int)tl.tracks.size(); i++) {
            Track& tk = tl.tracks[i];
            const bool isAudio = tk.kind == TrackKind::Audio;
            const ParamInfo pi = isAudio ? ParamInfo{} : infoFor(tk);
            const float top = laneTop_[i], bot = top + laneH_[i];
            const bool trackSel = tk.id == sel_.track;

            dl->AddRectFilled(ImVec2(cp.x, top), ImVec2(canvasX0_, bot - 1), trackSel ? IM_COL32(24, 66, 72, 255) : IM_COL32(36, 36, 40, 255));
            dl->AddRectFilled(ImVec2(canvasX0_, top), ImVec2(cx1, bot - 1), (i % 2) ? IM_COL32(27, 27, 30, 255) : IM_COL32(31, 31, 34, 255));

            ImGui::PushID(tk.id);
            std::string label = isAudio ? tk.name : (pi.group.empty() ? pi.label : (pi.group + " / " + pi.label));
            ImGui::PushClipRect(ImVec2(cp.x, top), ImVec2(canvasX0_ - 40, bot), true);
            ImGui::SetCursorScreenPos(ImVec2(cp.x + 8, top + 8));
            ImGui::TextUnformatted(label.c_str());
            if (isAudio) {
                ImGui::SetCursorScreenPos(ImVec2(cp.x + 8, top + 30));
                const AudioClip* sc = nullptr;
                for (const AudioClip& c : tk.audioClips) if (c.id == sel_.audioClip) sc = &c;
                if (!sc && !tk.audioClips.empty()) sc = &tk.audioClips.front();
                const AudioAsset* a = sc ? proj_.findAsset(sc->assetId) : nullptr;
                if (a && a->bpm > 0.0) ImGui::TextDisabled("%.1f BPM", a->bpm);
                else ImGui::TextDisabled(tk.audioClips.empty() ? "Drop a song here" : "BPM unknown");
            } else {
                float cur;
                if (getParam(proj_, tk.path, cur)) {
                    ImGui::SetCursorScreenPos(ImVec2(cp.x + 8, top + 30));
                    ImGui::TextDisabled("%s", valueText(pi, cur).c_str());
                }
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

            dl->PushClipRect(ImVec2(canvasX0_, cp.y), ImVec2(cx1, wp.y + ws.y), true);
            if (isAudio) {
                for (const AudioClip& c : tk.audioClips) drawAudioClip(dl, tk, c, top);
            } else {
                if (!tk.clips.empty()) {
                    ImU32 hold = IM_COL32(150, 155, 160, 140);
                    const Clip& first = tk.clips.front();
                    float y0 = valueToY(first.valueAtLocal(0.0), top, pi);
                    dl->AddLine(ImVec2(canvasX0_, y0), ImVec2(tToX(first.start), y0), hold, 1.0f);
                    for (size_t k = 0; k < tk.clips.size(); k++) {
                        const Clip& c = tk.clips[k];
                        float yy = valueToY(c.valueAtLocal(c.duration), top, pi);
                        float xe = (k + 1 < tk.clips.size()) ? tToX(tk.clips[k + 1].start) : cx1;
                        dl->AddLine(ImVec2(tToX(c.end()), yy), ImVec2(xe, yy), hold, 1.0f);
                    }
                }
                for (const Clip& c : tk.clips) drawClip(dl, tk, c, top, pi);
            }
            dl->PopClipRect();
        }
        if (tl.tracks.empty()) {
            ImGui::SetCursorScreenPos(ImVec2(canvasX0_ + 16, cp.y + 16));
            ImGui::TextDisabled("No tracks yet. Click \"+ Track\" to animate a parameter, or drop a song onto the window.");
        }

        // faint beat lines and marker lines through every lane, so keyframes can be lined up by eye
        dl->PushClipRect(ImVec2(canvasX0_, wp.y), ImVec2(cx1, wp.y + ws.y), true);
        const float gridBottom = std::max(cp.y + totalH, wp.y + ws.y);
        for (const Track& t : tl.tracks)
            if (t.kind == TrackKind::Audio && !t.muted)
                for (const AudioClip& c : t.audioClips) {
                    const AudioAsset* a = proj_.findAsset(c.assetId);
                    if (!a || a->bpm <= 0.0 || 60.0 / a->bpm * pps_ < 8.0) continue;
                    forEachBeat(c, *a, xToT(canvasX0_), xToT(cx1), [&](double tt, long k) {
                        float x = tToX(tt);
                        bool bar = ((k % 4) + 4) % 4 == 0;
                        dl->AddLine(ImVec2(x, wp.y), ImVec2(x, gridBottom), IM_COL32(90, 225, 110, bar ? 40 : 18));
                    });
                }
        for (const Marker& m : tl.markers) {
            float x = tToX(m.time);
            dl->AddLine(ImVec2(x, wp.y), ImVec2(x, gridBottom), IM_COL32(255, 170, 40, 90), 1.0f);
        }
        dl->PopClipRect();

        // --- the interactive canvas on top of the lanes
        ImGui::SetCursorScreenPos(ImVec2(canvasX0_, cp.y));
        ImGui::InvisibleButton("##canvas", ImVec2(canvasW_, std::max(totalH, ws.y)));
        const bool hovered = ImGui::IsItemHovered();

        if (hovered) {
            if (io.KeyCtrl && io.MouseWheel != 0.0f) {
                double tm = xToT(io.MousePos.x);
                pps_ = std::max(10.0f, std::min(600.0f, pps_ * (1.0f + 0.15f * io.MouseWheel)));
                scrollT_ = std::max(0.0, tm - (io.MousePos.x - canvasX0_) / pps_);
            } else if (io.KeyShift && io.MouseWheel != 0.0f) {
                scrollT_ = std::max(0.0, scrollT_ - io.MouseWheel * 80.0 / pps_);
            }
        }

        Hit h = hovered ? hitTest(io.MousePos) : Hit{};
        if (hovered && (h.kind == Hit::ClipEdge || drag_.kind == DragKind::Resize || drag_.kind == DragKind::AudioResize)) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        if (hovered && ImGui::IsMouseDoubleClicked(0)) onDoubleClick(h, io.MousePos);
        else if (hovered && ImGui::IsMouseClicked(0))  onClick(h, io.MousePos);
        if (hovered && ImGui::IsMouseClicked(1)) {
            sel_.track = h.track;
            ctx_ = Ctx{}; ctx_.track = h.track; ctx_.clip = h.clip; ctx_.key = h.key; ctx_.time = h.time;
            if (h.kind == Hit::Key) { ctx_.kind = Ctx::KeyCtx; sel_.clip = h.clip; sel_.audioClip = 0; sel_.key = h.key; }
            else if (h.kind == Hit::ClipHeader || h.kind == Hit::ClipBody || h.kind == Hit::ClipEdge) {
                if (h.audio) { ctx_.kind = Ctx::AudioClipCtx; sel_.audioClip = h.clip; sel_.clip = 0; }
                else         { ctx_.kind = Ctx::ClipCtx;      sel_.clip = h.clip; sel_.audioClip = 0; }
                sel_.key = -1;
            }
            else if (h.kind == Hit::Lane && !h.audio) ctx_.kind = Ctx::LaneCtx;
            if (ctx_.kind != Ctx::None) openCtx = true;
        }
        updateDrag(io.MousePos);

        if (!io.WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) deleteSelection();

        dl->PushClipRect(ImVec2(canvasX0_, wp.y), ImVec2(cx1, wp.y + ws.y), true);
        float px = tToX(tr.time);
        dl->AddLine(ImVec2(px, wp.y), ImVec2(px, wp.y + ws.y), kAccent, 1.5f);
        dl->PopClipRect();

        if (openCtx) ImGui::OpenPopup("##ctx");
        drawContextMenu();
    }

    // ---- mouse interaction -----------------------------------------------------------------
    struct Sel { int track = 0, clip = 0, key = -1, audioClip = 0; };
    enum class DragKind { None, Key, Move, Resize, AudioMove, AudioResize };
    struct Drag { DragKind kind = DragKind::None; int track = 0, clip = 0, key = -1; double grab = 0.0; };
    struct Ctx { enum Kind { None, LaneCtx, ClipCtx, KeyCtx, TrackName, AudioClipCtx } kind = None; int track = 0, clip = 0, key = -1; double time = 0.0; };

    // Snap a time to the frame grid, or to a beat / marker / clip edge / the playhead if one is close.
    // With Snap switched off nothing is adjusted.
    double snapTime(double t, int exceptClip, int exceptAudio = 0) {
        if (!snap_) return t;
        double best = snapToFrame(t), bestD = 8.0 / pps_;
        auto consider = [&](double c) { if (std::fabs(c - t) < bestD) { bestD = std::fabs(c - t); best = c; } };
        consider(proj_.transport.time);
        for (const Marker& m : proj_.timeline.markers) consider(m.time);
        for (const Track& tk : proj_.timeline.tracks) {
            for (const Clip& c : tk.clips) if (c.id != exceptClip) { consider(c.start); consider(c.end()); }
            for (const AudioClip& c : tk.audioClips) {
                if (c.id != exceptAudio) { consider(c.start); consider(c.end()); }
                const AudioAsset* a = proj_.findAsset(c.assetId);
                if (a && a->bpm > 0.0 && t >= c.start - 1.0 && t <= c.end() + 1.0) {
                    const double period = 60.0 / a->bpm, base = c.start - c.offset + a->beatOffset;
                    double tb = base + std::round((t - base) / period) * period;
                    if (tb >= c.start - 1e-9 && tb <= c.end() + 1e-9) consider(tb);
                }
            }
        }
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
                if (h.audio) { sel_.audioClip = h.clip; drag_.kind = DragKind::AudioResize; }
                else         { sel_.clip = h.clip;      drag_.kind = DragKind::Resize; }
                drag_.track = h.track; drag_.clip = h.clip;
                break;
            case Hit::ClipHeader: {
                Track* tk = proj_.timeline.findTrack(h.track);
                if (!tk) break;
                if (h.audio) {
                    sel_.audioClip = h.clip;
                    AudioClip* c = tk->findAudioClip(h.clip);
                    if (c) { drag_.kind = DragKind::AudioMove; drag_.track = h.track; drag_.clip = h.clip; drag_.grab = xToT(m.x) - c->start; }
                } else {
                    sel_.clip = h.clip;
                    Clip* c = tk->findClip(h.clip);
                    if (c) { drag_.kind = DragKind::Move; drag_.track = h.track; drag_.clip = h.clip; drag_.grab = xToT(m.x) - c->start; }
                }
                break;
            }
            case Hit::ClipBody: if (h.audio) sel_.audioClip = h.clip; else sel_.clip = h.clip; break;
            default: break;
        }
    }

    void onDoubleClick(const Hit& h, ImVec2 m) {
        if (h.audio || (h.kind != Hit::ClipBody && h.kind != Hit::ClipHeader)) return;
        Track* tk = proj_.timeline.findTrack(h.track);
        Clip* c = tk ? tk->findClip(h.clip) : nullptr;
        if (!c) return;
        const ParamInfo pi = infoFor(*tk);
        double local = h.time - c->start, len = c->data->length();
        if (len > 1e-9 && local > len) local = std::fmod(local, len);
        local = std::max(0.0, std::min(local, c->duration));
        local = snapToFrame(local);
        auto& keys = c->data->keys;
        for (const Keyframe& k : keys) if (std::fabs(k.t - local) < 1e-4) return;
        Keyframe nk; nk.t = local; nk.v = yToValue(m.y, laneTopOf(tk), pi);
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
        if (!tk) { drag_ = Drag{}; return; }
        const double mt = xToT(m.x);

        if (drag_.kind == DragKind::AudioMove || drag_.kind == DragKind::AudioResize) {
            AudioClip* c = tk->findAudioClip(drag_.clip);
            if (!c) { drag_ = Drag{}; return; }
            if (drag_.kind == DragKind::AudioMove) {
                double lo = tk->minStartForAudio(c->id), hi = tk->maxEndForAudio(c->id) - c->duration;
                double want = mt - drag_.grab;
                double s = snapTime(want, 0, c->id);
                double e = snapTime(want + c->duration, 0, c->id) - c->duration;      // also try the clip's end
                if (std::fabs(e - want) < std::fabs(s - want)) s = e;
                c->start = std::max(lo, std::min(std::max(lo, hi), s));
            } else {
                const AudioAsset* a = proj_.findAsset(c->assetId);
                double maxDur = std::min(tk->maxEndForAudio(c->id) - c->start, a ? a->duration() - c->offset : 1e18);
                c->duration = std::max(1.0 / kTimelineFps, std::min(maxDur, snapTime(mt, 0, c->id) - c->start));
            }
            tl.extendTo(c->end());
            dirty();
            return;
        }

        Clip* c = tk->findClip(drag_.clip);
        if (!c) { drag_ = Drag{}; return; }
        if (drag_.kind == DragKind::Key) {
            auto& keys = c->data->keys;
            int k = drag_.key;
            if (k < 0 || k >= (int)keys.size()) { drag_ = Drag{}; return; }
            const ParamInfo pi = infoFor(*tk);
            double lo = (k > 0) ? keys[k - 1].t : 0.0;
            double hi = (k + 1 < (int)keys.size()) ? keys[k + 1].t : c->duration;
            keys[k].t = std::max(lo, std::min(hi, snapTime(mt, c->id) - c->start));
            keys[k].v = yToValue(m.y, laneTopOf(tk), pi);
            dirty();
        } else if (drag_.kind == DragKind::Move) {
            double lo = tk->minStartFor(c->id), hi = tk->maxEndFor(c->id) - c->duration;
            double want = mt - drag_.grab;
            double s = snapTime(want, c->id);
            double e = snapTime(want + c->duration, c->id) - c->duration;
            if (std::fabs(e - want) < std::fabs(s - want)) s = e;
            c->start = std::max(lo, std::min(std::max(lo, hi), s));
            tl.extendTo(c->end());
            dirty();
        } else if (drag_.kind == DragKind::Resize) {
            double minDur = 1.0 / kTimelineFps, maxDur = tk->maxEndFor(c->id) - c->start;
            c->duration = std::max(minDur, std::min(maxDur, snapTime(mt, c->id) - c->start));
            tl.extendTo(c->end());
            dirty();
        }
    }

    void deleteSelection() {
        Timeline& tl = proj_.timeline;
        Track* tk = tl.findTrack(sel_.track);
        if (!tk) return;
        if (sel_.audioClip) { tl.removeAudioClip(tk->id, sel_.audioClip); sel_ = Sel{}; dirty(); return; }
        Clip* c = tk->findClip(sel_.clip);
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
        } else if (ctx_.kind == Ctx::AudioClipCtx) {
            const double t = proj_.transport.time;
            AudioClip* ac = tk->findAudioClip(ctx_.clip);
            const bool canSplit = ac && t > ac->start + 0.05 && t < ac->end() - 0.05;
            if (ImGui::MenuItem("Split at playhead", nullptr, false, canSplit)) {
                int id = tl.splitAudioClip(tk->id, ctx_.clip, t);
                if (id) { sel_.audioClip = id; dirty(); }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete clip")) { tl.removeAudioClip(tk->id, ctx_.clip); sel_ = Sel{}; dirty(); }
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

    // ---- inspector -------------------------------------------------------------------------------
    // Numeric field that applies its value when you press Enter or click away (so typing isn't
    // clamped halfway through). Returns true when a new value was committed.
    static bool commitDouble(const char* label, double& v, const char* fmt = "%.3f") {
        double tmp = v;
        ImGui::InputDouble(label, &tmp, 0.0, 0.0, fmt);
        if (ImGui::IsItemDeactivatedAfterEdit()) { v = tmp; return true; }
        return false;
    }

    void drawInspector(float height) {
        ImGui::BeginChild("##insp", ImVec2(kInspW, height), ImGuiChildFlags_Borders);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 3.0f));      // compact: the panel is short
        ImGui::PushItemWidth(110.0f);
        Timeline& tl = proj_.timeline;
        Track* tk = tl.findTrack(sel_.track);
        if (!tk) {
            ImGui::TextDisabled("Select a clip, keyframe or track to edit it here.");
        } else if (tk->kind == TrackKind::Audio && sel_.audioClip) {
            if (AudioClip* c = tk->findAudioClip(sel_.audioClip)) inspectAudioClip(*tk, *c);
        } else if (Clip* c = tk->findClip(sel_.clip)) {
            if (sel_.key >= 0 && sel_.key < (int)c->data->keys.size()) inspectKeyframe(*tk, *c, sel_.key);
            else inspectParamClip(*tk, *c);
        } else {
            ImGui::TextUnformatted(tk->kind == TrackKind::Audio ? tk->name.c_str() : infoFor(*tk).label.c_str());
            ImGui::TextDisabled("%s", tk->kind == TrackKind::Audio ? "Audio track" : "Parameter track");
        }
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::EndChild();
    }

    void inspectAudioClip(Track& tk, AudioClip& c) {
        AudioAsset* a = proj_.findAsset(c.assetId);
        ImGui::SeparatorText("Audio clip");
        ImGui::TextUnformatted(a ? a->name.c_str() : c.name.c_str());
        if (a && a->missing) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "Song file not found");
            ImGui::TextDisabled("%s", a->path.c_str());
            if (ImGui::Button("Relink song...") && onRelinkAudio) onRelinkAudio(a->id);
        } else if (a && a->loading) {
            ImGui::TextDisabled("Loading the song...");
        }
        const double fileLen = a ? a->duration() : 1e9;

        double v = c.start;
        if (commitDouble("Time", v)) { c.start = std::max(tk.minStartForAudio(c.id), std::min(v, tk.maxEndForAudio(c.id) - c.duration)); dirty(); }
        v = c.duration;
        if (commitDouble("Duration", v)) {
            double maxDur = std::min(tk.maxEndForAudio(c.id) - c.start, fileLen - c.offset);
            c.duration = std::max(1.0 / kTimelineFps, std::min(v, maxDur)); proj_.timeline.extendTo(c.end()); dirty();
        }
        v = c.offset;
        if (commitDouble("Start offset", v)) {
            c.offset = std::max(0.0, std::min(v, fileLen - 1.0 / kTimelineFps));
            c.duration = std::min(c.duration, fileLen - c.offset); dirty();
        }
        float pct = c.gain * 100.0f;
        if (ImGui::SliderFloat("Gain", &pct, 0.0f, 200.0f, "%.0f%%")) { c.gain = pct / 100.0f; dirty(); }

        if (!a) return;
        ImGui::SeparatorText("Beats");
        double bpm = a->bpm;
        if (commitDouble("BPM", bpm, "%.2f")) a->bpm = (bpm <= 0.0) ? 0.0 : std::max(20.0, std::min(300.0, bpm));
        if (ImGui::SmallButton("x2")) { if (a->bpm > 0.0 && a->bpm * 2.0 <= 300.0) a->bpm *= 2.0; }
        ImGui::SameLine();
        if (ImGui::SmallButton("/2")) { if (a->bpm >= 40.0) a->bpm *= 0.5; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Beat = playhead")) {                 // line the grid up with the playhead
            if (a->bpm > 0.0) {
                double period = 60.0 / a->bpm, fileT = proj_.transport.time - c.start + c.offset;
                a->beatOffset = std::fmod(std::fmod(fileT, period) + period, period);
            }
        }
        double bo = a->beatOffset;
        if (commitDouble("Beat offset", bo) && a->bpm > 0.0) {
            double period = 60.0 / a->bpm;
            a->beatOffset = std::fmod(std::fmod(bo, period) + period, period);
        }
        if (a->bpm <= 0.0) ImGui::TextDisabled("No tempo found. Type a BPM above.");
    }

    void inspectParamClip(Track& tk, Clip& c) {
        ImGui::SeparatorText("Clip");
        char nm[64]; std::snprintf(nm, sizeof(nm), "%s", c.name.c_str());
        if (ImGui::InputText("Name", nm, sizeof(nm))) c.name = nm;
        double v = c.start;
        if (commitDouble("Time", v)) { c.start = std::max(tk.minStartFor(c.id), std::min(v, tk.maxEndFor(c.id) - c.duration)); proj_.timeline.extendTo(c.end()); dirty(); }
        v = c.duration;
        if (commitDouble("Duration", v)) { c.duration = std::max(1.0 / kTimelineFps, std::min(v, tk.maxEndFor(c.id) - c.start)); proj_.timeline.extendTo(c.end()); dirty(); }
        double len = c.data->length();
        ImGui::TextDisabled("%d keyframes%s", (int)c.data->keys.size(), c.ghost ? ", ghost" : "");
        if (len > 1e-6) ImGui::TextDisabled("Loops %.1f times", c.duration / len);
    }

    void inspectKeyframe(Track& tk, Clip& c, int k) {
        auto& keys = c.data->keys;
        const ParamInfo pi = infoFor(tk);
        ImGui::SeparatorText("Keyframe");
        double lo = (k > 0) ? keys[k - 1].t : 0.0, hi = (k + 1 < (int)keys.size()) ? keys[k + 1].t : c.duration;
        double t = keys[k].t;
        if (commitDouble("Time", t)) { keys[k].t = std::max(lo, std::min(hi, t)); dirty(); }
        double val = pi.percent ? keys[k].v * 100.0 : keys[k].v;
        if (commitDouble(pi.percent ? "Value %" : "Value", val, "%.2f")) {
            double nv = pi.percent ? val / 100.0 : val;
            keys[k].v = (float)std::max((double)pi.min, std::min((double)pi.max, nv)); dirty();
        }
        int cur = (int)keys[k].interp;
        const char* names[] = {"Linear", "Smooth", "Hold"};
        if (ImGui::Combo("Curve", &cur, names, 3)) { keys[k].interp = (Interp)cur; dirty(); }
    }

    Project& proj_;
    float    pps_ = 60.0f;                 // pixels per second
    double   scrollT_ = 0.0;               // time at the left edge of the canvas
    float    canvasX0_ = 0, canvasW_ = 100;
    std::vector<float> laneTop_, laneH_;   // this frame's lane positions
    bool     snap_ = true;
    bool     inspectorOpen_ = true;
    int      rulerCtxMarker_ = 0;
    Sel      sel_;
    Drag     drag_;
    Ctx      ctx_;
    Geom     geom_;
};
