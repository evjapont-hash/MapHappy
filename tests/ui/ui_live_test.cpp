// Live performance mode through the real editor: F5 / the Live button, storing presets with Shift + a number,
// playing them with the number keys (top row and keypad) and the pads, cross-fades, and keys that must NOT fire.
#include "UiTest.h"

int main() {
    UiTest ui; if (!ui.ok()) return skipTest("no OpenGL 3.3 context available");
    Project& p = *ui.project;
    const int orange = ui.addSolidMedia(255, 128, 0);
    const int id = p.addSurface(Shape::Quad, orange).id;
    { Surface& s = *p.find(id); s.out[0] = {0.2f, 0.2f}; s.out[1] = {0.8f, 0.2f}; s.out[2] = {0.8f, 0.8f}; s.out[3] = {0.2f, 0.8f}; }
    ui.warmup();
    auto S = [&]() -> Surface& { return *p.find(id); };
    auto shiftKey = [&](ImGuiKey k) {
        ui.act([&ui] { ui.io().AddKeyEvent(ImGuiMod_Shift, true); ui.io().AddKeyEvent(ImGuiKey_LeftShift, true); });
        ui.act([&ui, k] { ui.io().AddKeyEvent(k, true); });
        ui.act([&ui, k] { ui.io().AddKeyEvent(k, false); ui.io().AddKeyEvent(ImGuiMod_Shift, false); ui.io().AddKeyEvent(ImGuiKey_LeftShift, false); });
        ui.wait(2);
    };

    // a preset exists, but outside Live mode the number keys do nothing
    ui.act([&] { S().opacity = 0.3f; ui.editor->storePreset(0); S().opacity = 1.0f; });
    ui.key(ImGuiKey_1); ui.wait(2);
    ui.check("outside Live mode the number keys don't play presets", [&] { return nearf(S().opacity, 1.0, 1e-6) && !ui.editor->liveMode(); });

    ui.key(ImGuiKey_F5); ui.wait(2);
    ui.check("F5 switches Live mode on, and the preset pads appear", [&] { return ui.editor->liveMode() && ui.editor->uiRect("pad:1").valid && ui.editor->uiRect("pad:0").valid; });
    ui.act([&] { p.presets[0].fade = 0.0f; });
    ui.key(ImGuiKey_1); ui.wait(2);
    ui.check("in Live mode, key 1 plays the preset on 1 (a cut)", [&] { return nearf(S().opacity, 0.3, 1e-6) && ui.editor->presetPlayer().activeSlot() == 0; });
    ui.check("...and the stage shows it", [&] { const Px v = ui.stagePixel(0.5f, 0.5f); return std::abs(v.r - 77) <= 4 && std::abs(v.g - 38) <= 4; });

    ui.act([&] { S().hue = 120.0f; S().opacity = 1.0f; });
    shiftKey(ImGuiKey_2);
    ui.check("Shift + 2 stores the current look on key 2", [&] { return p.presets[1].used && p.presets[1].name == "Preset 2" && nearf(p.presets[1].surfaces[0].values.at("hue"), 120, 1e-6); });
    ui.key(ImGuiKey_1); ui.wait(2);
    ui.act([&] { p.presets[1].fade = 1.0f; });
    ui.key(ImGuiKey_Keypad2); ui.wait(2);
    ui.check("the keypad's 2 plays it too, and a 1 second fade is under way", [&] { return ui.editor->presetPlayer().fading() && ui.editor->presetPlayer().activeSlot() == 1 && S().hue > 0.0f && S().hue < 120.0f; });
    ui.waitUntil("...it lands on the preset's values", [&] { return !ui.editor->presetPlayer().fading() && nearf(S().hue, 120, 1e-4) && nearf(S().opacity, 1.0, 1e-6); }, 400);

    ui.key(ImGuiKey_5); ui.wait(2);
    ui.check("an empty key changes nothing", [&] { return nearf(S().hue, 120, 1e-4) && ui.editor->presetPlayer().activeSlot() == 1; });
    ui.act([&] { S().hue = -60.0f; });
    ui.click(ui.uiCenter("pad:5")); ui.wait(2);
    ui.check("clicking an empty pad stores the current look there", [&] { return p.presets[4].used && nearf(p.presets[4].surfaces[0].values.at("hue"), -60, 1e-6); });
    ui.act([&] { S().hue = 0.0f; p.presets[4].fade = 0.0f; });
    ui.click(ui.uiCenter("pad:5")); ui.wait(2);
    ui.check("clicking a stored pad plays it", [&] { return nearf(S().hue, -60, 1e-6) && ui.editor->presetPlayer().activeSlot() == 4; });

    ui.rclick(ui.uiCenter("pad:0")); ui.wait(2);
    ui.click(UiTest::popupItem("##Popup_", 0)); ui.wait(2);                // "Store the current look here"
    ui.check("right-click a pad > Store puts the current look on it (key 0 is the tenth slot)", [&] { return p.presets[9].used && p.presets[9].name == "Preset 0"; });
    ui.act([&] { p.presets[1].fade = 3.0f; }); ui.key(ImGuiKey_2); ui.wait(20);
    ui.shot("live");
    ui.waitUntil("(the 3 second fade finishes)", [&] { return !ui.editor->presetPlayer().fading(); }, 600);

    // typing never fires presets: a slider opened for typing takes the digits
    ui.click(ui.uiCenter("tab:Surfaces")); ui.wait(2);
    ui.holdCtrl(true); ui.click(ui.uiCenter("slider:opacity")); ui.holdCtrl(false); ui.wait(2);
    ui.check("(a value is being typed)", [] { return ImGui::GetIO().WantTextInput; });
    ui.act([&] { S().hue = 33.0f; });
    ui.key(ImGuiKey_1); ui.wait(2);
    ui.check("while typing, the number keys don't play presets", [&] { return nearf(S().hue, 33, 1e-6); });
    ui.key(ImGuiKey_Escape); ui.wait(2);

    ui.click(ui.uiCenter("button:Live")); ui.wait(2);
    ui.check("the Live button switches Live mode off again, and the Conductor comes back", [&] { return !ui.editor->liveMode() && !ui.editor->uiRect("pad:1").valid && ui.editor->uiRect("button:+ Track").valid; });
    ui.check("presets are part of the project (an unsaved change)", [&] { ui.editor->markClean(); p.presets[2] = p.presets[1]; ui.editor->refreshDirtyNow(); return ui.editor->dirty(); });
    ui.run();
    return finishTest();
}
