#pragma once
#include "Project.h"
#include <cstdlib>
#include <string>
#include <vector>

// Every value a timeline can animate has a stable text path, e.g. "surface/3/posX".
// Tracks store the path, so they keep working when surfaces are reordered or renamed.
//
// All animatable surface parameters live in ONE table (paramDefs() below). The timeline, the
// inspector, the project file and the Add Track menu all read it, so a new parameter is one new row.

enum class ParamFormat { Plain, Percent, Degrees, Bool };

struct ParamDef {
    const char* key;                 // used in paths and in the project file
    const char* label;               // shown in the UI
    const char* category;            // groups the Add Track menu and the inspector
    float min, max, def;
    ParamFormat format;
    float (*get)(const Surface&);
    void (*set)(Surface&, float);
};

#define MH_PARAM(KEY, LABEL, CAT, MIN, MAX, DEF, FMT, MEMBER) \
    ParamDef{KEY, LABEL, CAT, MIN, MAX, DEF, FMT, [](const Surface& s) { return s.MEMBER; }, [](Surface& s, float v) { s.MEMBER = v; }}

inline const std::vector<ParamDef>& paramDefs() {
    static const std::vector<ParamDef> defs = {
        // ---- Appearance
        MH_PARAM("opacity",  "Opacity",  "Appearance", 0.0f, 1.0f, 1.0f, ParamFormat::Percent, opacity),
        ParamDef{"visible", "Visible", "Appearance", 0.0f, 1.0f, 1.0f, ParamFormat::Bool,
                 [](const Surface& s) { return s.visible ? 1.0f : 0.0f; }, [](Surface& s, float v) { s.visible = v >= 0.5f; }},
        MH_PARAM("feather",  "Feather",  "Appearance", 0.0f, 1.0f, 0.0f, ParamFormat::Percent, feather),
        // ---- Transform (on top of the mapped corners)
        MH_PARAM("posX",     "Position X", "Transform", -1.0f, 1.0f, 0.0f, ParamFormat::Percent, posX),
        MH_PARAM("posY",     "Position Y", "Transform", -1.0f, 1.0f, 0.0f, ParamFormat::Percent, posY),
        MH_PARAM("rotation", "Rotation",   "Transform", -720.0f, 720.0f, 0.0f, ParamFormat::Degrees, rotation),
        MH_PARAM("scale",    "Scale",      "Transform", 0.0f, 4.0f, 1.0f, ParamFormat::Percent, scale),
        MH_PARAM("scaleX",   "Scale X",    "Transform", 0.0f, 4.0f, 1.0f, ParamFormat::Percent, scaleX),
        MH_PARAM("scaleY",   "Scale Y",    "Transform", 0.0f, 4.0f, 1.0f, ParamFormat::Percent, scaleY),
        // ---- Content (the picture inside the surface)
        MH_PARAM("contentX", "Offset X",   "Content", -1.0f, 1.0f, 0.0f, ParamFormat::Percent, contentX),
        MH_PARAM("contentY", "Offset Y",   "Content", -1.0f, 1.0f, 0.0f, ParamFormat::Percent, contentY),
        MH_PARAM("contentScale",    "Zoom",     "Content", 0.1f, 8.0f, 1.0f, ParamFormat::Percent, contentScale),
        MH_PARAM("contentRotation", "Rotation", "Content", -720.0f, 720.0f, 0.0f, ParamFormat::Degrees, contentRotation),
        // ---- Color
        MH_PARAM("tintR",      "Red",        "Color", 0.0f, 1.0f, 1.0f, ParamFormat::Percent, tintR),
        MH_PARAM("tintG",      "Green",      "Color", 0.0f, 1.0f, 1.0f, ParamFormat::Percent, tintG),
        MH_PARAM("tintB",      "Blue",       "Color", 0.0f, 1.0f, 1.0f, ParamFormat::Percent, tintB),
        MH_PARAM("hue",        "Hue",        "Color", -360.0f, 360.0f, 0.0f, ParamFormat::Degrees, hue),
        MH_PARAM("saturation", "Saturation", "Color", 0.0f, 2.0f, 1.0f, ParamFormat::Percent, saturation),
        MH_PARAM("contrast",   "Contrast",   "Color", 0.0f, 2.0f, 1.0f, ParamFormat::Percent, contrast),
        MH_PARAM("brightness", "Brightness", "Color", -1.0f, 1.0f, 0.0f, ParamFormat::Percent, brightness),
    };
    return defs;
}

