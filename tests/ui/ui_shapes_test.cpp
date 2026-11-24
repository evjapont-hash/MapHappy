// Line and freeform surfaces through the real editor: adding them, dragging a line's ends, and editing a
// freeform outline's points (drag, double-click an edge to add, right-click to delete) on the stage.
#include "UiTest.h"

int main() {
    UiTest ui; if (!ui.ok()) return skipTest("no OpenGL 3.3 context available");
    Project& p = *ui.project;
    const int orange = ui.addSolidMedia(255, 128, 0);
    ui.warmup();
    auto sel = [&]() -> Surface* { return p.selected(); };
    auto offset = [](UiTest::Pos base, float dx, float dy) { return [base, dx, dy] { ImVec2 b = base(); return ImVec2(b.x + dx, b.y + dy); }; };
    auto at = [&](Vec2 v) { return ui.stagePos(v.x, v.y); };
    // freeform point i on the stage, looked up when the step runs
    auto pointPos = [&](int i) { return [&ui, &p, i] { Surface* s = p.selected(); Mat3 H; squareToQuad(s->out, H); const Vec2 v = mapPoint(H, s->poly[i]); return ui.stagePos(v.x, v.y)(); }; };

    // ---- line
    ui.click(ui.uiCenter("button:+ Line"));
    ui.check("+ Line adds a line surface and selects it", [&] { return sel() && sel()->shape == Shape::Line && p.surfaces.size() == 1; });
    ui.act([&] { sel()->mediaId = orange; sel()->lineA = {0.3f, 0.5f}; sel()->lineB = {0.6f, 0.5f}; }); ui.wait(3);
    ui.check("the line shows on the stage between its ends", [&] { return isColor(ui.stagePixel(0.45f, 0.5f), 255, 128, 0, 6) && isColor(ui.stagePixel(0.45f, 0.4f), 0, 0, 0, 6); });
    ui.drag(at({0.6f, 0.5f}), at({0.7f, 0.2f}));
    ui.check("dragging a line's end moves that end", [&] { return nearf(sel()->lineB.x, 0.7f, 0.01) && nearf(sel()->lineB.y, 0.2f, 0.01) && nearf(sel()->lineA.x, 0.3f, 1e-6); });
    ui.check("...and its quad follows", [&] { Vec2 q[4]; lineQuad(*sel(), p.stageAspect(), q); return nearf(sel()->out[1].x, q[1].x, 1e-5) && nearf(sel()->out[1].y, q[1].y, 1e-5); });
    ui.drag(at({0.5f, 0.35f}), at({0.5f, 0.45f}));                        // the middle of the line: its move handle
    ui.check("dragging the middle moves the whole line", [&] { return nearf(sel()->lineA.y, 0.6f, 0.01) && nearf(sel()->lineB.y, 0.3f, 0.01); });
    ui.drag(at({0.3f, 0.6f}), at({0.7f, 0.3f}));                          // try to drop one end onto the other
    ui.check("a line's two ends can't be dropped onto each other", [&] { return std::hypot(sel()->lineA.x - sel()->lineB.x, sel()->lineA.y - sel()->lineB.y) > 0.003f; });

    // ---- freeform
    ui.click(ui.uiCenter("button:+ Freeform"));
    ui.act([&] { Surface* s = sel(); s->mediaId = orange; s->out[0] = {0.3f, 0.2f}; s->out[1] = {0.7f, 0.2f}; s->out[2] = {0.7f, 0.8f}; s->out[3] = {0.3f, 0.8f};
                 s->poly = {{0.2f, 0.2f}, {0.8f, 0.2f}, {0.8f, 0.8f}, {0.2f, 0.8f}}; }); ui.wait(3);
    ui.check("+ Freeform adds a freeform surface", [&] { return sel() && sel()->shape == Shape::Freeform && p.surfaces.size() == 2; });
    ui.drag(pointPos(0), at({0.3f + 0.4f * 0.05f, 0.2f + 0.6f * 0.1f}));
    ui.check("dragging a point moves it (in the quad's own coordinates)", [&] { return nearf(sel()->poly[0].x, 0.05f, 0.02) && nearf(sel()->poly[0].y, 0.1f, 0.02) && nearf(sel()->poly[1].x, 0.8f, 1e-6); });
    ui.check("...and the stage shows the new outline", [&] { return isColor(ui.stagePixel(0.3f + 0.4f * 0.12f, 0.2f + 0.6f * 0.15f), 255, 128, 0, 6); });
    ui.drag(pointPos(1), at({0.95f, 0.1f}));
    ui.check("a point dragged outside the quad stops at the quad's edge", [&] { return nearf(sel()->poly[1].x, 1.0f, 1e-6) && nearf(sel()->poly[1].y, 0.0f, 1e-6); });
    ui.dclick([&ui, &p] { Surface* s = p.selected(); Mat3 H; squareToQuad(s->out, H); const Vec2 a = mapPoint(H, s->poly[2]), b = mapPoint(H, s->poly[3]); return ui.stagePos((a.x + b.x) / 2, (a.y + b.y) / 2)(); });
    ui.wait(2);
    ui.check("double-clicking an edge adds a point on it", [&] { const auto& q = sel()->poly; return q.size() == 5 && nearf(q[3].x, 0.5f, 0.02) && nearf(q[3].y, 0.8f, 0.02); });
    ui.rclick(pointPos(3)); ui.wait(2);
    ui.check("right-clicking a point deletes it", [&] { return sel()->poly.size() == 4; });
    for (int i = 0; i < 2; i++) { ui.rclick(pointPos(0)); ui.wait(2); }
    ui.check("an outline keeps at least three points", [&] { return sel()->poly.size() == 3; });
    ui.click(ui.uiCenter("button:Reset outline")); ui.wait(2);
    ui.check("Reset outline brings back the hexagon", [&] { return sel()->poly.size() == 6; });
    ui.run();
    return finishTest();
}
