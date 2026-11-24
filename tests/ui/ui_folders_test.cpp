// Folders in the real Surfaces list: making them, dragging surfaces into them, nesting, hiding a folder (checked on
// stage pixels), the right-click Move to menu, adding into the selected folder, and deleting a folder.
#include "UiTest.h"

int main() {
    UiTest ui; if (!ui.ok()) return skipTest("no OpenGL 3.3 context available");
    Project& p = *ui.project;
    const int orange = ui.addSolidMedia(255, 128, 0);
    const int a = p.addSurface(Shape::Quad, orange).id;
    { Surface& s = *p.find(a); s.out[0] = {0.1f, 0.3f}; s.out[1] = {0.4f, 0.3f}; s.out[2] = {0.4f, 0.7f}; s.out[3] = {0.1f, 0.7f}; s.name = "Left"; }
    const int b = p.addSurface(Shape::Quad, orange).id;
    { Surface& s = *p.find(b); s.out[0] = {0.6f, 0.3f}; s.out[1] = {0.9f, 0.3f}; s.out[2] = {0.9f, 0.7f}; s.out[3] = {0.6f, 0.7f}; s.name = "Right"; }
    ui.warmup();
    auto lit = [&](float x, float y) { return ui.stagePixel(x, y).r > 200; };
    // a drag-and-drop in the list: press, move past ImGui's drag threshold, rest on the target, release
    auto dragTo = [&](UiTest::Pos from, UiTest::Pos to) {
        ui.mouseTo(from); ui.down(); ui.wait(1);
        ui.mouseTo([from] { ImVec2 f = from(); return ImVec2(f.x + 12, f.y + 4); }); ui.wait(2);
        ui.mouseTo(to); ui.wait(3);
        ui.up(); ui.wait(3);
    };

    ui.click(ui.uiCenter("button:+ Folder")); ui.wait(2);
    ui.check("+ Folder makes a folder and selects it (its inspector shows)", [&] { return p.folders.size() == 1 && p.folders[0].name == "Folder 1" && ui.editor->uiRect("button:Delete folder").valid; });
    ui.check("...and it is listed", [&] { return ui.editor->uiRect("folder:Folder 1").valid; });

    dragTo(ui.uiCenter("surface:Left", 0.6f), ui.uiCenter("folder:Folder 1", 0.5f));
    ui.check("dragging a surface onto a folder puts it in", [&] { return p.find(a)->folderId == p.folders[0].id && p.find(b)->folderId == 0; });

    ui.click(ui.uiCenter("folderVisible:Folder 1")); ui.wait(3);
    ui.check("unticking a folder hides what is in it on the stage, and nothing else", [&] { return !p.folders[0].visible && !lit(0.25f, 0.5f) && lit(0.75f, 0.5f); });
    ui.click(ui.uiCenter("folderVisible:Folder 1")); ui.wait(3);
    ui.check("ticking it again brings it back", [&] { return lit(0.25f, 0.5f); });

    ui.click(ui.uiCenter("folder:Folder 1", 0.6f)); ui.wait(2);
    ui.click(ui.uiCenter("button:+ Folder")); ui.wait(2);
    ui.check("+ Folder with a folder selected makes one inside it", [&] { return p.folders.size() == 2 && p.folders[1].parentId == p.folders[0].id; });
    ui.click(ui.uiCenter("button:+ Quad")); ui.wait(2);
    ui.check("a new surface goes into the selected folder", [&] { return p.surfaces.size() == 3 && p.surfaces.front().folderId == p.folders[1].id; });

    ui.click(ui.uiCenter("folderVisible:Folder 1")); ui.wait(3);
    ui.check("hiding the outer folder hides the inner folder's surfaces too", [&] { return !p.isShown(p.surfaces.front()) && !lit(0.25f, 0.5f); });
    ui.click(ui.uiCenter("folderVisible:Folder 1")); ui.wait(3);

    dragTo(ui.uiCenter("folder:Folder 1", 0.6f), ui.uiCenter("folder:Folder 2", 0.6f));
    ui.check("a folder can't be dropped into its own sub-folder", [&] { return p.folders[0].parentId == 0 && p.folders[1].parentId == p.folders[0].id; });

    ui.rclick(ui.uiCenter("surface:Left", 0.6f)); ui.wait(2);
    ui.mouseTo(UiTest::popupItem("##Popup_", 0)); ui.wait(3);               // "Move to"
    ui.click(UiTest::menuItem(0, 0)); ui.wait(2);                          // "Top level"
    ui.check("right-click > Move to > Top level takes a surface out of its folder", [&] { return p.find(a)->folderId == 0; });

    ui.click(ui.uiCenter("folder:Folder 1", 0.6f)); ui.wait(2);
    ui.shot("folders");
    ui.click(ui.uiCenter("button:Delete folder")); ui.wait(3);
    ui.check("deleting a folder keeps its contents, one level up", [&] { return p.folders.size() == 1 && p.folders[0].name == "Folder 2" && p.folders[0].parentId == 0 && p.surfaces.size() == 3 && p.surfaces.front().folderId == p.folders[0].id; });
    ui.click(ui.stagePos(0.75f, 0.5f)); ui.wait(2);
    ui.check("clicking a surface on the stage shows the surface inspector again", [&] { return p.selectedSurfaceId == b && !ui.editor->uiRect("button:Delete folder").valid; });
    ui.run();
    return finishTest();
}
