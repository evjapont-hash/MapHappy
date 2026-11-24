// Makes the desktop app's pictures for the README (not a test): run with MAPHAPPY_TEST_SHOTS=<folder> and the show to open.
//   readme_shots web/show/show.maphappy
#include "UiTest.h"
#include "ProjectIO.h"

int main(int argc, char** argv) {
    if (argc < 2 || getTestEnv("MAPHAPPY_TEST_SHOTS").empty()) { std::printf("usage: MAPHAPPY_TEST_SHOTS=<folder> readme_shots <show.maphappy>\n"); return 1; }
    const std::string show = pathToUtf8(fs::absolute(pathFromUtf8(argv[1])));
    UiTest ui(1600, 900, /*appFont*/ true);
    if (!ui.ok()) return 1;
    ui.warmup();
    ui.act([&] { ui.editor->requestOpenFile(show); });
    ui.waitUntil("the song is loaded", [&] { Project& p = *ui.project; return !p.audioAssets.empty() && p.audioAssets[0].buf; });
    ui.act([&] {
        Project& p = *ui.project;
        for (Surface& s : p.surfaces) if (s.shape == Shape::Object3D) p.selectedSurfaceId = s.id;
        p.transport.time = 5.0; p.transport.seeked = true; p.timelineDirty = true;
    });
    ui.mouseTo(1300, 620); ui.wait(60);
    ui.shot("desktop-editor");
    ui.click(ui.uiCenter("button:Live")); ui.wait(5);
    ui.mouseTo(1300, 620); ui.wait(60);
    ui.shot("desktop-live");
    ui.run();
    return 0;
}
