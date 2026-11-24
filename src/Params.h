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
    bool (*applies)(const Surface&) = nullptr;   // only some shapes have it (nullptr = every surface)
    bool appliesTo(const Surface& s) const { return !applies || applies(s); }
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
        // ---- shape-specific
        ParamDef{"lineWidth", "Line width", "Line", 0.001f, 0.5f, 0.02f, ParamFormat::Percent,
                 [](const Surface& s) { return s.lineWidth; }, [](Surface& s, float v) { s.lineWidth = v; },
                 [](const Surface& s) { return s.shape == Shape::Line; }},
#define MH_PARAM3D(KEY, LABEL, MIN, MAX, DEF, FMT, MEMBER) \
        ParamDef{KEY, LABEL, "3D Object", MIN, MAX, DEF, FMT, [](const Surface& s) { return s.MEMBER; }, [](Surface& s, float v) { s.MEMBER = v; }, \
                 [](const Surface& s) { return s.shape == Shape::Object3D; }}
        MH_PARAM3D("rotX",     "Turn X",        -720.0f, 720.0f, -20.0f, ParamFormat::Degrees, rotX),
        MH_PARAM3D("rotY",     "Turn Y",        -720.0f, 720.0f, 30.0f,  ParamFormat::Degrees, rotY),
        MH_PARAM3D("rotZ",     "Turn Z",        -720.0f, 720.0f, 0.0f,   ParamFormat::Degrees, rotZ),
        MH_PARAM3D("objScale", "Size",          0.0f,    4.0f,   1.0f,   ParamFormat::Percent, objScale),
        MH_PARAM3D("fov",      "Field of view", 5.0f,    150.0f, 40.0f,  ParamFormat::Degrees, fov),
        MH_PARAM3D("shading",  "Lighting",      0.0f,    1.0f,   0.0f,   ParamFormat::Percent, shading),
#undef MH_PARAM3D
#define MH_LIGHT(KEY, LABEL, MIN, MAX, DEF, FMT, MEMBER) \
        ParamDef{KEY, LABEL, "Lighting", MIN, MAX, DEF, FMT, [](const Surface& s) { return s.light.MEMBER; }, [](Surface& s, float v) { s.light.MEMBER = v; }, \
                 [](const Surface& s) { return s.shape == Shape::Object3D; }}
        MH_LIGHT("lightModel",     "Light model",     0.0f, 3.0f,     2.0f,  ParamFormat::Plain,   model),
        MH_LIGHT("ambient",        "Ambient",         0.0f, 1.0f,     0.2f,  ParamFormat::Percent, ambient),
        MH_LIGHT("kd",             "Diffuse",         0.0f, 2.0f,     1.0f,  ParamFormat::Percent, kd),
        MH_LIGHT("ks",             "Specular",        0.0f, 2.0f,     0.5f,  ParamFormat::Percent, ks),
        MH_LIGHT("shininess",      "Shininess",       1.0f, 256.0f,   32.0f, ParamFormat::Plain,   shininess),
        MH_LIGHT("dirIntensity",   "Sun strength",    0.0f, 3.0f,     1.0f,  ParamFormat::Percent, dirIntensity),
        MH_LIGHT("dirAzimuth",     "Sun direction",   -180.0f, 180.0f, -30.0f, ParamFormat::Degrees, dirAzimuth),
        MH_LIGHT("dirElevation",   "Sun height",      -90.0f, 90.0f,  30.0f, ParamFormat::Degrees, dirElevation),
        MH_LIGHT("dirR",           "Sun red",         0.0f, 1.0f,     1.0f,  ParamFormat::Percent, dirR),
        MH_LIGHT("dirG",           "Sun green",       0.0f, 1.0f,     1.0f,  ParamFormat::Percent, dirG),
        MH_LIGHT("dirB",           "Sun blue",        0.0f, 1.0f,     1.0f,  ParamFormat::Percent, dirB),
        MH_LIGHT("pointIntensity", "Point strength",  0.0f, 5.0f,     0.0f,  ParamFormat::Percent, pointIntensity),
        MH_LIGHT("pointX",         "Point X",         -5.0f, 5.0f,    1.5f,  ParamFormat::Plain,   pointX),
        MH_LIGHT("pointY",         "Point Y",         -5.0f, 5.0f,    1.0f,  ParamFormat::Plain,   pointY),
        MH_LIGHT("pointZ",         "Point Z",         -5.0f, 5.0f,    2.0f,  ParamFormat::Plain,   pointZ),
        MH_LIGHT("pointRange",     "Point reach",     0.5f, 20.0f,    4.0f,  ParamFormat::Plain,   pointRange),
        MH_LIGHT("pointR",         "Point red",       0.0f, 1.0f,     1.0f,  ParamFormat::Percent, pointR),
        MH_LIGHT("pointG",         "Point green",     0.0f, 1.0f,     0.6f,  ParamFormat::Percent, pointG),
        MH_LIGHT("pointB",         "Point blue",      0.0f, 1.0f,     0.3f,  ParamFormat::Percent, pointB),
        MH_LIGHT("spotIntensity",  "Spot strength",   0.0f, 5.0f,     0.0f,  ParamFormat::Percent, spotIntensity),
        MH_LIGHT("spotX",          "Spot X",          -5.0f, 5.0f,    -2.0f, ParamFormat::Plain,   spotX),
        MH_LIGHT("spotY",          "Spot Y",          -5.0f, 5.0f,    2.0f,  ParamFormat::Plain,   spotY),
        MH_LIGHT("spotZ",          "Spot Z",          -5.0f, 5.0f,    2.5f,  ParamFormat::Plain,   spotZ),
        MH_LIGHT("spotAngle",      "Spot angle",      1.0f, 90.0f,    25.0f, ParamFormat::Degrees, spotAngle),
        MH_LIGHT("spotSoftness",   "Spot softness",   0.0f, 1.0f,     0.3f,  ParamFormat::Percent, spotSoftness),
        MH_LIGHT("spotR",          "Spot red",        0.0f, 1.0f,     0.4f,  ParamFormat::Percent, spotR),
        MH_LIGHT("spotG",          "Spot green",      0.0f, 1.0f,     0.7f,  ParamFormat::Percent, spotG),
        MH_LIGHT("spotB",          "Spot blue",       0.0f, 1.0f,     1.0f,  ParamFormat::Percent, spotB),
#undef MH_LIGHT
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
        for (const ParamDef& d : paramDefs()) if (d.appliesTo(s)) v.push_back(makeInfo(d, s.name, s.id));
    return v;
}

// The categories, in table order ("Appearance", "Transform", ...), for menus.
inline std::vector<std::string> paramCategories() {
    std::vector<std::string> c;
    for (const ParamDef& d : paramDefs()) if (std::find(c.begin(), c.end(), d.category) == c.end()) c.push_back(d.category);
    return c;
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
