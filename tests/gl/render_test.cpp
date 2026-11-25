// Pixel-level checks of the renderer: each visual feature is verified by reading real pixels back from the stage.
#include "GlTestSupport.h"
#include "Params.h"
#include "Renderer.h"

// A test picture: left half red, right half blue (so scrolling and zooming are visible)
class SplitSource : public ContentSource {
public:
    SplitSource() {
        std::vector<unsigned char> px(64 * 64 * 4);
        for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) { unsigned char* p = &px[(y * 64 + x) * 4]; p[0] = x < 32 ? 255 : 0; p[1] = 0; p[2] = x < 32 ? 0 : 255; p[3] = 255; }
        glGenTextures(1, &tex_); glBindTexture(GL_TEXTURE_2D, tex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    ~SplitSource() override { glDeleteTextures(1, &tex_); }
    void update(double) override {}
    GLuint texture() const override { return tex_; }
    int width() const override { return 64; }
    int height() const override { return 64; }
private: GLuint tex_ = 0;
};

static int addMedia(Project& p, std::unique_ptr<ContentSource> src) {
    auto m = std::make_unique<MediaItem>(); m->id = p.media.nextId(); p.media.ensureNextIdAbove(m->id);
    m->name = "test"; m->kind = MediaKind::Generator; m->source = std::move(src); const int id = m->id; p.media.items.push_back(std::move(m)); return id;
}
static void setQuad(Vec2* q, float x0, float y0, float x1, float y1) { q[0] = {x0, y0}; q[1] = {x1, y0}; q[2] = {x1, y1}; q[3] = {x0, y1}; }

int main() {
    GlContext gl; if (!gl.ok) return skipTest("no OpenGL 3.3 context available");
    Renderer renderer; Project p;
    const int W = 400, H = 400;
    RenderTarget rt = makeRenderTarget(W, H);
    const int orange = addMedia(p, std::make_unique<SolidSource>(255, 128, 0, 8, 8));
    const int red    = addMedia(p, std::make_unique<SolidSource>(255, 0, 0, 8, 8));
    const int green  = addMedia(p, std::make_unique<SolidSource>(0, 255, 0, 8, 8));
    const int grey   = addMedia(p, std::make_unique<SolidSource>(128, 128, 128, 8, 8));
    const int split  = addMedia(p, std::make_unique<SplitSource>());
    auto px = [&](int x, int y) { unsigned char d[4]; glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo); glReadPixels(x, H - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, d); glBindFramebuffer(GL_FRAMEBUFFER, 0); return Px{d[0], d[1], d[2]}; };   // y from the TOP
    auto clearSurfaces = [&] { p.surfaces.clear(); p.selectedSurfaceId = 0; };
    auto fresh = [&](int media, Shape shape = Shape::Quad) -> Surface& { clearSurfaces(); Surface& s = p.addSurface(shape, media); s.mediaId = media; setQuad(s.out, 0.25f, 0.25f, 0.75f, 0.75f); setQuad(s.in, 0, 0, 1, 1); return p.surfaces[0]; };
    auto draw = [&] { renderer.renderStage(rt, p); };

    { fresh(orange); draw();
      expect("baseline: the quad's centre shows the picture's colour", isColor(px(200, 200), 255, 128, 0, 3));
      expect("baseline: outside the quad is black", isColor(px(50, 50), 0, 0, 0, 3) && isColor(px(380, 380), 0, 0, 0, 3));
      expect("baseline: the quad covers exactly its mapped area", isColor(px(105, 105), 255, 128, 0, 3) && isColor(px(295, 295), 255, 128, 0, 3) && isColor(px(95, 200), 0, 0, 0, 3) && isColor(px(305, 200), 0, 0, 0, 3)); }

