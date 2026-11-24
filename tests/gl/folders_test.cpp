// Folders of surfaces: nesting, visibility passed down to everything inside (checked on real stage pixels),
// moving and deleting, and saving / loading (including damaged folder data).
#include "GlTestSupport.h"
#include "ProjectIO.h"
#include "Renderer.h"
using json = nlohmann::json;

static void setQuad(Vec2* q, float x0, float y0, float x1, float y1) { q[0] = {x0, y0}; q[1] = {x1, y0}; q[2] = {x1, y1}; q[3] = {x0, y1}; }

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    GlContext gl; if (!gl.ok) return skipTest("no OpenGL 3.3 context available");
    Renderer renderer; Project p;
    p.outW = 400; p.outH = 400;
    RenderTarget rt = makeRenderTarget(400, 400);
    auto px = [&](int x, int y) { unsigned char d[4]; glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo); glReadPixels(x, 399 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, d); glBindFramebuffer(GL_FRAMEBUFFER, 0); return Px{d[0], d[1], d[2]}; };
    const int solid = p.media.addGenerator([] { GenSettings s = GenSettings::defaults(GenType::Solid); s.setSize(16, 16); return s; }());
    auto lit = [&](int x, int y) { return px(x, y).r > 200; };

    // three surfaces side by side: a (left), b (middle), c (right)
    const int a = p.addSurface(Shape::Quad, solid).id; setQuad(p.find(a)->out, 0.05f, 0.4f, 0.30f, 0.6f);
    const int b = p.addSurface(Shape::Quad, solid).id; setQuad(p.find(b)->out, 0.375f, 0.4f, 0.625f, 0.6f);
    const int c = p.addSurface(Shape::Quad, solid).id; setQuad(p.find(c)->out, 0.70f, 0.4f, 0.95f, 0.6f);
    const int outer = p.addFolder("Outer");
    const int inner = p.addFolder("Inner", outer);
    expect("folders get ids and names, and nest", outer == 1 && inner == 2 && p.findFolder(inner)->parentId == outer && p.folderDepth(inner) == 2);
    expect("a folder default name is numbered", p.findFolder(p.addFolder())->name == "Folder 3");
    p.removeFolder(3);
    p.moveSurfaceToFolder(a, outer); p.moveSurfaceToFolder(b, inner);

    renderer.renderStage(rt, p);
    expect("everything is shown while every folder is visible", lit(70, 200) && lit(200, 200) && lit(330, 200));
    p.findFolder(inner)->visible = false; renderer.renderStage(rt, p);
    expect("hiding a folder hides the surfaces in it, and only those", lit(70, 200) && !lit(200, 200) && lit(330, 200));
    p.findFolder(inner)->visible = true; p.findFolder(outer)->visible = false; renderer.renderStage(rt, p);
    expect("hiding a folder hides everything in the folders inside it too", !lit(70, 200) && !lit(200, 200) && lit(330, 200));
    expect("...while each surface keeps its own Visible switch untouched", p.find(a)->visible && p.find(b)->visible && !p.isShown(*p.find(b)));
    p.findFolder(outer)->visible = true;
    p.find(b)->visible = false; renderer.renderStage(rt, p);
    expect("a surface's own switch still works inside a folder", !lit(200, 200) && lit(70, 200));
    p.find(b)->visible = true;

    expect("folderWithin: a folder is within itself and its parents, the top level contains all", p.folderWithin(inner, outer) && p.folderWithin(inner, inner) && !p.folderWithin(outer, inner) && p.folderWithin(outer, 0));
    expect("a folder can't be moved inside itself or into its own sub-folder", !p.moveFolder(outer, inner) && !p.moveFolder(outer, outer) && p.findFolder(outer)->parentId == 0);
    expect("...or into a folder that doesn't exist", !p.moveFolder(inner, 99) && !p.moveSurfaceToFolder(a, 99) && p.find(a)->folderId == outer);
    expect("moving a folder to the top level works", p.moveFolder(inner, 0) && p.findFolder(inner)->parentId == 0 && p.moveFolder(inner, outer));

    p.removeFolder(outer);
    expect("deleting a folder keeps its contents: they move up to its parent", p.folders.size() == 1 && p.findFolder(inner)->parentId == 0 && p.find(a)->folderId == 0 && p.find(b)->folderId == inner && p.surfaces.size() == 3);

    // ---- save and load
    { const int deep = p.addFolder("Deep \xC3\xA9", inner); p.moveSurfaceToFolder(c, deep); p.findFolder(deep)->visible = false; p.findFolder(inner)->open = false;
      const std::string text = projectToJson(p, "");
      Project q; LoadReport rep;
      expect("folders load back with no warnings", loadProjectFromJson(q, text, "", rep) && rep.warnings.empty());
      expect("...exactly as saved", projectToJson(q, "") == text);
      expect("...with their nesting, names, switches and contents", q.folders.size() == 2 && q.findFolder(deep) && q.findFolder(deep)->parentId == inner && q.findFolder(deep)->name == "Deep \xC3\xA9"
             && !q.findFolder(deep)->visible && !q.findFolder(inner)->open && q.find(c)->folderId == deep && q.find(b)->folderId == inner);
      expect("...and new folders don't reuse old ids", q.addFolder() > deep);
      p.findFolder(inner)->open = true;
      std::string before = projectToJson(p, "", true); p.findFolder(inner)->open = false;
      expect("opening or closing a folder in the list is not an unsaved change", projectToJson(p, "", true) == before);
      p.findFolder(inner)->visible = false;
      expect("...but hiding one is", projectToJson(p, "", true) != before);
      p.findFolder(inner)->visible = true;

      json j = json::parse(text);
      j["folders"].push_back({{"id", 7}, {"name", "A"}, {"parent", 8}});
      j["folders"].push_back({{"id", 8}, {"name", "B"}, {"parent", 7}});           // A inside B inside A
      j["folders"].push_back({{"id", 9}, {"name", "Orphan"}, {"parent", 55}});
      j["folders"].push_back({{"id", 9}, {"name", "Twin"}});
      for (auto& s : j["surfaces"]) if (s["id"] == a) s["folder"] = 77;
      Project r; LoadReport rep2;
      const bool ok = loadProjectFromJson(r, j.dump(), "", rep2);
      for (auto& w : rep2.warnings) std::printf("  load warning: %s\n", w.c_str());
      expect("damaged folders still load, with a warning for each problem", ok && rep2.warnings.size() == 4);
      expect("...a loop of folders is broken, an orphan and a lost surface go to the top level, a repeated id is skipped",
             r.findFolder(9) && r.findFolder(9)->parentId == 0 && r.findFolder(9)->name == "Orphan" && r.find(a)->folderId == 0 && r.folderDepth(7) <= 2 && r.folderDepth(8) <= 2
             && (r.findFolder(7)->parentId == 0 || r.findFolder(8)->parentId == 0));

      json old = json::parse(text); old.erase("folders"); for (auto& s : old["surfaces"]) s.erase("folder");
      Project o; LoadReport rep3;
      expect("files from before folders load with everything at the top level", loadProjectFromJson(o, old.dump(), "", rep3) && rep3.warnings.empty() && o.folders.empty() && o.find(c)->folderId == 0); }

    p.resetToEmpty();
    expect("a new project has no folders", p.folders.empty() && p.addFolder() == 1);
    expect("no OpenGL errors anywhere in this test", glGetError() == GL_NO_ERROR);
    destroyRenderTarget(rt);
    return finishTest();
}
