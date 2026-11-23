// Selecting surfaces and editing their corners with the mouse, in the Stage and Input views.
#include "UiTest.h"

int main() {
    UiTest ui; if (!ui.ok()) return skipTest("no OpenGL 3.3 context available");
    Project& p = *ui.project;
    const int orange = ui.addSolidMedia(255, 128, 0);
    Surface& a = p.addSurface(Shape::Quad, orange);                       // id 1
    a.out[0] = {0.10f, 0.25f}; a.out[1] = {0.50f, 0.18f}; a.out[2] = {0.48f, 0.80f}; a.out[3] = {0.12f, 0.72f};
    a.in[0] = {0.1f, 0.1f}; a.in[1] = {0.7f, 0.1f}; a.in[2] = {0.7f, 0.9f}; a.in[3] = {0.1f, 0.9f};
    Surface& c = p.addSurface(Shape::Circle, orange);                     // id 2, selected
    c.out[0] = {0.62f, 0.30f}; c.out[1] = {0.90f, 0.22f}; c.out[2] = {0.88f, 0.80f}; c.out[3] = {0.66f, 0.70f};
    ui.warmup();

    // positions that follow the surfaces' CURRENT corners, looked up when each step runs
    auto outCorner = [&](int id, int i) { return [&ui, id, i] { Surface* s = ui.project->find(id); return ui.stagePos(s->out[i].x, s->out[i].y)(); }; };
    auto inCorner  = [&](int id, int i) { return [&ui, id, i] { Surface* s = ui.project->find(id); return ui.inputPos(s->in[i].x, s->in[i].y)(); }; };
    auto offset = [](UiTest::Pos base, float dx, float dy) { return [base, dx, dy] { ImVec2 b = base(); return ImVec2(b.x + dx, b.y + dy); }; };

    ui.click(ui.stagePos(0.30f, 0.49f));
    ui.check("clicking a surface on the stage selects it", [&] { return p.selectedSurfaceId == 1; });

    const Vec2 before2 = p.find(1)->out[2];                              // (a reference taken before adding another surface would dangle)
    ui.drag(outCorner(1, 2), offset(ui.stagePos(before2.x, before2.y), 40, 20));      // the target is a FIXED point (a target relative to the moving corner would run away)
    ui.check("dragging an output corner moves it by the mouse's movement", [&] { Surface* s = p.find(1); Editor::Rect r = ui.editor->stageRect(); return nearf(s->out[2].x, before2.x + 40.0f / r.w, 0.01) && nearf(s->out[2].y, before2.y + 20.0f / r.h, 0.01); });

    float x0 = 0, x3 = 0;
    ui.act([&] { x0 = p.find(1)->out[0].x; x3 = p.find(1)->out[3].x; });
    ui.drag(ui.stagePos(0.30f, 0.49f), ui.stagePos(0.30f + 30.0f / 577.0f, 0.49f));
    ui.check("dragging inside the quad moves the whole quad", [&] { Surface* s = p.find(1); return s->out[0].x - x0 > 0.03f && nearf(s->out[0].x - x0, s->out[3].x - x3, 1e-4); });

    ui.drag(inCorner(1, 2), offset(ui.inputPos(0.7f, 0.9f), -30, -15));
    ui.check("dragging an input corner crops the picture", [&] { Surface* s = p.find(1); return s->in[2].x < 0.7f - 0.02f && s->in[2].y < 0.9f - 0.02f; });

    ui.drag(outCorner(1, 0), [&ui, &p] { Surface* s = p.find(1); return ui.stagePos(s->out[2].x + 0.05f, s->out[2].y + 0.05f)(); });
    ui.check("a drag that would twist the quad into a bow-tie is refused", [&] { return isConvex(p.find(1)->out); });

    ui.click(ui.stagePos(0.77f, 0.51f));
    ui.check("clicking another surface selects that one", [&] { return p.selectedSurfaceId == 2; });
    ui.click(ui.stagePos(0.02f, 0.95f));
    ui.check("clicking empty stage keeps the selection", [&] { return p.selectedSurfaceId == 2; });
    ui.check("a new circle's input region is square in pixels", [&] { Surface* s = p.find(2); return nearf((s->in[1].x - s->in[0].x) * 1.0f, (s->in[3].y - s->in[0].y) * 1.0f, 0.05); });
    ui.run();
    return finishTest();
}