inline const ParamDef* findParamDef(const std::string& key) {
    for (const ParamDef& d : paramDefs()) if (key == d.key) return &d;
    return nullptr;
}

// What the UI needs to know about one parameter of one surface.
struct ParamInfo {
    std::string path, group, label;     // group = surface name, label = e.g. "Position X"
    std::string key, category;
    float min = 0.0f, max = 1.0f, def = 1.0f;
    bool  isBool = false;               // on/off (shown as On/Off, applied as value >= 0.5)
    bool  percent = false;              // shown as 0..100% (the stored value is 0..1)
    bool  degrees = false;              // shown in degrees
};

inline std::string surfaceParamPath(int surfaceId, const std::string& key) {
    return "surface/" + std::to_string(surfaceId) + "/" + key;
}

inline bool parseParamPath(const std::string& path, int& surfaceId, std::string& key) {
    if (path.compare(0, 8, "surface/") != 0) return false;
    size_t slash = path.find('/', 8);
    if (slash == std::string::npos) return false;
    surfaceId = std::atoi(path.substr(8, slash - 8).c_str());
    key = path.substr(slash + 1);
    return surfaceId > 0;
}

inline ParamInfo makeInfo(const ParamDef& d, const std::string& surfaceName, int surfaceId) {
    ParamInfo pi;
    pi.path = surfaceParamPath(surfaceId, d.key); pi.group = surfaceName;
    pi.key = d.key; pi.label = d.label; pi.category = d.category;
    pi.min = d.min; pi.max = d.max; pi.def = d.def;
    pi.isBool = d.format == ParamFormat::Bool;
    pi.percent = d.format == ParamFormat::Percent;
    pi.degrees = d.format == ParamFormat::Degrees;
    return pi;
}

inline bool paramInfo(const Project& p, const std::string& path, ParamInfo& out) {
    int sid; std::string key;
    if (!parseParamPath(path, sid, key)) return false;
    const ParamDef* d = findParamDef(key);
    if (!d) return false;
    for (const Surface& s : p.surfaces) if (s.id == sid) { out = makeInfo(*d, s.name, sid); return true; }
    return false;
}

// Everything that can currently be animated, grouped by surface (front-most surface first).
inline std::vector<ParamInfo> listParams(const Project& p) {
    std::vector<ParamInfo> v;
    for (const Surface& s : p.surfaces)
        for (const ParamDef& d : paramDefs()) v.push_back(makeInfo(d, s.name, s.id));
    return v;
}

inline bool getParam(Project& p, const std::string& path, float& v) {
    int sid; std::string key;
    if (!parseParamPath(path, sid, key)) return false;
    const ParamDef* d = findParamDef(key);
    Surface* s = p.find(sid);
    if (!d || !s) return false;
    v = d->get(*s);
    return true;
}

// Values are kept inside the parameter's range, so a keyframe or a typo can never push a surface somewhere impossible.
inline bool setParam(Project& p, const std::string& path, float v) {
    int sid; std::string key;
    if (!parseParamPath(path, sid, key)) return false;
    const ParamDef* d = findParamDef(key);
    Surface* s = p.find(sid);
    if (!d || !s) return false;
    if (d->format != ParamFormat::Bool) v = std::max(d->min, std::min(d->max, v));
    d->set(*s, v);
    return true;
}

// Writes the value of every unmuted track at the playhead into the project.
inline void applyTimeline(Project& p) {
    for (const Track& t : p.timeline.tracks) {
        if (t.muted) continue;
        float v;
        if (t.evaluate(p.transport.time, v)) setParam(p, t.path, v);
    }
}
