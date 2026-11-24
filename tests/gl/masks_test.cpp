// Masks: what they cut away, checked on real stage pixels, and how they are saved.
#include "GlTestSupport.h"
#include "ProjectIO.h"
#include "Renderer.h"
using json = nlohmann::json;

static void setQuad(Vec2* q, float x0, float y0, float x1, float y1) { q[0] = {x0, y0}; q[1] = {x1, y0}; q[2] = {x1, y1}; q[3] = {x0, y1}; }
static Mask box(MaskShape shape, float x0, float y0, float x1, float y1, MaskMode mode = MaskMode::Hide) { Mask m = makeMask(shape, "m"); m.min = {x0, y0}; m.max = {x1, y1}; m.mode = mode; return m; }

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    GlContext gl; if (!gl.ok) return skipTest("no OpenGL 3.3 context available");
    Renderer renderer; Project p;
    p.outW = 400; p.outH = 400;
    RenderTarget rt = makeRenderTarget(400, 400);
    const int solid = p.media.addGenerator([] { GenSettings s = GenSettings::defaults(GenType::Solid); s.setSize(16, 16); return s; }());
    auto px = [&](int x, int y) { unsigned char d[4]; glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo); glReadPixels(x, 399 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, d); glBindFramebuffer(GL_FRAMEBUFFER, 0); return Px{d[0], d[1], d[2]}; };
    auto on = [&](int x, int y) { return isColor(px(x, y), 255, 128, 0, 3); };
    auto off = [&](int x, int y) { return isColor(px(x, y), 0, 0, 0, 3); };
    const int id = p.addSurface(Shape::Quad, solid).id;
    auto S = [&]() -> Surface& { return *p.find(id); };
    setQuad(S().out, 0.25f, 0.25f, 0.75f, 0.75f); setQuad(S().in, 0, 0, 1, 1);       // the surface covers pixels 100..300
    auto draw = [&] { renderer.renderStage(rt, p); };

    draw(); expect("without masks the whole surface shows", on(110, 110) && on(200, 200) && on(290, 290));
    S().masks = {box(MaskShape::Rectangle, 0.25f, 0.25f, 0.75f, 0.75f)}; draw();
    expect("a rectangle mask cuts a hole in the middle", off(200, 200) && off(160, 240) && on(110, 200) && on(290, 290));
    S().masks[0].min.x = 0.2525f; draw();                                    // an edge in the middle of a pixel (x = 150.5)
    { const Px v = px(150, 200); expect("...with an anti-aliased edge (a pixel half covered is half cut)", v.r > 100 && v.r < 160 && on(148, 200) && off(152, 200)); }
    S().masks[0].min.x = 0.25f;
    S().masks[0].mode = MaskMode::ShowOnly; draw();
    expect("'show only what is inside' keeps the middle and cuts the rest", on(200, 200) && off(110, 200) && off(290, 290));
    S().masks[0].enabled = false; draw();
    expect("a switched-off mask does nothing", on(110, 200) && on(200, 200));
    S().masks[0].enabled = true; S().masks[0].mode = MaskMode::Hide; S().masks[0].feather = 0.2f; draw();
    { const Px edge = px(150, 200), mid = px(200, 200), out = px(115, 200);
      expect("feather softens a mask's edge across its width", edge.r > 40 && edge.r < 215 && off(200, 200) && on(110, 200) && mid.r < 5 && out.r > 250); }

    S().masks = {box(MaskShape::Ellipse, 0, 0, 1, 1)}; draw();
    expect("an ellipse mask is round: centre and edge-middles cut, corners kept", off(200, 200) && off(200, 112) && off(112, 200) && on(105, 105) && on(295, 295));
    S().masks = {makeMask(MaskShape::Polygon, "tri")};                    // (0.5, 0.25) (0.75, 0.7) (0.25, 0.7)
    draw();
    expect("a polygon mask cuts its own shape", off(200, 180) && off(160, 230) && on(200, 130) && on(130, 280) && on(270, 160));
    S().masks[0].points.resize(2); draw();
    expect("a polygon with fewer than three points is ignored", on(200, 180));

    S().masks = {box(MaskShape::Rectangle, 0, 0, 0.5f, 1), box(MaskShape::Rectangle, 0.5f, 0, 1, 1)}; draw();
    expect("two masks combine (left half and right half cut: nothing left)", off(150, 200) && off(250, 200));
    S().masks = {box(MaskShape::Rectangle, 0, 0, 1, 0.5f, MaskMode::ShowOnly), box(MaskShape::Rectangle, 0, 0, 0.5f, 1, MaskMode::ShowOnly)}; draw();
    expect("...'show only' masks intersect (only the top-left quarter is left)", on(150, 150) && off(250, 150) && off(150, 250) && off(250, 250));
    { std::vector<Mask> many; for (int i = 0; i < kMaxMasks; i++) many.push_back(box(MaskShape::Rectangle, 0.1f * i, 0.0f, 0.1f * i + 0.05f, 0.1f));
      S().masks = many; draw();
      expect("the most masks a surface can have all apply", off(100 + (kMaxMasks - 1) * 20 + 5, 105) && on(100 + (kMaxMasks - 1) * 20 + 15, 105) && off(105, 105)); }

    // masks belong to the surface: they follow its warp, transform and shape
    S().masks = {box(MaskShape::Rectangle, 0.4f, 0.4f, 0.6f, 0.6f)};
    S().posX = 0.1f; draw();
    expect("moving the surface moves its mask with it", off(240, 200) && on(200, 200));
    S().posX = 0; S().out[1] = {0.95f, 0.25f}; S().out[2] = {0.95f, 0.75f}; draw();
    expect("stretching the quad stretches the mask", off(240, 200) && on(200, 200) && on(330, 200));
    setQuad(S().out, 0.25f, 0.25f, 0.75f, 0.75f);
    S().shape = Shape::Circle; draw();
    expect("a mask works on a circle", off(200, 200) && on(130, 200));
    S().shape = Shape::Object3D; S().rotX = 0; S().rotY = 0; S().masks = {box(MaskShape::Rectangle, 0.45f, 0.45f, 0.55f, 0.55f)}; draw();
    expect("...and on a 3D object", off(200, 200) && on(170, 200));
    S().shape = Shape::Quad;

    // ---- save and load
    { Mask poly = makeMask(MaskShape::Polygon, "Poly \xC3\xA9"); poly.points.push_back({0.1f, 0.2f}); poly.feather = 0.15f;
      S().masks = {box(MaskShape::Ellipse, 0.1f, 0.2f, 0.6f, 0.9f, MaskMode::ShowOnly), poly};
      S().masks[0].enabled = false; S().masks[0].name = "Oval";
      const std::string text = projectToJson(p, "");
      Project q; LoadReport rep;
      expect("masks load back without warnings", loadProjectFromJson(q, text, "", rep) && rep.warnings.empty());
      expect("...exactly as saved", projectToJson(q, "") == text);
      const Surface* qs = q.find(id);
      expect("...with their shapes, modes, switches, boxes, points and feather", qs && qs->masks.size() == 2 && qs->masks[0].name == "Oval" && qs->masks[0].shape == MaskShape::Ellipse
             && qs->masks[0].mode == MaskMode::ShowOnly && !qs->masks[0].enabled && nearf(qs->masks[0].max.y, 0.9f, 1e-6) && qs->masks[1].points.size() == 4 && nearf(qs->masks[1].feather, 0.15f, 1e-6));
      json j = json::parse(text);
      auto& ms = j["surfaces"][0]["masks"];
      ms[0]["min"] = json::array({0.8, 0.9}); ms[0]["max"] = json::array({0.2, 0.1});          // corners the wrong way round
      ms[1]["points"] = json::array({json::array({0, 0})});                                      // too few points
      for (int i = 0; i < 7; i++) ms.push_back({{"shape", "rectangle"}, {"min", {0, 0}}, {"max", {1, 1}}});
      Project r; LoadReport rep2;
      const bool ok = loadProjectFromJson(r, j.dump(), "", rep2);
      for (auto& w : rep2.warnings) std::printf("  load warning: %s\n", w.c_str());
      const Surface* rs = r.find(id);
      expect("damaged masks: swapped corners are fixed, a broken one is skipped, extras beyond the limit dropped (with warnings)",
             ok && rs && (int)rs->masks.size() == kMaxMasks && nearf(rs->masks[0].min.x, 0.2f, 1e-6) && nearf(rs->masks[0].max.y, 0.9f, 1e-6) && rep2.warnings.size() == 2); }

    expect("no OpenGL errors anywhere in this test", glGetError() == GL_NO_ERROR);
    destroyRenderTarget(rt);
    return finishTest();
}
