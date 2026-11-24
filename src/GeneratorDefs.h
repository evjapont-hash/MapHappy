#pragma once
// The media generators (test card, grid, text, solid colour, gradient, strobe, lines): their types, their
// parameters, and how they are saved. Nothing in here needs OpenGL, so it is unit-tested on every platform.
//
// Like Params.h, every generator parameter lives in ONE table (genTypes() below). The inspector, the project
// file, the value checks and the shaders (each parameter is the uniform "u_<key>") all follow it.
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>
#include <vector>

enum class GenType { TestCard = 0, Grid, Text, Solid, Gradient, Strobe, Lines };
enum class GenParamKind { Float, Int, Bool, Color, Choice, Text };

using GenColor = std::array<float, 4>;   // RGBA, 0..1

struct GenChoice { const char* key; const char* label; };

struct GenParamDef {
    const char*  key;                    // shader uniform "u_<key>", and the name in the project file
    const char*  label;                  // shown in the inspector
    GenParamKind kind;
    float        min = 0.0f, max = 1.0f;
    GenColor     def = {0, 0, 0, 1};     // colours use all four; other kinds use def[0]
    const char*  fmt = "%.2f";           // how a Float is shown
    std::vector<GenChoice> choices;      // Choice only (the value is the index)
    const char*  textDef = "";           // Text only
};

struct GenTypeInfo {
    GenType     type;
    const char* key;                     // project file name of the type
    const char* label;                   // shown in the UI
    bool        animated;                // changes over time by itself
    std::vector<GenParamDef> params;
};

namespace gendefs_ {
inline GenParamDef f(const char* k, const char* l, float mn, float mx, float d, const char* fmt = "%.2f") { GenParamDef p{k, l, GenParamKind::Float, mn, mx, {d, 0, 0, 0}, fmt, {}, ""}; return p; }
inline GenParamDef i(const char* k, const char* l, float mn, float mx, float d) { GenParamDef p{k, l, GenParamKind::Int, mn, mx, {d, 0, 0, 0}, "%d", {}, ""}; return p; }
inline GenParamDef b(const char* k, const char* l, bool d) { GenParamDef p{k, l, GenParamKind::Bool, 0, 1, {d ? 1.0f : 0.0f, 0, 0, 0}, "", {}, ""}; return p; }
inline GenParamDef c(const char* k, const char* l, float r, float g, float bl, float a = 1.0f) { GenParamDef p{k, l, GenParamKind::Color, 0, 1, {r, g, bl, a}, "", {}, ""}; return p; }
inline GenParamDef ch(const char* k, const char* l, std::vector<GenChoice> cs, int d = 0) { GenParamDef p{k, l, GenParamKind::Choice, 0, (float)cs.size() - 1, {(float)d, 0, 0, 0}, "", std::move(cs), ""}; return p; }
inline GenParamDef t(const char* k, const char* l, const char* d) { GenParamDef p{k, l, GenParamKind::Text, 0, 0, {0, 0, 0, 0}, "", {}, d}; return p; }
}

