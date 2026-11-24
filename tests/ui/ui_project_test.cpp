// Saving, opening, the unsaved-changes prompts, missing media, damaged files, quitting, and crash recovery
// through the real menus, shortcuts and dialogs.
#include "UiTest.h"
#include "ProjectIO.h"

int main() {
    UiTest ui; if (!ui.ok()) return skipTest("no OpenGL 3.3 context available");
    Project* proj = nullptr;                          // always the CURRENT project (restart() replaces it)
    auto P = [&]() -> Project& { return *ui.project; };
    (void)proj;
    const fs::path docsDir = ui.home.path() / "Documents";
    fs::create_directories(docsDir);
    const std::string docs = pathToUtf8(docsDir), fileA = docs + "/myshow.maphappy", fileB = docs + "/other.maphappy", fileC = docs + "/missing.maphappy", fileD = docs + "/broken.maphappy";
    const std::string video = testVideoPath();

    P().addSurface(Shape::Quad); ui.editor->markClean();
    ui.warmup();
    auto dirty = [&] { return ui.editor->dirty(); };
    auto read = [&](const std::string& path) { std::string t; readTextFile(path, t); return t; };
    // the editor notices edits twice a second, so wait for it like a user would
    auto edited = [&](const char* what, std::function<void()> edit) { ui.act(edit); ui.waitUntil(what, dirty, 400); };

    // ---- a fresh project is clean; an edit makes it unsaved
    ui.check("a fresh project is clean and untitled", [&] { return !dirty() && ui.editor->windowTitle() == "MapHappy - Untitled"; });
    edited("an edit is noticed as an unsaved change", [&] { P().addSurface(Shape::Circle); });
    ui.check("the title shows the unsaved marker", [&] { return ui.editor->windowTitle() == "MapHappy - Untitled *"; });

    // ---- Ctrl+S on an untitled project: Save As, type a name, Enter
    ui.ctrl(ImGuiKey_S); ui.wait(2);
    ui.check("Ctrl+S on an untitled project opens the Save As dialog", [] { return UiTest::windowOpen("Save project as"); });
    ui.typeText("myshow"); ui.key(ImGuiKey_Enter); ui.wait(3);
    ui.check("typing a name and pressing Enter saves it, with the extension added", [&] { return fs::exists(pathFromUtf8(fileA)) && samePath(ui.editor->filePath(), fileA); });
    ui.check("the saved file is a valid project", [&] { return read(fileA).find("\"format\": \"maphappy\"") != std::string::npos; });
    ui.check("saving cleans the project and retitles the window; the dialog closes", [&] { return !dirty() && ui.editor->windowTitle() == "MapHappy - myshow.maphappy" && !UiTest::windowOpen("Save project as"); });
    ui.check("the file is remembered in the recent files", [&] { AppSettings s = loadSettings(); return !s.recent.empty() && samePath(s.recent[0], fileA) && samePath(s.lastFolder, docs); });

    // ---- Ctrl+S on a named project saves silently
    edited("another edit is unsaved", [&] { P().addSurface(Shape::Triangle); });
    ui.ctrl(ImGuiKey_S); ui.wait(2);
    ui.check("Ctrl+S on a named project saves without a dialog", [&] { return !UiTest::windowOpen("Save project as") && !dirty() && read(fileA).find("Triangle-1") != std::string::npos; });

    // ---- New with unsaved changes asks first
    edited("an edit to a surface is noticed", [&] { P().find(P().selectedSurfaceId)->opacity = 0.42f; });
    ui.act([&] { ui.editor->requestNew(); }); ui.wait(3);
    ui.check("New with unsaved changes asks what to do", [] { return UiTest::windowOpen("Unsaved changes"); });
    ui.click(UiTest::modalButton("Unsaved changes", 2)); ui.wait(3);                  // Cancel
    ui.check("Cancel keeps the project exactly as it was", [&] { return !UiTest::windowOpen("Unsaved changes") && P().surfaces.size() == 3 && dirty() && samePath(ui.editor->filePath(), fileA); });
    ui.act([&] { ui.editor->requestNew(); }); ui.wait(3);
    ui.click(UiTest::modalButton("Unsaved changes", 1)); ui.wait(4);                  // Don't save
    ui.check("Don't save discards the changes and starts a blank project", [&] { return P().surfaces.size() == 1 && P().surfaces[0].name == "Quad-1" && ui.editor->filePath().empty() && !dirty(); });
    ui.check("...while the old file still holds its last saved version", [&] { const std::string t = read(fileA); return t.find("Triangle-1") != std::string::npos && t.find("0.42") == std::string::npos; });

    // ---- Ctrl+O: type the path, Enter
    ui.ctrl(ImGuiKey_O); ui.wait(2);
    ui.check("Ctrl+O opens the Open dialog", [] { return UiTest::windowOpen("Open project"); });
    ui.typeText(fileA); ui.key(ImGuiKey_Enter); ui.wait(4);
    ui.check("opening a project restores it", [&] { return samePath(ui.editor->filePath(), fileA) && P().surfaces.size() == 3 && !dirty() && ui.editor->windowTitle() == "MapHappy - myshow.maphappy"; });

    // ---- opening another project while there are unsaved changes: Save first
    ui.act([&] { std::string e; Project other; other.addSurface(Shape::Quad); other.addSurface(Shape::Quad); saveProjectToFile(other, fileB, e); });
    edited("a rename is noticed", [&] { P().find(P().selectedSurfaceId)->name = "renamed"; });
    ui.act([&] { ui.editor->requestOpenFile(fileB); }); ui.wait(3);
    ui.check("opening while there are unsaved changes asks first", [] { return UiTest::windowOpen("Unsaved changes"); });
    ui.click(UiTest::modalButton("Unsaved changes", 0)); ui.wait(4);                  // Save
    ui.check("Save writes the changes, then opens the other project", [&] { return read(fileA).find("renamed") != std::string::npos && samePath(ui.editor->filePath(), fileB) && P().surfaces.size() == 2; });

    // ---- a project whose video is missing opens with a warning (needs the test video)
    if (!video.empty()) {
        const std::string clip = docs + "/clip.mp4";
        ui.act([&, clip] { fs::copy_file(video, clip, fs::copy_options::overwrite_existing); std::string e; Project v; const int id = v.media.addVideo(clip, e); v.addSurface(Shape::Quad, id); saveProjectToFile(v, fileC, e); fs::remove(clip); });
        ui.act([&] { ui.editor->requestOpenFile(fileC); }); ui.wait(4);
        ui.check("a project with a missing video opens and warns", [&] { return UiTest::windowOpen("Opened with problems") && samePath(ui.editor->filePath(), fileC); });
        ui.click(UiTest::modalButton("Opened with problems", 0)); ui.wait(3);
        ui.check("the missing video is a placeholder and the surface keeps its place", [&] { MediaItem* m = P().media.find(2); return !UiTest::windowOpen("Opened with problems") && m && m->missing && P().surfaces[0].mediaId == 2; });
        ui.check("opening a project with problems doesn't mark it unsaved", [&] { return !dirty(); });
    }

    // ---- a damaged file gives an error and leaves the open project alone
    ui.act([&] { std::ofstream(pathFromUtf8(fileD)) << "{ not valid json"; });
    ui.act([&] { ui.editor->requestOpenFile(fileD); }); ui.wait(4);
    ui.check("a damaged project shows an error", [] { return UiTest::windowOpen("Something went wrong"); });
    ui.click(UiTest::modalButton("Something went wrong", 0)); ui.wait(3);
    ui.check("...and the open project is untouched", [&] { return samePath(ui.editor->filePath(), (video.empty() ? fileB : fileC)); });

    // ---- the File menu
    ui.click(ui.uiCenter("menu:File")); ui.wait(3);
    ui.check("the File menu opens", [] { return ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel); });
    ui.click(UiTest::at(1100, 42)); ui.wait(3);                                       // click empty toolbar space, like a user would
    ui.check("...and closes again", [] { return !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel); });

    ui.act([&] { ui.editor->requestExit(); }); ui.wait(2);
    ui.check("quitting a clean project needs no question", [&] { return ui.editor->exitApproved() && !UiTest::windowOpen("Unsaved changes"); });
    ui.run();

    // ---- crash recovery: unsaved work is autosaved, then the app "crashes" and is started again
    ui.restart();
    ui.act([&] { ui.editor->requestOpenFile(fileB); }); ui.wait(4);
    edited("work on the reopened project is unsaved", [&] { P().addSurface(Shape::Circle); P().addSurface(Shape::Triangle); P().find(P().selectedSurfaceId)->name = "unsaved work"; });
    std::string before;
    ui.act([&] { ui.editor->autosaveNow(); before = projectToJson(P(), fileB, false); });
    ui.check("an autosave file is written next to the project", [&] { return fs::exists(pathFromUtf8(autosavePathFor(fileB))); });
    ui.restartStep();                                                                 // the crash
    ui.act([&] { ui.editor->requestOpenFile(fileB); }); ui.wait(5);
    ui.check("opening a project with a newer autosave offers to recover it", [] { return UiTest::windowOpen("Recover unsaved work?"); });
    ui.click(UiTest::modalButton("Recover unsaved work?", 0)); ui.wait(6);            // Recover
    ui.check("Recover brings back the unsaved work, exactly, and marks it unsaved", [&] { return dirty() && samePath(ui.editor->filePath(), fileB) && P().surfaces[0].name == "unsaved work" && projectToJson(P(), fileB, false) == before; });

    // ---- quitting with unsaved work asks first, and discarding it removes the autosave
    ui.act([&] { ui.editor->requestExit(); }); ui.wait(4);
    ui.check("quitting with unsaved changes asks first", [&] { return UiTest::windowOpen("Unsaved changes") && !ui.editor->exitApproved(); });
    ui.click(UiTest::modalButton("Unsaved changes", 2)); ui.wait(4);                  // Cancel
    ui.check("Cancel keeps the app open", [&] { return !ui.editor->exitApproved() && !UiTest::windowOpen("Unsaved changes"); });
    ui.act([&] { ui.editor->requestExit(); }); ui.wait(4);
    ui.click(UiTest::modalButton("Unsaved changes", 1)); ui.wait(4);                  // Don't save
    ui.check("Don't save lets the app quit and throws the autosave away, so it isn't offered again", [&] { return ui.editor->exitApproved() && !fs::exists(pathFromUtf8(autosavePathFor(fileB))); });
    ui.run();
    return finishTest();
}