    { Surface& s = fresh(orange); s.out[0] = {0.1f, 0.1f}; s.out[1] = {0.9f, 0.3f}; s.out[2] = {0.9f, 0.9f}; s.out[3] = {0.1f, 0.9f}; draw();
      int columns = 0, soft = 0, solidIn = 0, solidOut = 0;
      for (int x = 60; x < 340; x++) {                              // the slanted top edge runs along y = 40 + (x - 40) / 4
          const double edgeY = 40.0 + (x - 40) / 4.0; bool any = false;
          for (int y = (int)edgeY - 2; y <= (int)edgeY + 2; y++) { Px v = px(x, y); if (v.r > 12 && v.r < 243) any = true; }
          columns++; if (any) soft++;
      }
      for (int y = 250; y < 340; y++) if (isColor(px(200, y), 255, 128, 0, 3)) solidIn++;
      for (int y = 5; y < 60; y++) if (isColor(px(200, y), 0, 0, 0, 3)) solidOut++;
      expect("edges are anti-aliased: nearly every column along a slanted edge has an in-between pixel", soft > columns * 9 / 10);
      expect("...while the inside stays solid and the outside solid black", solidIn == 90 && solidOut == 55); }

    { fresh(orange, Shape::Circle); draw();
      expect("circle: centre filled, corners of its bounding quad empty", isColor(px(200, 200), 255, 128, 0, 3) && isColor(px(112, 112), 0, 0, 0, 3) && isColor(px(288, 112), 0, 0, 0, 3));
      expect("circle: the edge is round", isColor(px(266, 266), 255, 128, 0, 3) && isColor(px(276, 276), 0, 0, 0, 3)); }
    { fresh(orange, Shape::Triangle); draw();
      expect("triangle: narrow at the apex, wide at the base", isColor(px(200, 110), 255, 128, 0, 3) && isColor(px(120, 110), 0, 0, 0, 3) && isColor(px(115, 290), 255, 128, 0, 3) && isColor(px(200, 60), 0, 0, 0, 3)); }

    { Surface& s = fresh(orange); s.opacity = 0.5f; draw(); expect("opacity 50% blends with the black behind it", isColor(px(200, 200), 128, 64, 0, 3)); }
    { Surface& s = fresh(orange); s.tintR = 0.0f; draw(); expect("tint: red channel 0 removes the red", isColor(px(200, 200), 0, 128, 0, 3)); }
    { Surface& s = fresh(orange); s.tintG = 0.5f; s.tintB = 0.5f; draw(); expect("tint multiplies each channel", isColor(px(200, 200), 255, 64, 0, 3)); }
    { Surface& s = fresh(red); s.hue = 120.0f; draw(); expect("hue +120 degrees turns red into green exactly", isColor(px(200, 200), 0, 255, 0, 3)); }
    { Surface& s = fresh(red); s.hue = 240.0f; draw(); expect("hue +240 degrees turns red into blue exactly", isColor(px(200, 200), 0, 0, 255, 3)); }
    { Surface& s = fresh(red); s.hue = 360.0f; draw(); expect("a full 360 degree hue turn is the identity", isColor(px(200, 200), 255, 0, 0, 3)); }
    { Surface& s = fresh(orange); s.saturation = 0.0f; draw();
      const int luma = (int)(0.299 * 255 + 0.587 * 128 + 0.5); Px v = px(200, 200);
      expect("saturation 0 makes it grey at the colour's luminance", v.r == v.g && v.g == v.b && std::abs(v.r - luma) <= 2); }
    { Surface& s = fresh(grey); s.brightness = 0.25f; draw(); expect("brightness +25% lifts mid-grey by 64", isColor(px(200, 200), 192, 192, 192, 3)); }
    { Surface& s = fresh(grey); s.brightness = -0.5f; draw(); expect("brightness -50% darkens it to black", isColor(px(200, 200), 0, 0, 0, 3)); }
    { Surface& s = fresh(orange); s.contrast = 2.0f; draw(); expect("contrast 200% pushes channels away from mid-grey", isColor(px(200, 200), 255, 129, 0, 4)); }
    { Surface& s = fresh(orange); s.contrast = 0.0f; draw(); expect("contrast 0 flattens everything to mid-grey", isColor(px(200, 200), 128, 128, 128, 3)); }

    { Surface& s = fresh(orange); s.feather = 0.0f; draw(); Px crisp = px(105, 200);
      s.feather = 1.0f; draw(); Px nearEdge = px(105, 200), centre = px(200, 200), mid = px(150, 200);
      expect("feather 0: full strength right up to the edge", isColor(crisp, 255, 128, 0, 3));
      expect("feather 100%: the middle is still full strength", isColor(centre, 255, 128, 0, 6));
      expect("feather 100%: it fades towards the edge", nearEdge.r < 120 && mid.r > nearEdge.r + 40 && mid.r < 255); }

