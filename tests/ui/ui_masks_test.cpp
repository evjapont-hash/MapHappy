// Masks through the real editor: the Masks section of the surface inspector, and dragging a mask's handles,
// moving it, and editing a polygon mask's points on the stage.
#include "UiTest.h"

int main() {
    UiTest ui; if (!ui.ok()) return skipTest("no OpenGL 3.3 context available");
    Project& p = *ui.project;
    const int orange = ui.addSolidMedia(255, 128, 0);
    const int id = p.addSurface(Shape::Quad, orange).id;
    { Surface& s = *p.find(id); s.out[0] = {0.2f, 0.2f}; s.out[1] = {0.8f, 0.2f}; s.out[2] = {0.8f, 0.8f}; s.out[3] = {0.2f, 0.8f}; }
    ui.warmup();
    auto S = [&]() -> Surface& { return *p.find(id); };
    auto lit = [&](float x, float y) { return isColor(ui.stagePixel(x, y), 255, 128, 0, 6); };
    auto dark = [&](float x, float y) { return isColor(ui.stagePixel(x, y), 0, 0, 0, 6); };
    auto unitPos = [&](float ux, float uy) { return ui.stagePos(0.2f + 0.6f * ux, 0.2f + 0.6f * uy); };     // the surface's own 0..1 square on the stage
    auto scrollPanel = [&](float notches) { ui.mouseTo(150, 600); ui.wait(1); ui.wheel(notches); };

    ui.check("the surface inspector has a Masks section", [&] { return ui.editor->uiRect("header:Masks").valid; });
    ui.click(ui.uiCenter("header:Masks")); ui.wait(2);
    scrollPanel(-10.0f);
    ui.click(ui.uiCenter("button:+ Rectangle")); ui.wait(3);
    ui.check("+ Rectangle adds a mask and selects it", [&] { return S().masks.size() == 1 && S().masks[0].shape == MaskShape::Rectangle && ui.editor->uiRect("button:Delete mask").valid; });
    ui.check("...and it cuts a hole in the surface on the stage", [&] { return dark(0.5f, 0.5f) && lit(0.25f, 0.5f); });

    ui.drag(unitPos(0.7f, 0.7f), unitPos(0.9f, 0.9f));
    ui.check("dragging a mask's corner on the stage resizes it", [&] { const Mask& m = S().masks[0]; return nearf(m.max.x, 0.9f, 0.02) && nearf(m.max.y, 0.9f, 0.02) && nearf(m.min.x, 0.3f, 1e-5); });
    ui.drag(unitPos(0.6f, 0.6f), unitPos(0.5f, 0.6f));
    ui.check("dragging inside it moves it", [&] { const Mask& m = S().masks[0]; return nearf(m.min.x, 0.2f, 0.02) && nearf(m.max.x, 0.8f, 0.02) && nearf(m.min.y, 0.3f, 0.02); });
    ui.drag(unitPos(0.5f, 0.6f), unitPos(0.95f, 0.6f));
    ui.check("...but not past the surface's edge", [&] { const Mask& m = S().masks[0]; return nearf(m.max.x, 1.0f, 1e-4) && nearf(m.min.x, 0.4f, 0.02); });

    scrollPanel(-10.0f);                                                    // the selected mask's settings make the inspector taller
    ui.click(ui.uiCenter("combo:Mask mode")); ui.wait(2);
    ui.click(UiTest::popupItem("##Combo_", 19)); ui.wait(3);
    ui.check("the Mode list switches it to 'show only what is inside'", [&] { return S().masks[0].mode == MaskMode::ShowOnly && dark(0.25f, 0.5f) && lit(0.5f + 0.6f * 0.25f, 0.6f); });
    ui.click(ui.uiCenter("maskEnabled:Mask 1")); ui.wait(3);
    ui.check("unticking a mask switches it off", [&] { return !S().masks[0].enabled && lit(0.25f, 0.5f); });

    ui.click(ui.uiCenter("button:+ Polygon")); ui.wait(3);
    ui.check("+ Polygon adds a three-point mask, named after the first", [&] { return S().masks.size() == 2 && S().masks[1].points.size() == 3 && S().masks[1].name == "Mask 2"; });
    ui.dclick(unitPos(0.5f, 0.7f)); ui.wait(2);                            // the middle of its bottom edge
    ui.check("double-clicking a polygon mask's edge adds a point there", [&] { const auto& q = S().masks[1].points; return q.size() == 4 && nearf(q[2].y, 0.7f, 0.02) && nearf(q[2].x, 0.5f, 0.02); });
    ui.drag(unitPos(0.5f, 0.25f), unitPos(0.5f, 0.1f));
    ui.check("dragging one of its points moves it", [&] { return nearf(S().masks[1].points[0].y, 0.1f, 0.02); });
    ui.rclick(unitPos(0.5f, 0.7f)); ui.wait(2);
    ui.check("right-clicking a point deletes it", [&] { return S().masks[1].points.size() == 3; });

    scrollPanel(-10.0f);
    ui.click(ui.uiCenter("button:Delete mask")); ui.wait(3);
    ui.check("Delete mask removes the selected mask", [&] { return S().masks.size() == 1 && S().masks[0].name == "Mask 1"; });
    ui.check("masks count as unsaved changes", [&] { ui.editor->markClean(); S().masks[0].feather = 0.3f; ui.editor->refreshDirtyNow(); return ui.editor->dirty(); });
    ui.run();
    return finishTest();
}
