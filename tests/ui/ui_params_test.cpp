// The animatable parameters through the real editor: inspector sliders, the Animate shortcut,
// the blend dropdown, and the whole chain  timeline -> parameter -> renderer -> pixels.
#include "UiTest.h"

int main() {
    UiTest ui; if (!ui.ok()) return skipTest("no OpenGL 3.3 context available");
    Project& p = *ui.project; Timeline& tl = p.timeline;
    const int orange = ui.addSolidMedia(255, 128, 0);
    Surface& a = p.addSurface(Shape::Quad, orange);                        // id 1: a wide bar
    a.out[0] = {0.3f, 0.45f}; a.out[1] = {0.7f, 0.45f}; a.out[2] = {0.7f, 0.55f}; a.out[3] = {0.3f, 0.55f};
    Surface& b = p.addSurface(Shape::Circle, orange);                      // id 2: somewhere else
    b.out[0] = {0.05f, 0.05f}; b.out[1] = {0.2f, 0.05f}; b.out[2] = {0.2f, 0.25f}; b.out[3] = {0.05f, 0.25f};
    p.selectedSurfaceId = 1;
    ui.editor->markClean();
    ui.warmup();

    auto param = [&](int sid, const char* key) { float v = -999; getParam(p, surfaceParamPath(sid, key), v); return v; };
    // the inspector is taller than its panel once a section is open: scroll it with the mouse wheel, as a user would
    auto scrollPanel = [&](float notches) { ui.mouseTo(150, 500); ui.wait(1); ui.wheel(notches); };

    ui.check("the inspector has its Transform section and Blend dropdown", [&] { return ui.editor->uiRect("header:Transform").valid && ui.editor->uiRect("combo:Blend").valid; });
    ui.click(ui.uiCenter("header:Transform")); ui.wait(2);
    ui.check("opening Transform reveals its sliders", [&] { return ui.editor->uiRect("slider:posX").valid && ui.editor->uiRect("slider:rotation").valid; });
    ui.click(ui.uiCenter("slider:posX", 0.75f));
    ui.check("clicking a slider 75% along sets Position X to about +50%", [&] { return nearf(param(1, "posX"), 0.5, 0.06); });
    ui.rclick(ui.uiCenter("slider:posX"));
    ui.click(UiTest::popupItem("##Popup_", 19)); ui.wait(2);               // the menu's second entry: "Reset to default"
    ui.check("right-click > Reset to default puts a parameter back", [&] { return nearf(param(1, "posX"), 0.0, 1e-6); });
    ui.holdCtrl(true); ui.click(ui.uiCenter("slider:posX")); ui.holdCtrl(false); ui.wait(2);
    ui.check("Ctrl+click on a slider opens it for typing an exact value", [&] { return ImGui::GetIO().WantTextInput; });
    ui.click(UiTest::at(900, 300)); ui.wait(2);
    ui.check("...and the text box closes when you click away", [&] { return !ImGui::GetIO().WantTextInput; });

    scrollPanel(-4.0f);
    ui.click(ui.uiCenter("slider:scale", 0.5f));
    ui.check("the Scale slider spans 0%..400%, so halfway is 200%", [&] { return nearf(param(1, "scale"), 2.0, 0.12); });
    ui.click(ui.uiCenter("button:Reset transform"));
    ui.check("Reset transform puts all six transform parameters back", [&] { Surface* s = p.find(1); const Surface d; return s->scale == d.scale && s->posX == 0 && s->rotation == 0 && s->scaleX == 1 && s->scaleY == 1; });

    scrollPanel(+12.0f);
    ui.click(ui.uiCenter("combo:Blend")); ui.wait(2);
    ui.click(UiTest::popupItem("##Combo_", 19)); ui.wait(2);                // the second entry: Add
    ui.check("the Blend dropdown changes the blend mode", [&] { return p.find(1)->blend == Blend::Add; });
    ui.click(ui.uiCenter("combo:Blend")); ui.wait(2);
    ui.click(UiTest::popupItem("##Combo_", 0)); ui.wait(2);
    ui.check("...and back to Over", [&] { return p.find(1)->blend == Blend::Over; });

    // ---- right-click > Animate, then the timeline drives the surface
    ui.act([&] { p.transport.time = 0.0; });
    scrollPanel(-4.0f);
    ui.rclick(ui.uiCenter("slider:rotation"));
    ui.click(UiTest::popupItem("##Popup_", 0)); ui.wait(3);                 // "Animate in the Conductor"
    ui.check("right-click > Animate creates a track for that parameter", [&] { return tl.findTrackByPath(surfaceParamPath(1, "rotation")) != nullptr; });
    ui.check("...with a first clip at the playhead holding the current value (nothing jumps)", [&] { Track* t = tl.findTrackByPath(surfaceParamPath(1, "rotation")); float v; return t && t->clips.size() == 1 && nearf(t->clips[0].start, 0.0, 1e-6) && nearf(t->clips[0].duration, 2.0, 1e-6) && t->evaluate(1.0, v) && nearf(v, 0.0, 1e-6); });
    ui.act([&] { Track* t = tl.findTrackByPath(surfaceParamPath(1, "rotation")); if (t && !t->clips.empty()) t->clips[0].data->keys = {{0, 0.0f, Interp::Linear}, {2.0, 90.0f, Interp::Linear}}; });
    ui.act([&] { p.transport.time = 0.0; p.transport.seeked = true; p.timelineDirty = true; }); ui.wait(3);
    ui.check("t=0: the bar is horizontal on the stage", [&] { return isColor(ui.stagePixel(0.35f, 0.5f), 255, 128, 0, 6) && isColor(ui.stagePixel(0.5f, 0.2f), 0, 0, 0, 6); });
    ui.act([&] { p.transport.time = 2.0; p.transport.seeked = true; p.timelineDirty = true; }); ui.wait(3);
    ui.check("t=2 s: the timeline has turned it 90 degrees, so it is now a tall bar on the real stage pixels", [&] { return nearf(param(1, "rotation"), 90.0, 1e-3) && isColor(ui.stagePixel(0.5f, 0.2f), 255, 128, 0, 6) && isColor(ui.stagePixel(0.35f, 0.5f), 0, 0, 0, 6); });
    ui.check("animating the rotation never touched the mapped corners", [&] { Surface* s = p.find(1); return nearf(s->out[0].x, 0.3, 1e-6) && nearf(s->out[2].y, 0.55, 1e-6); });
    ui.check("a timeline driving a parameter does NOT count as an unsaved change", [&] { ui.editor->markClean(); p.transport.time = 1.0; applyTimeline(p); ui.editor->refreshDirtyNow(); return !ui.editor->dirty() && nearf(param(1, "rotation"), 45.0, 1e-3); });
    ui.check("...but changing a parameter nothing animates does", [&] { p.find(1)->scaleX = 1.7f; ui.editor->refreshDirtyNow(); const bool d = ui.editor->dirty(); p.find(1)->scaleX = 1.0f; return d; });

    // ---- Add Track menu, nested
    ui.click(ui.uiCenter("button:+ Track"));
    ui.mouseTo(UiTest::popupItem("##Popup_", 0)); ui.wait(3);
    ui.mouseTo(UiTest::menuItem(0, 19)); ui.wait(3);                         // second category: Transform
    ui.check("a surface menu lists its categories, and a category lists its parameters", [] { UiTest::menuWindow(0); return UiTest::menuWindow(1) != nullptr; });
    ui.click(UiTest::menuItem(1, 0)); ui.wait(2);                            // "Position X" of the first-listed surface
    ui.check("choosing a parameter adds its track", [&] { return tl.findTrackByPath(surfaceParamPath(p.surfaces.front().id, "posX")) != nullptr; });

    // ---- clicking selects what you can SEE, not what is mapped
    ui.act([&] { p.find(1)->rotation = 0; if (Track* t = tl.findTrackByPath(surfaceParamPath(1, "rotation"))) tl.removeTrack(t->id); p.find(1)->posY = 0.3f; p.selectedSurfaceId = 2; });
    ui.wait(3);
    ui.click(ui.stagePos(0.5f, 0.78f)); ui.wait(2);
    ui.check("with the bar moved down by animation, clicking where it now is selects it", [&] { return p.selectedSurfaceId == 1; });
    ui.act([&] { p.selectedSurfaceId = 2; });
    ui.click(ui.stagePos(0.5f, 0.5f)); ui.wait(2);
    ui.check("clicking where the bar used to be (now empty) doesn't select it", [&] { return p.selectedSurfaceId == 2; });
    ui.run();
    return finishTest();
}