    { Surface& s = fresh(orange); s.posX = 0.25f; draw();
      expect("position X +25% of the stage moves the quad 100 px right", isColor(px(350, 200), 255, 128, 0, 3) && isColor(px(150, 200), 0, 0, 0, 3));
      expect("the mapped corners are untouched by the move", nearf(p.surfaces[0].out[0].x, 0.25, 1e-6)); }
    { Surface& s = fresh(orange); s.posY = -0.25f; draw(); expect("position Y -25% moves it up", isColor(px(200, 50), 255, 128, 0, 3) && isColor(px(200, 350), 0, 0, 0, 3)); }
    { Surface& s = fresh(orange); s.scale = 0.5f; draw(); expect("scale 50% shrinks it about its centre", isColor(px(160, 200), 255, 128, 0, 3) && isColor(px(130, 200), 0, 0, 0, 3)); }
    { Surface& s = fresh(orange); s.scale = 0.0f; draw(); expect("scale 0 draws nothing and doesn't crash", isColor(px(200, 200), 0, 0, 0, 3)); }
    { Surface& s = fresh(orange); setQuad(s.out, 0.2f, 0.4f, 0.8f, 0.6f); s.rotation = 90.0f; draw();
      expect("rotation 90 degrees turns a wide bar into a tall one", isColor(px(200, 100), 255, 128, 0, 3) && isColor(px(200, 300), 255, 128, 0, 3) && isColor(px(100, 200), 0, 0, 0, 3)); }
    { Surface& s = fresh(orange); setQuad(s.out, 0.2f, 0.4f, 0.8f, 0.6f); s.rotation = 45.0f; draw();
      expect("rotation 45 degrees puts the bar on the diagonal", isColor(px(270, 270), 255, 128, 0, 3) && isColor(px(130, 130), 255, 128, 0, 3) && isColor(px(270, 130), 0, 0, 0, 3)); }

    { fresh(split); draw(); expect("the split picture is red on the left, blue on the right", isColor(px(150, 200), 255, 0, 0, 3) && isColor(px(250, 200), 0, 0, 255, 3)); }
    { Surface& s = fresh(split); s.contentX = 0.25f; draw(); expect("content offset X +25% scrolls the picture right (the split moves 50 px)", isColor(px(180, 200), 255, 0, 0, 3) && isColor(px(240, 200), 255, 0, 0, 3) && isColor(px(260, 200), 0, 0, 255, 3)); }
    { Surface& s = fresh(split); s.contentScale = 2.0f; draw(); expect("content zoom 2x keeps the split centred and doubles each half", isColor(px(110, 200), 255, 0, 0, 3) && isColor(px(190, 200), 255, 0, 0, 3) && isColor(px(210, 200), 0, 0, 255, 3)); }
    { Surface& s = fresh(split); s.contentRotation = 90.0f; draw(); expect("content rotation 90 turns the red/blue split into top/bottom", isColor(px(200, 130), 255, 0, 0, 3) && isColor(px(200, 270), 0, 0, 255, 3)); }

    auto two = [&](int back, int front, Blend frontBlend) {
        clearSurfaces();
        p.addSurface(Shape::Quad, back);  setQuad(p.surfaces[0].out, 0.25f, 0.25f, 0.75f, 0.75f);
        p.addSurface(Shape::Quad, front); setQuad(p.surfaces[0].out, 0.5f, 0.25f, 1.0f, 0.75f);
        p.surfaces[0].blend = frontBlend; for (auto& s : p.surfaces) setQuad(s.in, 0, 0, 1, 1);
        draw(); };
    two(red, green, Blend::Over);     expect("Over: the front surface covers the back one where they overlap", isColor(px(250, 200), 0, 255, 0, 3) && isColor(px(150, 200), 255, 0, 0, 3) && isColor(px(350, 200), 0, 255, 0, 3));
    two(red, green, Blend::Add);      expect("Add: red + green overlap = yellow, the rest unchanged", isColor(px(250, 200), 255, 255, 0, 3) && isColor(px(150, 200), 255, 0, 0, 3) && isColor(px(350, 200), 0, 255, 0, 3));
    two(grey, grey, Blend::Add);      expect("Add clips at white instead of wrapping", isColor(px(250, 200), 255, 255, 255, 3));
    two(orange, grey, Blend::Multiply); expect("Multiply: orange x 50% grey darkens it", isColor(px(250, 200), 128, 64, 0, 3) && isColor(px(350, 200), 0, 0, 0, 3));
    two(grey, grey, Blend::Screen);   expect("Screen: two 50% greys give 75%", isColor(px(250, 200), 191, 191, 191, 3));
    two(red, green, Blend::Add); p.surfaces[0].opacity = 0.5f; draw();
    expect("Add respects opacity", isColor(px(250, 200), 255, 128, 0, 3));
    two(orange, grey, Blend::Multiply); p.surfaces[0].opacity = 0.0f; draw();
    expect("a fully transparent multiply surface changes nothing", isColor(px(250, 200), 255, 128, 0, 3));

