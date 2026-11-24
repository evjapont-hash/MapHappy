// The media generators, checked by reading back the pixels they draw, plus how they live in the media pool
// and in project files.
#include "GlTestSupport.h"
#include "GeneratorSource.h"
#include "Params.h"
#include "ProjectIO.h"
#include "Renderer.h"
using json = nlohmann::json;

// The generator's whole picture, read back from its texture. at(x, y) counts y from the TOP.
struct Pic {
    int w = 0, h = 0; std::vector<unsigned char> px;
    explicit Pic(const ContentSource& s) : w(s.width()), h(s.height()), px((size_t)w * h * 4) {
        glBindTexture(GL_TEXTURE_2D, s.texture());
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    }
    Px at(int x, int y) const { const unsigned char* p = &px[((size_t)(h - 1 - y) * w + x) * 4]; return Px{p[0], p[1], p[2]}; }
    int inkInRows(int y0, int y1, int minR = 200) const { int n = 0; for (int y = y0; y < y1; y++) for (int x = 0; x < w; x++) n += at(x, y).r >= minR; return n; }
    int inkIn(int x0, int x1, int y0, int y1, int minR = 200) const { int n = 0; for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) n += at(x, y).r >= minR; return n; }
    int whiteIn(int x0, int x1, int y0, int y1) const { int n = 0; for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) { Px v = at(x, y); n += v.r >= 230 && v.g >= 230 && v.b >= 230; } return n; }
    double inkCentreX(int minR = 200) const { double sx = 0; int n = 0; for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) if (at(x, y).r >= minR) { sx += x; n++; } return n ? sx / n : -1; }
};

