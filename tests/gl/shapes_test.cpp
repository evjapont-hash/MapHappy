// Line and freeform surfaces: what they draw (read back from the stage) and how they are saved.
#include "GlTestSupport.h"
#include "Params.h"
#include "ProjectIO.h"
#include "Renderer.h"
using json = nlohmann::json;

static void setQuad(Vec2* q, float x0, float y0, float x1, float y1) { q[0] = {x0, y0}; q[1] = {x1, y0}; q[2] = {x1, y1}; q[3] = {x0, y1}; }

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    GlContext gl; if (!gl.ok) return skipTest("no OpenGL 3.3 context available");
    Renderer renderer; Project p;
    p.outW = 400; p.outH = 400;
    const int W = 400, H = 400;
    RenderTarget rt = makeRenderTarget(W, H);
    const int orange = p.media.addGenerator([] { GenSettings s = GenSettings::defaults(GenType::Solid); s.setSize(16, 16); return s; }());
    auto px = [&](int x, int y) { unsigned char d[4]; glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo); glReadPixels(x, H - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, d); glBindFramebuffer(GL_FRAMEBUFFER, 0); return Px{d[0], d[1], d[2]}; };
    auto draw = [&] { renderer.renderStage(rt, p); };
    auto isOrange = [](Px v) { return isColor(v, 255, 128, 0, 3); };
    auto isBlack  = [](Px v) { return isColor(v, 0, 0, 0, 3); };

    // ---- line
    { p.surfaces.clear();
      Surface& l = p.addSurface(Shape::Line, orange);
      l.lineA = {0.25f, 0.5f}; l.lineB = {0.75f, 0.5f}; l.lineWidth = 0.1f; p.syncLines(); draw();
      expect("a line is drawn from one end to the other", isOrange(px(110, 200)) && isOrange(px(200, 200)) && isOrange(px(290, 200)));
      expect("...and no further", isBlack(px(90, 200)) && isBlack(px(310, 200)));
      expect("...as wide as its width (10% of the stage = 40 px)", isOrange(px(200, 183)) && isOrange(px(200, 217)) && isBlack(px(200, 176)) && isBlack(px(200, 224)));
      setParam(p, surfaceParamPath(l.id, "lineWidth"), 0.2f); draw();
      expect("Line width is a parameter (so it can be animated)", isOrange(px(200, 165)) && isBlack(px(200, 140)));
      Surface& l2 = p.surfaces[0];
      l2.lineA = {0.5f, 0.1f}; l2.lineB = {0.5f, 0.9f}; l2.lineWidth = 0.05f; draw();
      expect("moving its ends redraws it there (vertical now), even before the quad is synced", isOrange(px(200, 50)) && isOrange(px(200, 350)) && isOrange(px(205, 200)) && isBlack(px(240, 200)) && isBlack(px(200, 20)));
      l2.lineA = {0.2f, 0.2f}; l2.lineB = {0.8f, 0.8f}; draw();
      expect("a diagonal line", isOrange(px(200, 200)) && isOrange(px(120, 120)) && isBlack(px(280, 120)) && isBlack(px(120, 280)));
      l2.rotation = 90.0f; draw();
      expect("a line takes the transform like any surface (rotated 90: the other diagonal)", isOrange(px(280, 120)) && isBlack(px(120, 120))); }

    // ---- freeform
    { p.surfaces.clear();
      Surface& f = p.addSurface(Shape::Freeform, orange);
      setQuad(f.out, 0.25f, 0.25f, 0.75f, 0.75f); setQuad(f.in, 0, 0, 1, 1);
      draw();
      expect("a new freeform surface is a hexagon: filled centre, empty quad corners", isOrange(px(200, 200)) && isBlack(px(105, 105)) && isBlack(px(295, 295)));
      Surface& g = p.surfaces[0];
      g.poly = {{0, 0}, {1, 0}, {1, 1}, {0.5f, 0.5f}, {0, 1}};          // a square with a V cut into its bottom
      draw();
      expect("freeform: a concave outline fills its arms...", isOrange(px(140, 200)) && isOrange(px(260, 200)) && isOrange(px(120, 250)) && isOrange(px(280, 250)));
      expect("...but not its notch", isBlack(px(200, 280)) && isBlack(px(200, 230)));
      int soft = 0;
      for (int y = 205; y < 295; y++) for (int x = 105; x < 200; x++) { Px v = px(x, y); if (v.r > 12 && v.r < 243) soft++; }
      expect("...and its slanted edges are anti-aliased", soft > 60);
      g.poly.push_back({-0.2f, 0.5f});                                  // (the editor and the loader keep points inside; the renderer just copes)
      draw();
      expect("the picture stops at the quad's edge even if a point is outside it", isBlack(px(85, 200)) && isOrange(px(140, 200)));
      g.poly = {{0.1f, 0.1f}, {0.9f, 0.1f}};
      draw();
      expect("a freeform with fewer than three points draws nothing and doesn't crash", isBlack(px(200, 200)));
      g.poly.clear(); for (int i = 0; i < kMaxPolyPoints; i++) { const float a = 6.2831853f * i / kMaxPolyPoints; g.poly.push_back({0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)}); }
      draw();
      expect("the maximum number of points works (a round outline)", isOrange(px(200, 200)) && isOrange(px(290, 200)) && isBlack(px(110, 110)));
      g.poly = {{0, 0}, {1, 0}, {1, 1}, {0, 1}}; g.feather = 1.0f; draw();
      expect("feather fades a freeform's edges like any shape", isOrange(px(200, 200)) && px(104, 200).r < 120); }

    // ---- save and load
    { p.surfaces.clear(); p.outW = 1920; p.outH = 1080;
      Surface& l = p.addSurface(Shape::Line, orange); l.lineA = {0.1f, 0.2f}; l.lineB = {0.6f, 0.9f}; l.lineWidth = 0.03f;
      Surface& f = p.addSurface(Shape::Freeform, orange); f.poly = {{0, 0}, {1, 0.1f}, {0.7f, 0.9f}, {0.2f, 1.0f}};
      p.syncLines();
      const std::string text = projectToJson(p, "");
      Project q; LoadReport rep;
      const bool ok = loadProjectFromJson(q, text, "", rep);
      for (auto& w : rep.warnings) std::printf("  load warning: %s\n", w.c_str());
      expect("lines and freeform surfaces load back with no warnings", ok && rep.warnings.empty() && q.surfaces.size() == 2);
      expect("...exactly as saved", projectToJson(q, "") == text);
      const Surface* ql = q.find(l.id); const Surface* qf = q.find(f.id);
      expect("...with the line's ends and width", ql && ql->shape == Shape::Line && nearf(ql->lineB.y, 0.9f, 1e-6) && nearf(ql->lineWidth, 0.03f, 1e-6));
      expect("...and the freeform points", qf && qf->shape == Shape::Freeform && qf->poly.size() == 4 && nearf(qf->poly[3].x, 0.2f, 1e-6));
      expect("new surfaces are named per shape", ql->name.rfind("Line-", 0) == 0 && qf->name.rfind("Freeform-", 0) == 0);
      { json k = json::parse(text); for (auto& s : k["surfaces"]) if (s["shape"] == "freeform") s["points"][1] = json::array({1.7, -0.4});
        Project c; LoadReport rc; loadProjectFromJson(c, k.dump(), "", rc);
        expect("a freeform point outside its quad is brought back to the quad's edge on loading", nearf(c.find(f.id)->poly[1].x, 1.0f, 1e-6) && nearf(c.find(f.id)->poly[1].y, 0.0f, 1e-6)); }

      json j = json::parse(text);
      for (auto& s : j["surfaces"]) { if (s["shape"] == "line") s["lineB"] = s["lineA"]; if (s["shape"] == "freeform") s["points"] = json::array({json::array({0, 0}), json::array({1, "x"})}); }
      Project r; LoadReport rep2;
      expect("damaged line ends and freeform points are reset, with warnings", loadProjectFromJson(r, j.dump(), "", rep2) && rep2.warnings.size() == 2 && r.find(f.id)->poly.size() == 6 && nearf(r.find(l.id)->lineB.x, Surface().lineB.x, 1e-6));
      j["counters"]["shape"] = json::array({4, 5, 6});
      Project old; LoadReport rep3;
      expect("files from before lines and freeforms existed (3 shape counters) still load", loadProjectFromJson(old, j.dump(), "", rep3) && old.counters().shape[2] >= 6); }

    expect("no OpenGL errors anywhere in this test", glGetError() == GL_NO_ERROR);
    destroyRenderTarget(rt);
    return finishTest();
}
