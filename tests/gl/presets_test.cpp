// Live presets: storing a look, recalling it (cut and cross-fade), and saving presets with the project.
#include "GlTestSupport.h"
#include "Presets.h"
#include "ProjectIO.h"
#include "Renderer.h"
using json = nlohmann::json;

// the stored values of one surface in a preset
static PresetSurface& of(Preset& pr, int id) { for (auto& ps : pr.surfaces) if (ps.id == id) return ps; static PresetSurface none; return none; }
static const PresetSurface& of(const Preset& pr, int id) { return of(const_cast<Preset&>(pr), id); }

static void setQuad(Vec2* q, float x0, float y0, float x1, float y1) { q[0] = {x0, y0}; q[1] = {x1, y0}; q[2] = {x1, y1}; q[3] = {x0, y1}; }

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    GlContext gl; if (!gl.ok) return skipTest("no OpenGL 3.3 context available");
    Project p; Renderer renderer;
    p.outW = 200; p.outH = 200;
    RenderTarget rt = makeRenderTarget(200, 200);
    auto px = [&](int x, int y) { unsigned char d[4]; glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo); glReadPixels(x, 199 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, d); glBindFramebuffer(GL_FRAMEBUFFER, 0); return Px{d[0], d[1], d[2]}; };
    const int red = p.media.addGenerator([] { GenSettings s = GenSettings::defaults(GenType::Solid); s.setSize(8, 8); s.setColor("color", {1, 0, 0, 1}); return s; }());
    const int green = p.media.addGenerator([] { GenSettings s = GenSettings::defaults(GenType::Solid); s.setSize(8, 8); s.setColor("color", {0, 1, 0, 1}); return s; }());
    const int a = p.addSurface(Shape::Quad, red).id;
    setQuad(p.find(a)->out, 0, 0, 1, 1);
    const int l = p.addSurface(Shape::Line, red).id;
    const int folder = p.addFolder("F");
    p.moveSurfaceToFolder(l, folder);
    auto A = [&]() -> Surface& { return *p.find(a); };

    expect("keys and slots: 1..9 are slots 0..8, and 0 is slot 9", presetSlotForDigit(1) == 0 && presetSlotForDigit(0) == 9 && presetKeyDigit(9) == 0 && presetKeyDigit(4) == 5);

    // ---- store
    A().opacity = 0.8f; A().hue = 40.0f; A().posX = 0.1f; p.find(l)->lineWidth = 0.05f;
    const Preset look1 = capturePreset(p, "Look 1", 0.0f);
    expect("a preset holds every surface's parameters and media", look1.used && look1.surfaces.size() == 2 && of(look1, a).values.at("opacity") == 0.8f && of(look1, a).mediaId == red);
    expect("...only the parameters each surface has (line width for the line only)", of(look1, a).values.count("lineWidth") == 0 && of(look1, l).values.at("lineWidth") == 0.05f);
    expect("...and the folders' visibility", look1.folders.size() == 1 && look1.folders[0].first == folder && look1.folders[0].second);

    // ---- cut
    A().opacity = 0.2f; A().hue = -90.0f; A().posX = -0.3f; A().mediaId = green; p.find(l)->lineWidth = 0.2f; p.findFolder(folder)->visible = false;
    PresetPlayer player;
    player.start(p, look1, 0);
    expect("a preset with no fade is a cut: everything is back at once", nearf(A().opacity, 0.8, 1e-6) && nearf(A().hue, 40, 1e-6) && nearf(A().posX, 0.1, 1e-6) && A().mediaId == red
           && nearf(p.find(l)->lineWidth, 0.05, 1e-6) && p.findFolder(folder)->visible && !player.fading() && player.activeSlot() == 0);
    renderer.renderStage(rt, p);
    expect("...and it shows on the stage (red, hue turned by 40 degrees)", px(100, 100).r > 150 && px(100, 100).g > 20);

    // ---- cross-fade
    Preset look2 = capturePreset(p, "Look 2", 0.5f);
    for (auto& ps : look2.surfaces) if (ps.id == a) { ps.values["opacity"] = 0.0f; ps.values["hue"] = 140.0f; ps.mediaId = green; }
    player.start(p, look2, 1);
    expect("a cross-fade starts where things are", nearf(A().opacity, 0.8, 1e-6) && player.fading() && player.activeSlot() == 1);
    expect("...the picture switches at once", A().mediaId == green);
    player.update(p, 0.25);
    expect("halfway through the fade, values are halfway (eased)", nearf(A().opacity, 0.4, 1e-5) && nearf(A().hue, 90, 1e-4) && nearf(player.progress(), 0.5, 1e-6));
    player.update(p, 0.1);
    { const double k = 0.7, e = k * k * (3 - 2 * k); expect("...and the easing is smooth (smoothstep)", nearf(A().opacity, 0.8 - 0.8 * e, 1e-5)); }
    player.update(p, 1.0);
    expect("at the end the preset's values are reached exactly, and the fade stops", nearf(A().opacity, 0.0, 1e-6) && nearf(A().hue, 140, 1e-6) && !player.fading());

    // ---- switches: on at the start, off at the end
    A().opacity = 1.0f; A().visible = false;
    Preset on = capturePreset(p, "on", 1.0f); for (auto& ps : on.surfaces) if (ps.id == a) ps.values["visible"] = 1.0f;
    player.start(p, on, 2);
    expect("a surface switched on by a preset appears at the start of the fade", A().visible);
    Preset off = capturePreset(p, "off", 1.0f); for (auto& ps : off.surfaces) if (ps.id == a) { ps.values["visible"] = 0.0f; ps.values["opacity"] = 0.0f; }
    off.folders = {{folder, false}};
    player.start(p, off, 3); player.update(p, 0.5);
    expect("one switched off stays visible while it fades, and so does a folder", A().visible && p.findFolder(folder)->visible && A().opacity > 0.1f && A().opacity < 0.9f);
    player.update(p, 0.6);
    expect("...and goes off at the end", !A().visible && !p.findFolder(folder)->visible);
    A().visible = true; p.findFolder(folder)->visible = true;

    // ---- interrupting a fade
    A().opacity = 0.0f;
    Preset full = capturePreset(p, "full", 1.0f); for (auto& ps : full.surfaces) if (ps.id == a) ps.values["opacity"] = 1.0f;
    Preset none = capturePreset(p, "none", 1.0f);                         // opacity 0
    player.start(p, full, 4); player.update(p, 0.5);
    const float mid = A().opacity;
    player.start(p, none, 5);
    expect("a new preset during a fade carries on from the in-between values (no jump)", nearf(A().opacity, mid, 1e-6) && mid > 0.4f && mid < 0.6f);
    player.update(p, 2.0);
    expect("...and reaches its own values", nearf(A().opacity, 0.0, 1e-6));

    // ---- robustness
    { Preset odd = look1;
      of(odd, a).values["opacity"] = 7.0f; of(odd, a).values["rotation"] = NAN; of(odd, a).values["nonsense"] = 3.0f;
      of(odd, a).mediaId = 999;
      odd.surfaces.push_back(PresetSurface{777, red, {{"opacity", 0.5f}}});
      const int added = p.addSurface(Shape::Circle, green).id; p.find(added)->opacity = 0.33f;
      A().rotation = 12.0f;
      player.start(p, odd, 6);
      expect("values out of range are kept in range, broken ones and unknown names ignored", nearf(A().opacity, 1.0, 1e-6) && nearf(A().rotation, 12.0, 1e-6));
      expect("a deleted picture leaves the surface's picture alone; a deleted surface is skipped", A().mediaId == green && !p.find(777));
      expect("a surface added after the preset was stored is left alone", nearf(p.find(added)->opacity, 0.33, 1e-6));
      p.removeSurface(added); }

    // ---- saving with the project
    { p.presets[0] = look1; p.presets[9] = capturePreset(p, "Zero \xC3\xA9", 2.5f);
      p.syncLines();                                                       // (the editor keeps line quads in step every frame)
      const std::string text = projectToJson(p, "");
      const json j = json::parse(text);
      expect("only the slots in use are saved, with the key that plays them", j["presets"].size() == 2 && j["presets"][1]["key"] == 0 && j["presets"][1]["slot"] == 9);
      Project q; LoadReport rep;
      const bool loaded = loadProjectFromJson(q, text, "", rep);
      for (auto& w : rep.warnings) std::printf("  load warning: %s\n", w.c_str());
      if (projectToJson(q, "") != text) { const std::string t2 = projectToJson(q, ""); size_t i = 0; while (i < t2.size() && i < text.size() && t2[i] == text[i]) i++; std::printf("  first difference at %zu: ...%s...\n", i, text.substr(i > 80 ? i - 80 : 0, 160).c_str()); }
      expect("presets load back without warnings, exactly", loaded && rep.warnings.empty() && projectToJson(q, "") == text);
      expect("...with their names, fades and values", q.presets[9].used && q.presets[9].name == "Zero \xC3\xA9" && nearf(q.presets[9].fade, 2.5, 1e-6) && !q.presets[4].used
             && q.presets[0].surfaces.size() == 2 && nearf(of(q.presets[0], a).values.at("hue"), 40, 1e-6) && q.presets[0].folders.size() == 1);
      json bad = json::parse(text);
      bad["presets"].push_back({{"slot", 12}});
      bad["presets"].push_back({{"slot", 0}, {"name", "twin"}});
      bad["presets"][0]["fade"] = -4; bad["presets"][0]["surfaces"][0]["values"]["opacity"] = "lots"; bad["presets"][0]["surfaces"][0]["values"]["futureParam"] = 1;
      Project r; LoadReport rep2;
      expect("damaged presets: bad and repeated slots are skipped with warnings, bad values dropped, the fade kept in range",
             loadProjectFromJson(r, bad.dump(), "", rep2) && rep2.warnings.size() == 2 && r.presets[0].name == "Look 1" && r.presets[0].fade == 0.0f
             && r.presets[0].surfaces[0].values.count("opacity") == 0 && r.presets[0].surfaces[0].values.count("futureParam") == 0);
      p.resetToEmpty();
      expect("a new project has no presets", !p.presets[0].used && !p.presets[9].used); }

    expect("no OpenGL errors anywhere in this test", glGetError() == GL_NO_ERROR);
    destroyRenderTarget(rt);
    return finishTest();
}