static GenSettings make(GenType t, int w, int h) { GenSettings s = GenSettings::defaults(t); s.setSize(w, h); return s; }

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);       // every line reaches the log at once, even if a driver hangs later
    GlContext gl; if (!gl.ok) return skipTest("no OpenGL 3.3 context available");

    { GenSettings s = make(GenType::Solid, 64, 36); s.setColor("color", {1.0f, 0.5f, 0.0f, 1.0f});
      GeneratorSource g(s); Pic p(g);
      expect("solid colour fills the whole picture", isColor(p.at(0, 0), 255, 128, 0, 1) && isColor(p.at(63, 35), 255, 128, 0, 1) && isColor(p.at(30, 20), 255, 128, 0, 1));
      expect("...at the size it was given", g.width() == 64 && g.height() == 36);
      g.settings.setColor("color", {0, 0, 1, 1}); g.update(0.0);
      expect("editing a parameter changes the picture on the next update", isColor(Pic(g).at(10, 10), 0, 0, 255, 1)); }

    { GenSettings s = make(GenType::Grid, 200, 100); s.setNum("columns", 4); s.setNum("rows", 2); s.setNum("thickness", 4);
      GeneratorSource g(s); Pic p(g);
      expect("grid: a vertical line every quarter of the width", isColor(p.at(50, 25), 255, 255, 255, 2) && isColor(p.at(100, 25), 255, 255, 255, 2) && isColor(p.at(150, 75), 255, 255, 255, 2));
      expect("grid: a horizontal line halfway down", isColor(p.at(25, 50), 255, 255, 255, 2));
      expect("grid: the cells are background", isColor(p.at(25, 25), 0, 0, 0, 2) && isColor(p.at(75, 75), 0, 0, 0, 2));
      expect("grid: lines are as wide as asked (4 px)", isColor(p.at(48, 25), 255, 255, 255, 2) && isColor(p.at(51, 25), 255, 255, 255, 2) && isColor(p.at(46, 25), 0, 0, 0, 2) && isColor(p.at(53, 25), 0, 0, 0, 2));
      g.settings.setNum("scrollX", 1.0f); g.setTime(0.5); Pic q(g);
      expect("grid: scrolling 1 cell/s moves the lines half a cell in half a second", isColor(q.at(25, 25), 255, 255, 255, 2) && isColor(q.at(50, 25), 0, 0, 0, 2)); }

    { GenSettings s = make(GenType::Lines, 200, 100); s.setNum("count", 4); s.setNum("thickness", 0.5f); s.setNum("speed", 0.0f);
      GeneratorSource g(s); Pic p(g);
      expect("lines: stripes every 50 px, half of each spacing thick", isColor(p.at(75, 50), 255, 255, 255, 2) && isColor(p.at(25, 50), 255, 255, 255, 2) && isColor(p.at(100, 50), 0, 0, 0, 2) && isColor(p.at(64, 50), 255, 255, 255, 2) && isColor(p.at(61, 50), 0, 0, 0, 2));
      g.settings.setNum("speed", 1.0f); g.setTime(0.5); Pic q(g);
      expect("lines: moving at 1 line/s, half a second later the gaps are lines", isColor(q.at(100, 50), 255, 255, 255, 2) && isColor(q.at(75, 50), 0, 0, 0, 2));
      g.settings.setNum("speed", 0.0f); g.settings.setNum("angle", 90.0f); g.update(0.0); Pic r(g);
      expect("lines: at 90 degrees they run across", isColor(r.at(10, 25), 255, 255, 255, 2) && isColor(r.at(190, 25), 255, 255, 255, 2) && isColor(r.at(10, 50), 0, 0, 0, 2));
      g.settings.setNum("angle", 0.0f); g.settings.setNum("softness", 1.0f); g.update(0.0); Pic soft(g);
      expect("lines: softness blurs the edges", soft.at(64, 50).r > 20 && soft.at(64, 50).r < 235); }

    { GenSettings s = make(GenType::Gradient, 200, 100); s.setColor("colorA", {0, 0, 0, 1}); s.setColor("colorB", {1, 1, 1, 1});
      GeneratorSource g(s); Pic p(g);
      expect("gradient: from the first colour on the left...", p.at(0, 50).r <= 3);
      expect("...to the second on the right", p.at(199, 50).r >= 252);
      expect("...evenly in between", std::abs(p.at(100, 50).r - 128) <= 3 && std::abs(p.at(50, 50).r - 64) <= 3);
      g.settings.setNum("angle", 180.0f); g.update(0.0); Pic rev(g);
      expect("gradient: 180 degrees reverses it", rev.at(0, 50).r >= 252 && rev.at(199, 50).r <= 3);
      g.settings.setNum("angle", 0.0f); g.settings.setNum("repeat", 2); g.update(0.0); Pic rep(g);
      expect("gradient: repeat 2 mirrors it (dark, light, dark)", rep.at(0, 50).r <= 3 && rep.at(100, 50).r >= 250 && rep.at(199, 50).r <= 6);
      g.settings.setNum("repeat", 1); g.settings.setNum("style", 1); g.update(0.0); Pic rad(g);
      expect("gradient: radial is dark in the middle and light in the corners", rad.at(100, 50).r <= 5 && rad.at(0, 0).r >= 245 && rad.at(199, 99).r >= 245);
      g.settings.setNum("style", 0); g.settings.setNum("spin", 90.0f); g.setTime(2.0); Pic spun(g);
      expect("gradient: spinning 90 deg/s, after 2 s it has turned around", spun.at(0, 50).r >= 250 && spun.at(199, 50).r <= 5); }

    { GenSettings s = make(GenType::Strobe, 32, 32); s.setNum("rate", 2.0f); s.setNum("duty", 0.25f);
      GeneratorSource g(s);
      g.setTime(0.0);  expect("strobe: flashes at the start of a cycle", isColor(Pic(g).at(16, 16), 255, 255, 255, 1));
      g.setTime(0.2);  expect("strobe: dark after its on time", isColor(Pic(g).at(16, 16), 0, 0, 0, 1));
      g.setTime(0.5);  expect("strobe: flashes again one period later (2 Hz)", isColor(Pic(g).at(16, 16), 255, 255, 255, 1));
      g.settings.setNum("fade", 0.5f); g.setTime(0.2);
      const int expected = (int)std::lround(255.0 * strobeLevel(strobePhase(0.2, 2.0f), 0.25f, 0.5f));
      expect("strobe: with fade it dims exactly as strobeLevel() says", std::abs(Pic(g).at(16, 16).r - expected) <= 2 && expected > 150 && expected < 200);
      g.settings.setNum("fade", 0.0f);
      g.setTime(0.0); g.update(0.2); expect("strobe: update() advances its own clock", nearf(g.time(), 0.2, 1e-9) && isColor(Pic(g).at(16, 16), 0, 0, 0, 1)); }

    { GenSettings s = make(GenType::Text, 400, 200); s.setText("text", "MMMM"); s.setNum("font", 1); s.setNum("size", 0.3f);
      GeneratorSource g(s); Pic p(g);
      expect("text: the letters are drawn in the middle of the picture", p.inkInRows(70, 130) > 1000 && p.inkInRows(0, 60) == 0 && p.inkInRows(140, 200) == 0);
      expect("text: centred left to right", std::abs(p.inkCentreX() - 200) < 6);
      g.settings.setNum("posY", 0.25f); g.update(0.0); Pic up(g);
      expect("text: 'height on picture' moves it up", up.inkInRows(0, 100) > 1000 && up.inkInRows(100, 200) == 0);
      g.settings.setNum("posY", 0.5f); g.settings.setNum("align", 0); g.update(0.0);
      expect("text: left alignment", Pic(g).inkCentreX() < 160);
      g.settings.setNum("align", 1); g.settings.setNum("scroll", 0.25f); g.setTime(2.0); const double x0 = Pic(g).inkCentreX(); g.setTime(2.5); const double x1 = Pic(g).inkCentreX();
      expect("text: scrolling moves it left like a ticker", x0 > 0 && x1 > 0 && x0 - x1 > 40 && x0 - x1 < 60);
      g.settings.setNum("scroll", 0.0f); g.settings.setText("text", ""); g.update(0.0);
      expect("text: no text, no ink", Pic(g).inkInRows(0, 200, 30) == 0);
      g.settings.setText("text", "A\nB"); g.settings.setNum("size", 0.3f); g.update(0.0); Pic two(g);
      expect("text: two lines are stacked around the middle", two.inkInRows(40, 98) > 100 && two.inkInRows(102, 160) > 100 && two.inkInRows(0, 38) == 0); }

    { GeneratorSource g(make(GenType::TestCard, 320, 180)); Pic p(g);
      expect("test card: red top-left, green top-right, blue bottom-left, white bottom-right", isColor(p.at(6, 6), 255, 0, 0, 2) && isColor(p.at(313, 6), 0, 255, 0, 2) && isColor(p.at(6, 173), 0, 102, 255, 2) && isColor(p.at(313, 173), 255, 255, 255, 2));
      expect("test card: colour bars in the lower band", isColor(p.at(10, 122), 255, 255, 255, 2) && isColor(p.at(50, 122), 255, 255, 0, 2) && isColor(p.at(290, 122), 0, 0, 0, 2));
      expect("test card: the label and resolution are printed", g.currentText() == "MAPHAPPY\n320 x 180" && p.whiteIn(40, 280, 28, 52) > 40);
      g.settings.setText("label", ""); g.settings.setNum("showResolution", 0); g.update(0.0);
      expect("test card: with no label nothing is printed", g.currentText().empty() && Pic(g).whiteIn(40, 280, 28, 52) == 0);
      g.settings.setSize(160, 90);
      expect("a new size waits for applyResize() (between frames)", g.wantsResize() && g.width() == 320);
      g.applyResize();
      expect("...then the texture has the new size", !g.wantsResize() && g.width() == 160 && g.height() == 90 && isColor(Pic(g).at(3, 3), 255, 0, 0, 2)); }

    // ---- in the media pool, on a surface, and in project files
    { Renderer renderer; Project p; RenderTarget rt = makeRenderTarget(200, 100);
      GenSettings solid = make(GenType::Solid, 32, 32); solid.setColor("color", {0, 1, 0, 1});
      const int a = p.media.addGenerator(solid);
      const int b = p.media.addGenerator(GenType::Strobe);
      const int c = p.media.addGenerator(GenType::Strobe);
      expect("generators get ids and names in the pool", a == 2 && b == 3 && c == 4 && p.media.find(b)->name == "Strobe" && p.media.find(c)->name == "Strobe 2" && p.media.find(a)->generator());
      expect("the built-in test pattern is not a configurable generator", p.media.find(MediaPool::kPatternId)->generator() == nullptr);
      Surface& s = p.addSurface(Shape::Quad, a);
      for (int i = 0; i < 4; i++) s.out[i] = Vec2{(i == 1 || i == 2) ? 1.0f : 0.0f, i >= 2 ? 1.0f : 0.0f};
      renderer.renderStage(rt, p);
      unsigned char d[4]; glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo); glReadPixels(100, 50, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, d); glBindFramebuffer(GL_FRAMEBUFFER, 0);
      expect("a surface shows a generator's picture on the stage", d[0] < 3 && d[1] > 252 && d[2] < 3);
      p.media.find(b)->playing = false; p.media.update(0.3);
      expect("a paused generator's clock doesn't move", p.media.find(b)->generator()->time() == 0.0 && nearf(p.media.find(c)->generator()->time(), 0.3, 1e-9));
      p.media.find(c)->speed = 2.5f; p.media.update(0.2);
      expect("Speed 2.5x runs a generator's animation two and a half times as fast", nearf(p.media.find(c)->generator()->time(), 0.8, 1e-9));
      p.media.find(c)->speed = 0.0f; p.media.update(0.5);
      expect("...and Speed 0 freezes it", nearf(p.media.find(c)->generator()->time(), 0.8, 1e-9));
      p.media.find(c)->speed = 1.75f;

      GeneratorSource* g = p.media.find(c)->generator();
      g->settings.setNum("rate", 7.5f); g->settings.setColor("onColor", {1, 0, 0, 0.5f}); g->settings.setSize(640, 360);
      p.media.applyResizes();
      p.media.find(b)->name = "My strobe";
      const int t = p.media.addGenerator(GenType::Text);
      p.media.find(t)->generator()->settings.setText("text", "Caf\xC3\xA9\nline 2");
      p.addSurface(Shape::Circle, t);
      const std::string text = projectToJson(p, "");
      Project q; LoadReport rep;
      const bool loaded = loadProjectFromJson(q, text, "", rep);
      for (const std::string& w : rep.warnings) std::printf("  load warning: %s\n", w.c_str());
      if (!rep.error.empty()) std::printf("  load error: %s\n", rep.error.c_str());
      expect("a project with generators loads without warnings", loaded && rep.warnings.empty());
      expect("...and saves back to exactly the same file", projectToJson(q, "") == text);
      const MediaItem* qc = q.media.find(c);
      expect("...with every generator setting, id, name and order kept", qc && qc->generator() && qc->generator()->settings == g->settings && qc->generator()->width() == 640
             && q.media.find(b)->name == "My strobe" && !q.media.find(b)->playing && q.media.items.size() == 5 && q.media.items[3]->id == c);
      expect("...including its speed", nearf(q.media.find(c)->speed, 1.75f, 1e-6) && nearf(q.media.find(a)->speed, 1.0f, 1e-6));
      expect("...and surfaces still use them", q.surfaces.size() == 2 && q.surfaces[0].mediaId == t && q.surfaces[1].mediaId == a);

      json j = json::parse(text);
      for (auto& m : j["media"]) if (m["id"] == t) m["type"] = "hologram";
      LoadReport rep2; Project r;
      expect("an unknown generator type is skipped with a warning", loadProjectFromJson(r, j.dump(), "", rep2) && !r.media.find(t) && rep2.warnings.size() >= 2);
      expect("...and the surface that used it falls back to the test pattern", r.surfaces[0].mediaId == MediaPool::kPatternId);
      destroyRenderTarget(rt); }

    expect("no OpenGL errors anywhere in this test", glGetError() == GL_NO_ERROR);
    return finishTest();
}