    { Surface& s = fresh(split); s.flipH = true; draw(); expect("flip horizontal swaps the halves", isColor(px(150, 200), 0, 0, 255, 3) && isColor(px(250, 200), 255, 0, 0, 3)); }
    { Surface& s = fresh(orange); s.visible = false; draw(); expect("hidden surfaces aren't drawn", isColor(px(200, 200), 0, 0, 0, 3)); }
    // A quad in perspective has a vanishing line somewhere on the stage, where the warp divides by zero: nothing may be drawn there.
    { const RenderTarget big = makeRenderTarget(1920, 1080);
      const Vec2 quads[][4] = {{{0.14f, 0.14f}, {0.23f, 0.13f}, {0.226f, 0.297f}, {0.139f, 0.29f}}, {{0.745f, 0.147f}, {0.816f, 0.149f}, {0.818f, 0.29f}, {0.739f, 0.288f}},
                               {{0.127f, 0.708f}, {0.208f, 0.714f}, {0.219f, 0.93f}, {0.121f, 0.943f}}, {{0.438f, 0.413f}, {0.508f, 0.393f}, {0.517f, 0.466f}, {0.423f, 0.474f}},
                               {{0.4f, 0.4f}, {0.5f, 0.35f}, {0.6f, 0.7f}, {0.35f, 0.6f}}};
      for (Shape shape : {Shape::Quad, Shape::Circle, Shape::Triangle, Shape::Freeform}) {
          int stray = 0, inside = 0;
          for (const auto& q : quads) {
              Surface& sf = fresh(orange, shape);
              if (shape == Shape::Freeform) sf.poly = defaultPolygon();
              float x0 = 1, y0 = 1, x1 = 0, y1 = 0;
              for (int i = 0; i < 4; i++) { sf.out[i] = q[i]; x0 = std::min(x0, q[i].x); x1 = std::max(x1, q[i].x); y0 = std::min(y0, q[i].y); y1 = std::max(y1, q[i].y); }
              renderer.renderStage(big, p);
              std::vector<unsigned char> d((size_t)big.w * big.h * 4);
              glBindFramebuffer(GL_FRAMEBUFFER, big.fbo); glReadPixels(0, 0, big.w, big.h, GL_RGBA, GL_UNSIGNED_BYTE, d.data()); glBindFramebuffer(GL_FRAMEBUFFER, 0);
              for (int y = 0; y < big.h; y++) for (int x = 0; x < big.w; x++) {
                  if (!d[((size_t)(big.h - 1 - y) * big.w + x) * 4]) continue;
                  const bool in = x >= x0 * big.w - 2 && x <= x1 * big.w + 2 && y >= y0 * big.h - 2 && y <= y1 * big.h + 2;
                  in ? inside++ : stray++;
              }
          }
          if (stray) std::printf("  %s: %d stray pixels\n", shapeName(shape), stray);
          expect("a surface in perspective draws nothing outside itself (no stray lines)", stray == 0 && inside > 10000);
      }
      RenderTarget gone = big; destroyRenderTarget(gone); }
    expect("no OpenGL errors anywhere in this test", glGetError() == GL_NO_ERROR);
    destroyRenderTarget(rt);
    return finishTest();
}
