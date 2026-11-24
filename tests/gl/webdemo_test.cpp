// The web player's demo show (web/show/show.maphappy) must open in the desktop app without problems, so it can be
// edited there and played on the web. Also checks the song is found through its relative path.
#include "GlTestSupport.h"
#include "ProjectIO.h"
#include "PathUtil.h"

int main() {
    GlContext gl; if (!gl.ok) return skipTest("no OpenGL 3.3 context available");
    const std::string file = std::string(MAPHAPPY_SOURCE_DIR) + "/web/show/show.maphappy";
    Project p; LoadReport rep;
    const bool ok = loadProjectFromFile(p, file, rep);
    for (auto& w : rep.warnings) std::printf("  load warning: %s\n", w.c_str());
    if (!rep.error.empty()) std::printf("  load error: %s\n", rep.error.c_str());
    expect("the web demo show opens in the desktop app without warnings", ok && rep.warnings.empty());
    expect("...with its cube, backdrop and title", p.surfaces.size() == 3 && p.surfaces[0].shape == Shape::Object3D && p.surfaces[0].name == "Cube");
    expect("...its generators", p.media.items.size() == 4 && p.media.find(3) && p.media.find(3)->generator() && p.media.find(3)->generator()->settings.type == GenType::Lines);
    expect("...its song, found next to the project", p.audioAssets.size() == 1 && rep.audioToLoad.size() == 1);
    expect("...its timeline and presets", p.timeline.tracks.size() == 7 && p.presets[0].used && p.presets[9].used && p.presets[9].name == "Dark");
    Project q; LoadReport rep2;
    expect("saving it from the desktop app keeps everything (it loads back identically)", loadProjectFromJson(q, projectToJson(p, file), pathToUtf8(pathFromUtf8(file).parent_path()), rep2) && projectToJson(q, file) == projectToJson(p, file));
    return finishTest();
}
