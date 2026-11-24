// The media generators through the real Media tab: the + Generator menu, the inspector's parameter controls,
// putting a generator on a surface, and changing its picture size.
#include "UiTest.h"

int main() {
    UiTest ui; if (!ui.ok()) return skipTest("no OpenGL 3.3 context available");
    Project& p = *ui.project;
    Surface& s = p.addSurface(Shape::Quad);
    s.out[0] = {0.2f, 0.2f}; s.out[1] = {0.8f, 0.2f}; s.out[2] = {0.8f, 0.8f}; s.out[3] = {0.2f, 0.8f};
    const int surfaceId = s.id;
    ui.editor->markClean();
    ui.warmup();
    ui.check("the project starts with no unsaved changes", [&] { ui.editor->refreshDirtyNow(); return !ui.editor->dirty(); });

    auto newest = [&]() -> MediaItem* { return p.media.items.empty() ? nullptr : p.media.items.back().get(); };
    auto addFromMenu = [&](int index) {
        ui.click(ui.uiCenter("button:+ Generator")); ui.wait(2);
        ui.click(UiTest::popupItem("##Popup_", 19.0f * index)); ui.wait(3);
    };

    ui.click(ui.uiCenter("tab:Media")); ui.wait(2);
    ui.check("the Media tab has a + Generator button", [&] { return ui.editor->uiRect("button:+ Generator").valid; });

    addFromMenu(3);                                                       // the fourth entry: Solid Colour
    ui.check("+ Generator > Solid Colour adds one to the media pool", [&] { MediaItem* m = newest(); return p.media.items.size() == 2 && m->generator() && m->generator()->settings.type == GenType::Solid && m->name == "Solid Colour"; });
    ui.check("...and shows its parameters in the inspector", [&] { return ui.editor->uiRect("gen:color").valid; });
    ui.click(ui.uiCenter("button:Use on selected surface")); ui.wait(3);
    ui.check("Use on selected surface puts it on the surface", [&] { return p.find(surfaceId)->mediaId == newest()->id; });
    ui.check("...and the stage shows its colour", [&] { return isColor(ui.stagePixel(0.5f, 0.5f), 255, 128, 0, 3) && isColor(ui.stagePixel(0.05f, 0.05f), 0, 0, 0, 3); });

    addFromMenu(5);                                                       // Strobe
    ui.check("a second generator is added and selected", [&] { MediaItem* m = newest(); return p.media.items.size() == 3 && m->generator() && m->generator()->settings.type == GenType::Strobe && ui.editor->uiRect("gen:rate").valid; });
    ui.click(ui.uiCenter("gen:rate", 0.75f));
    ui.check("dragging the Rate slider changes the strobe's rate", [&] { const float r = newest()->generator()->settings.num("rate"); return r > 20.0f && r < 25.0f; });
    ui.click(ui.uiCenter("gen:fade", 0.5f));
    ui.check("...and the Fade slider its fade", [&] { return nearf(newest()->generator()->settings.num("fade"), 0.5, 0.06); });

    ui.act([&] { p.outW = 1280; p.outH = 720; }); ui.wait(2);
    ui.click(ui.uiCenter("button:Match output")); ui.wait(3);
    ui.check("Match output gives the generator the stage's resolution, applied between frames", [&] { const GeneratorSource* g = newest()->generator(); return g->settings.width == 1280 && g->settings.height == 720 && g->width() == 1280 && g->height() == 720; });

    addFromMenu(2);                                                       // Text
    ui.check("the Text generator's inspector has a text box", [&] { MediaItem* m = newest(); return m->generator() && m->generator()->settings.type == GenType::Text && ui.editor->uiRect("gen:text").valid; });
    ui.click(ui.uiCenter("gen:text")); ui.wait(2);
    ui.key(ImGuiKey_End); ui.typeText(" WORLD"); ui.wait(2);
    ui.check("typing in it changes the text", [&] { return newest()->generator()->settings.text("text") == "HELLO WORLD"; });
    ui.click(UiTest::at(900, 300)); ui.wait(2);
    ui.check("adding generators counts as an unsaved change", [&] { ui.editor->refreshDirtyNow(); return ui.editor->dirty(); });
    ui.run();
    return finishTest();
}
