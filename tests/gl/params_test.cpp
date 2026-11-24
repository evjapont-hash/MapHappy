// The animatable-parameter table: every parameter's key, range, default and formatting, and the timeline driving them.
#include "GlTestSupport.h"
#include "Params.h"
#include <algorithm>
#include <set>

int main() {
    GlContext gl; if (!gl.ok) return skipTest("no OpenGL 3.3 context available");
    const auto& defs = paramDefs();
    const size_t general = (size_t)std::count_if(defs.begin(), defs.end(), [](const ParamDef& d) { return d.applies == nullptr; });
    expect("there are 20 animatable parameters every surface has (3 appearance, 6 transform, 4 content, 7 colour)", general == 20);
    expect("...plus shape-specific ones (a line's width) at the end of the table", defs.size() == 21 && std::string(defs.back().key) == "lineWidth");
    std::set<std::string> keys; bool unique = true, ranges = true, defaultsOk = true, labels = true;
    const Surface fresh;
    for (const ParamDef& d : defs) {
        if (!keys.insert(d.key).second) unique = false;
        if (!(d.min < d.max) || !(d.def >= d.min && d.def <= d.max)) ranges = false;
        if (!nearf(d.get(fresh), d.def)) defaultsOk = false;
        if (!d.label[0] || !d.category[0]) labels = false;
    }
    expect("every key is unique", unique);
    expect("every range is valid and contains its default", ranges);
    expect("every default in the table equals the real default of a new surface", defaultsOk);
    expect("every parameter has a label and a category", labels);

    Project p; Surface& s = p.addSurface(Shape::Quad);
    const int id = s.id;
    bool roundTrip = true;
    for (const ParamDef& d : defs) {
        const std::string path = surfaceParamPath(id, d.key);
        const float mid = d.format == ParamFormat::Bool ? 0.0f : (d.min + d.max) * 0.5f + 0.0123f * (d.max - d.min) * 0.5f;
        float back = -999; ParamInfo pi;
        if (!setParam(p, path, mid) || !getParam(p, path, back) || !nearf(back, mid, 1e-5) || !paramInfo(p, path, pi) || pi.path != path || pi.key != d.key) roundTrip = false;
    }
    expect("every parameter can be set, read back and described by its path", roundTrip);

    float v;
    setParam(p, surfaceParamPath(id, "opacity"), 7.0f);  getParam(p, surfaceParamPath(id, "opacity"), v);  expect("values above the range are clamped", nearf(v, 1.0f));
    setParam(p, surfaceParamPath(id, "opacity"), -3.0f); getParam(p, surfaceParamPath(id, "opacity"), v);  expect("values below the range are clamped", nearf(v, 0.0f));
    setParam(p, surfaceParamPath(id, "rotation"), 5000.0f); getParam(p, surfaceParamPath(id, "rotation"), v); expect("rotation is limited to +/-720 degrees", nearf(v, 720.0f));
    setParam(p, surfaceParamPath(id, "scale"), -2.0f); getParam(p, surfaceParamPath(id, "scale"), v);       expect("scale can't go negative", nearf(v, 0.0f));
    setParam(p, surfaceParamPath(id, "contentScale"), 0.0f); getParam(p, surfaceParamPath(id, "contentScale"), v); expect("zoom can't reach zero", nearf(v, 0.1f));
    setParam(p, surfaceParamPath(id, "visible"), 0.4f); expect("visible below 0.5 means hidden", !p.find(id)->visible);
    setParam(p, surfaceParamPath(id, "visible"), 0.5f); expect("visible at 0.5 or more means shown", p.find(id)->visible);

    ParamInfo pi;
    expect("an unknown parameter is refused", !setParam(p, surfaceParamPath(id, "nonsense"), 1.0f) && !getParam(p, surfaceParamPath(id, "nonsense"), v) && !paramInfo(p, surfaceParamPath(id, "zzz"), pi));
    expect("an unknown surface is refused", !setParam(p, surfaceParamPath(999, "opacity"), 1.0f) && !paramInfo(p, surfaceParamPath(77, "opacity"), pi));
    expect("a malformed path is refused", !setParam(p, "surface/abc", 1.0f) && !setParam(p, "audio/3", 1.0f) && !setParam(p, "", 1.0f));

    ParamInfo a, b, c, d2;
    paramInfo(p, surfaceParamPath(id, "opacity"), a); paramInfo(p, surfaceParamPath(id, "visible"), b); paramInfo(p, surfaceParamPath(id, "rotation"), c); paramInfo(p, surfaceParamPath(id, "hue"), d2);
    expect("formatting: opacity as a percentage, visible as on/off, rotation and hue as degrees", a.percent && !a.degrees && b.isBool && c.degrees && !c.percent && d2.degrees);
    expect("info carries the surface name and the category", a.group == s.name && a.category == "Appearance" && c.category == "Transform");

    p.addSurface(Shape::Circle);
    auto all = listParams(p);
    expect("listing covers every parameter of every surface, front-most surface first", all.size() == 2 * general && all.front().group == p.surfaces.front().name);
    std::set<std::string> cats; for (auto& i : all) cats.insert(i.category);
    expect("there are four categories", cats == std::set<std::string>{"Appearance", "Transform", "Content", "Color"});

    // ---- the timeline drives them (start from a clean surface)
    { Surface clean; clean.id = id; clean.name = p.find(id)->name; *p.find(id) = clean; }
    Timeline& tl = p.timeline;
    const int t1 = tl.addTrack(surfaceParamPath(id, "posX")), c1 = tl.addClip(t1, 0.0, 4.0, 0.0f);
    tl.findTrack(t1)->findClip(c1)->data->keys = {{0, -0.5f, Interp::Linear}, {4.0, 0.5f, Interp::Linear}};
    const int t2 = tl.addTrack(surfaceParamPath(id, "rotation")), c2 = tl.addClip(t2, 0.0, 4.0, 0.0f);
    tl.findTrack(t2)->findClip(c2)->data->keys = {{0, 0.0f, Interp::Linear}, {4.0, 360.0f, Interp::Linear}};
    const int t3 = tl.addTrack(surfaceParamPath(id, "tintG")), c3 = tl.addClip(t3, 0.0, 4.0, 0.0f);
    tl.findTrack(t3)->findClip(c3)->data->keys = {{0, 1.0f, Interp::Hold}, {2.0, 0.0f, Interp::Hold}, {4.0, 1.0f, Interp::Hold}};
    p.transport.time = 1.0; applyTimeline(p);
    expect("the timeline moves a surface (a -0.5..0.5 ramp is at -0.25 after a quarter)", nearf(p.find(id)->posX, -0.25f, 1e-5));
    expect("...spins it (90 degrees a quarter of the way into a 360 ramp)", nearf(p.find(id)->rotation, 90.0f, 1e-3));
    expect("...and drives colour channels independently", nearf(p.find(id)->tintG, 1.0f) && nearf(p.find(id)->tintR, 1.0f));
    p.transport.time = 3.0; applyTimeline(p);
    expect("later in the show the values follow the keyframes", nearf(p.find(id)->posX, 0.25f, 1e-5) && nearf(p.find(id)->rotation, 270.0f, 1e-3) && nearf(p.find(id)->tintG, 0.0f));
    tl.findTrack(t1)->muted = true; p.find(id)->posX = 0.9f; applyTimeline(p);
    expect("a muted track no longer drives its parameter", nearf(p.find(id)->posX, 0.9f));
    p.removeSurface(id);
    expect("deleting a surface removes all of its tracks", tl.tracks.empty());
    return finishTest();
}
