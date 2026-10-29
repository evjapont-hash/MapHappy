#pragma once
#include "Project.h"
#include <cstdlib>
#include <string>
#include <vector>

// Every value a timeline can animate has a stable text path, e.g. "surface/3/opacity".
// Tracks store the path, so they keep working when surfaces are reordered or renamed.
struct ParamInfo {
    std::string path, group, label;     // group = surface name, label = e.g. "Opacity"
    float min = 0.0f, max = 1.0f, def = 1.0f;
    bool  isBool = false;               // on/off (shown as On/Off, applied as value >= 0.5)
    bool  percent = false;              // shown as 0..100%
};

inline std::string surfaceParamPath(int surfaceId, const std::string& name) {
    return "surface/" + std::to_string(surfaceId) + "/" + name;
}

inline bool parseParamPath(const std::string& path, int& surfaceId, std::string& name) {
    if (path.compare(0, 8, "surface/") != 0) return false;
    size_t slash = path.find('/', 8);
    if (slash == std::string::npos) return false;
    surfaceId = std::atoi(path.substr(8, slash - 8).c_str());
    name = path.substr(slash + 1);
    return surfaceId > 0;
}

inline bool paramInfo(const Project& p, const std::string& path, ParamInfo& out) {
    int sid; std::string name;
    if (!parseParamPath(path, sid, name)) return false;
    const Surface* s = nullptr;
    for (const Surface& x : p.surfaces) if (x.id == sid) { s = &x; break; }
    if (!s) return false;
    out = ParamInfo{};
    out.path = path; out.group = s->name;
    if (name == "opacity")      { out.label = "Opacity"; out.percent = true; out.def = 1.0f; return true; }
    if (name == "visible")      { out.label = "Visible"; out.isBool = true;  out.def = 1.0f; return true; }
    return false;
}

// Everything that can currently be animated, grouped by surface (front-most surface first).
inline std::vector<ParamInfo> listParams(const Project& p) {
    std::vector<ParamInfo> v;
    for (const Surface& s : p.surfaces)
        for (const char* n : {"opacity", "visible"}) {
            ParamInfo pi;
            if (paramInfo(p, surfaceParamPath(s.id, n), pi)) v.push_back(pi);
        }
    return v;
}

inline bool getParam(Project& p, const std::string& path, float& v) {
    int sid; std::string name;
    if (!parseParamPath(path, sid, name)) return false;
    Surface* s = p.find(sid);
    if (!s) return false;
    if (name == "opacity") { v = s->opacity; return true; }
    if (name == "visible") { v = s->visible ? 1.0f : 0.0f; return true; }
    return false;
}

inline bool setParam(Project& p, const std::string& path, float v) {
    int sid; std::string name;
    if (!parseParamPath(path, sid, name)) return false;
    Surface* s = p.find(sid);
    if (!s) return false;
    if (name == "opacity") { s->opacity = std::max(0.0f, std::min(1.0f, v)); return true; }
    if (name == "visible") { s->visible = v >= 0.5f; return true; }
    return false;
}

// Writes the value of every unmuted track at the playhead into the project.
inline void applyTimeline(Project& p) {
    for (const Track& t : p.timeline.tracks) {
        if (t.muted) continue;
        float v;
        if (t.evaluate(p.transport.time, v)) setParam(p, t.path, v);
    }
}