inline const std::vector<GenTypeInfo>& genTypes() {
    using namespace gendefs_;
    static const std::vector<GenTypeInfo> types = {
        {GenType::TestCard, "testcard", "Test Card", true, {
            i("columns", "Columns", 2, 64, 16), i("rows", "Rows", 2, 36, 9),
            t("label", "Label", "MAPHAPPY"),
            b("showResolution", "Show resolution", true),
            b("sweep", "Moving line", true),
            c("accent", "Accent", 1.0f, 0.6f, 0.0f),
        }},
        {GenType::Grid, "grid", "Grid", true, {
            i("columns", "Columns", 1, 128, 16), i("rows", "Rows", 1, 128, 9),
            f("thickness", "Line width", 1, 64, 3, "%.0f px"),
            c("lineColor", "Lines", 1, 1, 1), c("background", "Background", 0, 0, 0),
            f("scrollX", "Scroll X", -8, 8, 0, "%.2f cells/s"), f("scrollY", "Scroll Y", -8, 8, 0, "%.2f cells/s"),
        }},
        {GenType::Text, "text", "Text", true, {
            t("text", "Text", "HELLO"),
            ch("font", "Font", {{"sans", "Sans"}, {"pixel", "Pixel"}}),
            ch("align", "Align", {{"left", "Left"}, {"center", "Centre"}, {"right", "Right"}}, 1),
            f("size", "Size", 0.02f, 1.0f, 0.25f, "%.2f of height"),
            f("posY", "Height on picture", 0, 1, 0.5f, "%.2f"),
            c("color", "Colour", 1, 1, 1), c("background", "Background", 0, 0, 0),
            f("scroll", "Scroll", -2, 2, 0, "%.2f widths/s"),
        }},
        {GenType::Solid, "solid", "Solid Colour", false, {
            c("color", "Colour", 1.0f, 0.5f, 0.0f),
        }},
        {GenType::Gradient, "gradient", "Gradient", true, {
            c("colorA", "From", 0.05f, 0.1f, 0.6f), c("colorB", "To", 1.0f, 0.3f, 0.5f),
            ch("style", "Style", {{"linear", "Linear"}, {"radial", "Radial"}}),
            f("angle", "Angle", -360, 360, 0, "%.0f deg"),
            f("spin", "Spin", -360, 360, 0, "%.0f deg/s"),
            i("repeat", "Repeat", 1, 16, 1),
            f("scroll", "Scroll", -4, 4, 0, "%.2f /s"),
        }},
        {GenType::Strobe, "strobe", "Strobe", true, {
            f("rate", "Rate", 0.1f, 30.0f, 4.0f, "%.2f Hz"),
            f("duty", "On time", 0.01f, 1.0f, 0.25f, "%.2f"),
            f("fade", "Fade out", 0.0f, 1.0f, 0.0f, "%.2f"),
            c("onColor", "Flash", 1, 1, 1), c("offColor", "Off", 0, 0, 0),
        }},
        {GenType::Lines, "lines", "Lines", true, {
            i("count", "Count", 1, 200, 12),
            f("thickness", "Thickness", 0.01f, 0.99f, 0.3f, "%.2f"),
            f("softness", "Softness", 0, 1, 0, "%.2f"),
            f("angle", "Angle", -180, 180, 0, "%.0f deg"),
            f("speed", "Speed", -10, 10, 0.5f, "%.2f lines/s"),
            c("color", "Lines", 1, 1, 1), c("background", "Background", 0, 0, 0),
        }},
    };
    return types;
}

inline const GenTypeInfo& genTypeInfo(GenType t) {
    for (const GenTypeInfo& g : genTypes()) if (g.type == t) return g;
    return genTypes().front();
}
inline const GenTypeInfo* genTypeFromKey(const std::string& key) {
    for (const GenTypeInfo& g : genTypes()) if (key == g.key) return &g;
    return nullptr;
}
inline const GenParamDef* findGenParam(GenType t, const std::string& key) {
    for (const GenParamDef& d : genTypeInfo(t).params) if (key == d.key) return &d;
    return nullptr;
}

constexpr int kGenMinSize = 16, kGenMaxSize = 8192;

// A generator's settings: its type, picture size and parameter values.
struct GenSettings {
    GenType type = GenType::Solid;
    int width = 1920, height = 1080;
    std::map<std::string, GenColor> values;        // every non-text parameter (scalars use [0])
    std::map<std::string, std::string> texts;      // Text parameters

    static GenSettings defaults(GenType t) {
        GenSettings s; s.type = t;
        for (const GenParamDef& d : genTypeInfo(t).params) {
            if (d.kind == GenParamKind::Text) s.texts[d.key] = d.textDef;
            else s.values[d.key] = d.def;
        }
        return s;
    }

    float num(const std::string& key) const {
        auto it = values.find(key);
        if (it != values.end()) return it->second[0];
        const GenParamDef* d = findGenParam(type, key);
        return d ? d->def[0] : 0.0f;
    }
    GenColor color(const std::string& key) const {
        auto it = values.find(key);
        if (it != values.end()) return it->second;
        const GenParamDef* d = findGenParam(type, key);
        return d ? d->def : GenColor{0, 0, 0, 1};
    }
    std::string text(const std::string& key) const {
        auto it = texts.find(key);
        if (it != texts.end()) return it->second;
        const GenParamDef* d = findGenParam(type, key);
        return d ? d->textDef : "";
    }

    // Values are kept in range (ints rounded, bools 0/1, colours 0..1), so a typo in a file can't break a shader.
    static float clampScalar(const GenParamDef& d, float v) {
        if (!std::isfinite(v)) v = d.def[0];
        v = std::max(d.min, std::min(d.max, v));
        if (d.kind == GenParamKind::Int || d.kind == GenParamKind::Choice) v = std::round(v);
        if (d.kind == GenParamKind::Bool) v = v >= 0.5f ? 1.0f : 0.0f;
        return v;
    }
    bool setNum(const std::string& key, float v) {
        const GenParamDef* d = findGenParam(type, key);
        if (!d || d->kind == GenParamKind::Text || d->kind == GenParamKind::Color) return false;
        values[key] = {clampScalar(*d, v), 0, 0, 0};
        return true;
    }
    bool setColor(const std::string& key, GenColor c) {
        const GenParamDef* d = findGenParam(type, key);
        if (!d || d->kind != GenParamKind::Color) return false;
        for (float& x : c) x = std::isfinite(x) ? std::max(0.0f, std::min(1.0f, x)) : 0.0f;
        values[key] = c;
        return true;
    }
    bool setText(const std::string& key, const std::string& v) {
        const GenParamDef* d = findGenParam(type, key);
        if (!d || d->kind != GenParamKind::Text) return false;
        texts[key] = v.size() > 2000 ? v.substr(0, 2000) : v;
        return true;
    }
    void setSize(int w, int h) { width = std::max(kGenMinSize, std::min(kGenMaxSize, w)); height = std::max(kGenMinSize, std::min(kGenMaxSize, h)); }
    bool operator==(const GenSettings& o) const { return type == o.type && width == o.width && height == o.height && values == o.values && texts == o.texts; }
};

