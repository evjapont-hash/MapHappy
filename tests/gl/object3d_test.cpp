// 3D object surfaces: the model is rendered with the media as its texture, then mapped onto the stage by the quad.
// Checked by reading back real pixels, plus saving and loading.
#include "GlTestSupport.h"
#include "Params.h"
#include "PathUtil.h"
#include "ProjectIO.h"
#include "Renderer.h"
#include <set>
using json = nlohmann::json;

// left half red, right half blue
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

static void setQuad(Vec2* q, float x0, float y0, float x1, float y1) { q[0] = {x0, y0}; q[1] = {x1, y0}; q[2] = {x1, y1}; q[3] = {x0, y1}; }

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    GlContext gl; if (!gl.ok) return skipTest("no OpenGL 3.3 context available");
    Renderer renderer; Project p;
    p.outW = 400; p.outH = 400;
    const int W = 400, H = 400;
    RenderTarget rt = makeRenderTarget(W, H);
    int split = 0;
    { auto m = std::make_unique<MediaItem>(); m->id = p.media.nextId(); p.media.ensureNextIdAbove(m->id); m->name = "split"; m->kind = MediaKind::Generator;
      m->source = std::make_unique<SplitSource>(); split = m->id; p.media.items.push_back(std::move(m)); }
    auto px = [&](int x, int y) { unsigned char d[4]; glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo); glReadPixels(x, H - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, d); glBindFramebuffer(GL_FRAMEBUFFER, 0); return Px{d[0], d[1], d[2]}; };
    auto isRed = [](Px v) { return v.r > 200 && v.g < 30 && v.b < 30; };
    auto isBlue = [](Px v) { return v.b > 200 && v.g < 30 && v.r < 30; };
    auto isBlack = [](Px v) { return isColor(v, 0, 0, 0, 3); };
    auto draw = [&] { renderer.renderStage(rt, p); };

    Surface& s0 = p.addSurface(Shape::Object3D, split);
    const int id = s0.id;
    auto S = [&]() -> Surface& { return *p.find(id); };
    setQuad(S().out, 0, 0, 1, 1);
    S().rotX = 0; S().rotY = 0; S().rotZ = 0;
    draw();
    expect("a new 3D object takes the whole picture as its texture", nearf(S().in[0].x, 0, 1e-6) && nearf(S().in[2].x, 1, 1e-6));
    expect("a cube facing the camera shows one face: the picture, red left and blue right", isRed(px(170, 200)) && isBlue(px(230, 200)));
    expect("...the space around the model stays empty (transparent)", isBlack(px(10, 10)) && isBlack(px(390, 390)) && isBlack(px(200, 10)));
    expect("...and the model has its own texture, sized like its quad", renderer.objectTexture(id) != 0 && renderer.objectTextureWidth(id) == 400 && renderer.objectTextureHeight(id) == 400);

    S().rotY = 45; draw();
    expect("turned 45 degrees: two faces, meeting at the middle (blue half of the left face, red half of the right)", isBlue(px(185, 200)) && isRed(px(215, 200)));
    expect("...and the depth test hides the faces behind (the outer halves show their own colours)", isRed(px(85, 200)) && isBlue(px(315, 200)));
    S().rotY = 0;


    S().objScale = 0.5f; draw();
    expect("size 50% shrinks the model about the middle", isRed(px(185, 200)) && isBlack(px(120, 200)));
    S().objScale = 1.0f;
    setParam(p, surfaceParamPath(id, "rotY"), 45.0f); draw();
    expect("turn Y is an animatable parameter like any other", nearf(S().rotY, 45, 1e-6) && isBlue(px(185, 200)));
    S().rotY = 0;

    S().meshKind = MeshKind::Sphere; draw();
    expect("a sphere is round: its middle is filled, its bounding square's corners are empty", isRed(px(150, 200)) && isBlue(px(250, 200)) && isBlack(px(40, 40)) && isBlack(px(360, 360)) && isRed(px(100, 200)));
    S().meshKind = MeshKind::Plane; draw();
    expect("a plane facing the camera fills a square in the middle", isRed(px(110, 200)) && isBlue(px(290, 200)));

    setQuad(S().out, 0.0f, 0.25f, 1.0f, 0.75f); S().meshKind = MeshKind::Sphere; draw();
    expect("in a wide quad the model keeps its shape (the texture is sized 2:1, not stretched)", renderer.objectTextureWidth(id) == 2 * renderer.objectTextureHeight(id) && isRed(px(150, 200)) && isBlack(px(40, 200)) && isBlack(px(360, 200)));
    setQuad(S().out, 0, 0, 1, 1);

    S().meshKind = MeshKind::Cube; S().flipH = true; draw();
    expect("flipping and the colour controls apply to the rendered model", isBlue(px(170, 200)) && isRed(px(230, 200)));
    S().flipH = false; S().hue = 120.0f; draw();
    expect("...(hue 120: red turns green)", px(170, 200).g > 200 && px(170, 200).r < 40);
    S().hue = 0;
    S().in[0] = {0, 0}; S().in[1] = {0.5f, 0}; S().in[2] = {0.5f, 1}; S().in[3] = {0, 1}; draw();
    expect("the input quad chooses which part of the picture the model wears", isRed(px(170, 200)) && isRed(px(230, 200)));
    p.resetInput(S());

    TempDir tmp("obj");
    const std::string objPath = tmp.file("tri.obj");
    { std::ofstream f(std::filesystem::u8path(objPath)); f << "v -1 -1 0\nv 1 -1 0\nv 0 1 0\nvt 0 1\nvt 1 1\nvt 0.5 0\nf 1/1 2/2 3/3\n"; }
    S().meshKind = MeshKind::File; S().meshPath = objPath; draw();
    expect("an OBJ model renders: a triangle, wide at the bottom and pointed at the top", (isRed(px(150, 300)) || isBlue(px(150, 300))) && isBlack(px(120, 110)) && isBlack(px(280, 110)) && !isBlack(px(200, 130)));
    S().meshPath = tmp.file("missing.obj"); draw();
    expect("a model file that can't be read shows a cube instead", isRed(px(170, 200)) && isBlue(px(230, 200)));

    S().visible = false; draw();
    expect("a hidden 3D object is not rendered, and its texture is let go", isBlack(px(200, 200)) && renderer.objectTexture(id) == 0);
    S().visible = true;

    // ---- lighting (on a white or red picture, the cube's front face filling the middle)
    { const int white = p.media.addGenerator([] { GenSettings g = GenSettings::defaults(GenType::Solid); g.setSize(8, 8); g.setColor("color", {1, 1, 1, 1}); return g; }());
      const int redPic = p.media.addGenerator([] { GenSettings g = GenSettings::defaults(GenType::Solid); g.setSize(8, 8); g.setColor("color", {1, 0, 0, 1}); return g; }());
      Surface& o = S();
      const LightRig defaults;
      o.mediaId = white; o.meshKind = MeshKind::Cube; o.rotX = 0; o.rotY = 0; o.shading = 1.0f;
      o.light = defaults; o.light.ambient = 0; o.light.ks = 0; o.light.dirAzimuth = 0; o.light.dirElevation = 0; o.light.model = (float)LightModel::BlinnPhong;
      auto r = [&](int x, int y) { return px(x, y).r; };
      draw();
      expect("lighting: a face looking straight at the sun is fully lit", r(200, 200) >= 250);
      o.light.dirAzimuth = 60; draw();
      expect("...at 60 degrees it gets half the light (diffuse = N.L)", std::abs(r(200, 200) - 128) <= 4);
      o.light.dirAzimuth = 90; draw();
      expect("...side-on it gets none", r(200, 200) <= 3);
      o.light.ambient = 0.5f; draw();
      expect("the ambient term lights everything evenly", std::abs(r(200, 200) - 128) <= 4 && std::abs(r(120, 280) - 128) <= 4);
      o.light.ambient = 0; o.shading = 0.5f; draw();
      expect("Lighting 50% mixes the lit and the plain picture", std::abs(r(200, 200) - 128) <= 4);
      o.shading = 1.0f; o.light.model = (float)LightModel::Unlit; draw();
      expect("the Unlit model shows the picture as it is, whatever the lights", r(200, 200) >= 250);

      o.light.model = (float)LightModel::BlinnPhong; o.light.dirAzimuth = 0; o.mediaId = redPic; o.light.ks = 1.0f; o.light.shininess = 32; draw();
      const Px spotC = px(200, 200), off = px(140, 140);
      expect("specular: a white highlight where the light reflects towards the camera, fading away from it", spotC.r >= 250 && spotC.g > 200 && off.g < spotC.g - 20);
      const int blinnOff = px(160, 160).g;
      o.light.model = (float)LightModel::Phong; draw();
      const int phongOff = px(160, 160).g;
      expect("Phong and Blinn-Phong are different programs: Blinn's highlight is wider at the same shininess", px(200, 200).g > 200 && blinnOff > phongOff + 5);
      o.light.ks = 0; o.mediaId = white; o.light.model = (float)LightModel::BlinnPhong;

      o.light.dirIntensity = 0; o.light.pointIntensity = 1.0f; o.light.pointX = 0; o.light.pointY = 0; o.light.pointZ = 1.2f; o.light.pointRange = 1.0f; draw();
      expect("a point light close to the face lights its middle more than its corners", r(200, 200) > r(90, 90) + 40 && r(200, 200) > 40);
      o.light.pointIntensity = 0; o.light.spotIntensity = 2.0f; o.light.spotX = 0; o.light.spotY = 0; o.light.spotZ = 4.0f; o.light.spotAngle = 10; o.light.spotSoftness = 0; draw();
      expect("a narrow spotlight lights a disc in the middle and nothing outside its cone", r(200, 200) > 200 && r(120, 200) <= 3 && r(200, 120) <= 3);
      o.light.spotSoftness = 1.0f; o.light.spotAngle = 30; draw();
      { int partial = 0; for (int x = 200; x < 340; x += 2) { int v = r(x, 200); if (v > 20 && v < 230) partial++; }
        expect("...a soft spotlight fades out towards its edge", partial > 5); }
      o.light.spotIntensity = 0; o.light.dirIntensity = 1;

      o.meshKind = MeshKind::Sphere; o.light.dirAzimuth = 70; o.light.model = (float)LightModel::BlinnPhong; draw();
      std::set<int> smooth; for (int x = 40; x < 360; x++) smooth.insert(r(x, 200));
      o.light.model = (float)LightModel::Toon; draw();
      std::set<int> toon; for (int x = 40; x < 360; x++) toon.insert(r(x, 200));
      expect("Toon lights a sphere in a few flat bands; Blinn-Phong in a smooth ramp", toon.size() <= 8 && smooth.size() > 40);
      o.light = defaults; o.shading = 0; o.meshKind = MeshKind::Cube; o.mediaId = split; }

    // ---- save and load
    { S().meshKind = MeshKind::File; S().meshPath = objPath; S().rotX = 12.5f; S().fov = 70.0f; S().shading = 0.4f;
      S().mediaId = MediaPool::kPatternId; p.removeMedia(split);        // (the test's own picture isn't something a project can save)
      const std::string file = tmp.file("show.maphappy");
      std::string err;
      expect("a project with a 3D object saves", saveProjectToFile(p, file, err));
      std::string text; readTextFile(file, text);
      const json j = json::parse(text);
      expect("the model file is stored relative to the project too", j["surfaces"][0]["mesh"] == "file" && j["surfaces"][0]["meshPathRel"] == "tri.obj");
      Project q; LoadReport rep;
      expect("...and loads back exactly, without warnings", loadProjectFromFile(q, file, rep) && rep.warnings.empty() && projectToJson(q, file) == projectToJson(p, file));
      const Surface* qs = q.find(id);
      expect("...with its model and view", qs && qs->shape == Shape::Object3D && qs->meshKind == MeshKind::File && samePath(qs->meshPath, objPath) && nearf(qs->rotX, 12.5f, 1e-6) && nearf(qs->fov, 70.0f, 1e-6));
      fs::remove(pathFromUtf8(objPath));
      Project r; LoadReport rep2;
      expect("a missing model file is reported when the project opens", loadProjectFromFile(r, file, rep2) && rep2.warnings.size() == 1 && rep2.warnings[0].find("tri.obj") != std::string::npos); }

    p.removeSurface(id); draw();
    expect("removing the surface lets its texture go", renderer.objectTexture(id) == 0);
    expect("no OpenGL errors anywhere in this test", glGetError() == GL_NO_ERROR);
    destroyRenderTarget(rt);
    return finishTest();
}
