#pragma once
// Live presets ("busking"): store the current look in one of ten slots, and recall it live from the number keys,
// cutting or cross-fading to it.
//
// A preset holds every surface's animatable parameters (Params.h) and media, and every folder's visibility.
// Recalling one changes only what it holds: surfaces added after it was stored are left alone, and surfaces that
// have since been deleted are skipped. A timeline that is playing still drives the parameters it animates.
#include "Params.h"
#include <cmath>
#include <string>
#include <vector>

inline Preset capturePreset(const Project& p, const std::string& name, float fade) {
    Preset pr;
    pr.used = true;
    pr.name = name;
    pr.fade = std::max(0.0f, std::min(30.0f, fade));
    for (const Surface& s : p.surfaces) {
        PresetSurface ps;
        ps.id = s.id; ps.mediaId = s.mediaId;
        for (const ParamDef& d : paramDefs()) if (d.appliesTo(s)) ps.values[d.key] = d.get(s);
        pr.surfaces.push_back(ps);
    }
    for (const Folder& f : p.folders) pr.folders.push_back({f.id, f.visible});
    return pr;
}

// Plays presets: a cut, or a cross-fade over the preset's fade time (eased, so it starts and lands gently).
class PresetPlayer {
public:
    // Starts going to `pr` from wherever things are now (also from the middle of another fade).
    void start(Project& p, const Preset& pr, int slot) {
        items_.clear(); switches_.clear();
        active_ = slot; t_ = 0.0; dur_ = std::max(0.0, (double)pr.fade);
        for (const PresetSurface& ps : pr.surfaces) {
            Surface* s = p.find(ps.id);
            if (!s) continue;                                            // deleted since the preset was stored
            if (p.media.find(ps.mediaId)) s->mediaId = ps.mediaId;       // the picture changes at once
            for (const auto& kv : ps.values) {
                const ParamDef* d = findParamDef(kv.first);
                if (!d || !d->appliesTo(*s) || !std::isfinite(kv.second)) continue;
                const float to = d->format == ParamFormat::Bool ? (kv.second >= 0.5f ? 1.0f : 0.0f) : std::max(d->min, std::min(d->max, kv.second));
                const float from = d->get(*s);
                if (d->format == ParamFormat::Bool) {
                    // switching ON happens at the start (so its fade can be seen), OFF at the end (after it has faded)
                    if (to >= 0.5f) d->set(*s, 1.0f); else if (from >= 0.5f) switches_.push_back({ps.id, 0, d});
                } else if (from != to) items_.push_back({ps.id, d, from, to});
            }
        }
        for (const auto& fv : pr.folders) {
            Folder* f = p.findFolder(fv.first);
            if (!f) continue;
            if (fv.second) f->visible = true; else if (f->visible) switches_.push_back({0, fv.first, nullptr});
        }
        update(p, 0.0);
    }

    // Advances the fade by dt seconds and writes the in-between values. Call once a frame.
    void update(Project& p, double dt) {
        if (active_ < 0 || done_()) return;
        t_ += dt;
        const double k = dur_ <= 0.0 ? 1.0 : std::min(1.0, t_ / dur_);
        const float e = (float)(k * k * (3.0 - 2.0 * k));                // smoothstep
        for (const Item& it : items_) if (Surface* s = p.find(it.surfaceId)) it.def->set(*s, it.from + (it.to - it.from) * e);
        if (k >= 1.0) {
            for (const Switch& sw : switches_) {
                if (sw.def) { if (Surface* s = p.find(sw.surfaceId)) sw.def->set(*s, 0.0f); }
                else if (Folder* f = p.findFolder(sw.folderId)) f->visible = false;
            }
            items_.clear(); switches_.clear();
            finished_ = true;
        } else finished_ = false;
    }

    int    activeSlot() const { return active_; }                       // the last preset played (-1 = none)
    bool   fading() const { return active_ >= 0 && !finished_; }
    float  progress() const { return dur_ <= 0.0 ? 1.0f : (float)std::min(1.0, t_ / dur_); }
    void   reset() { *this = PresetPlayer(); }

private:
    struct Item { int surfaceId; const ParamDef* def; float from, to; };
    struct Switch { int surfaceId; int folderId; const ParamDef* def; };    // def == nullptr: a folder
    bool done_() const { return finished_ && items_.empty() && switches_.empty(); }
    std::vector<Item> items_;
    std::vector<Switch> switches_;
    int active_ = -1;
    double t_ = 0.0, dur_ = 0.0;
    bool finished_ = true;
};