// ---- strobe timing, shared by the shader (through uPhase) and the tests
// phase = position in the current flash cycle (0..1). Returns the flash level 0..1.
inline float strobeLevel(double phase, float duty, float fade) {
    if (phase < duty) return 1.0f;
    if (fade <= 0.0f) return 0.0f;
    return (float)std::max(0.0, 1.0 - (phase - duty) / fade);
}
inline double strobePhase(double time, float rate) { double x = time * rate; return x - std::floor(x); }

// ---- project file
inline nlohmann::json genToJson(const GenSettings& s) {
    nlohmann::json o;
    const GenTypeInfo& info = genTypeInfo(s.type);
    o["type"] = info.key;
    o["width"] = s.width; o["height"] = s.height;
    nlohmann::json p = nlohmann::json::object();
    for (const GenParamDef& d : info.params) {
        switch (d.kind) {
            case GenParamKind::Text:   p[d.key] = s.text(d.key); break;
            case GenParamKind::Color:  { GenColor c = s.color(d.key); p[d.key] = {c[0], c[1], c[2], c[3]}; } break;
            case GenParamKind::Bool:   p[d.key] = s.num(d.key) >= 0.5f; break;
            case GenParamKind::Int:    p[d.key] = (int)std::lround(s.num(d.key)); break;
            case GenParamKind::Choice: { int i = std::max(0, std::min((int)d.choices.size() - 1, (int)std::lround(s.num(d.key)))); p[d.key] = d.choices[i].key; } break;
            default:                   p[d.key] = s.num(d.key);
        }
    }
    o["params"] = p;
    return o;
}

// Reads a generator written by genToJson. Missing or broken values fall back to the defaults (noted in `warnings`).
// Returns false only when the type is unknown.
inline bool genFromJson(const nlohmann::json& o, GenSettings& out, std::vector<std::string>* warnings = nullptr) {
    if (!o.is_object()) return false;
    auto ti = o.find("type");
    const GenTypeInfo* info = (ti != o.end() && ti->is_string()) ? genTypeFromKey(ti->get<std::string>()) : nullptr;
    if (!info) return false;
    out = GenSettings::defaults(info->type);
    auto num = [&](const char* k, int def) { auto it = o.find(k); return (it != o.end() && it->is_number()) ? it->get<int>() : def; };
    out.setSize(num("width", 1920), num("height", 1080));
    auto pit = o.find("params");
    if (pit == o.end() || !pit->is_object()) return true;
    const nlohmann::json& p = *pit;
    bool fixed = false;
    for (const GenParamDef& d : info->params) {
        auto it = p.find(d.key);
        if (it == p.end()) continue;                       // an older file: keep the default
        const nlohmann::json& v = *it;
        bool ok = true;
        switch (d.kind) {
            case GenParamKind::Text:   ok = v.is_string() && out.setText(d.key, v.get<std::string>()); break;
            case GenParamKind::Bool:   ok = v.is_boolean() && out.setNum(d.key, v.get<bool>() ? 1.0f : 0.0f); break;
            case GenParamKind::Color:
                ok = v.is_array() && (v.size() == 3 || v.size() == 4) && std::all_of(v.begin(), v.end(), [](const nlohmann::json& x) { return x.is_number(); });
                if (ok) out.setColor(d.key, {v[0].get<float>(), v[1].get<float>(), v[2].get<float>(), v.size() == 4 ? v[3].get<float>() : 1.0f});
                break;
            case GenParamKind::Choice: {
                ok = false;
                if (v.is_string()) for (size_t i = 0; i < d.choices.size(); i++) if (v.get<std::string>() == d.choices[i].key) { out.setNum(d.key, (float)i); ok = true; }
            } break;
            default:
                ok = v.is_number();
                if (ok) { const float x = v.get<float>(); out.setNum(d.key, x); if (out.num(d.key) != x) fixed = true; }
        }
        if (!ok) fixed = true;
    }
    if (fixed && warnings) warnings->push_back(std::string("A ") + info->label + " generator had values out of range; they were corrected");
    return true;
}
