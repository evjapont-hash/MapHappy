// Undo and redo through the real shortcuts and menu: edits on the stage, sliders, deleting, media, songs and playback.
#include "UiTest.h"

int main() {
    UiTest ui; if (!ui.ok()) return skipTest("no OpenGL 3.3 context available");
    Project& p = *ui.project;
    Surface& s0 = p.addSurface(Shape::Quad);
    s0.out[0] = {0.2f, 0.2f}; s0.out[1] = {0.8f, 0.2f}; s0.out[2] = {0.8f, 0.8f}; s0.out[3] = {0.2f, 0.8f};
    const int id = s0.id;
    ui.editor->markClean();
    ui.warmup(60);
    auto undo = [&] { ui.ctrl(ImGuiKey_Z); ui.wait(2); };
    auto redo = [&] { ui.ctrl(ImGuiKey_Y); ui.wait(2); };
    auto noticed = [&] { ui.wait(50); };                                    // an edit made by code is picked up within half a second
    auto corner = [&] { return p.find(id)->out[0]; };

    ui.check("a fresh project has nothing to undo", [&] { return !ui.editor->canUndo() && !ui.editor->canRedo(); });
    ui.ctrl(ImGuiKey_Z); ui.wait(2);
    ui.check("...and Ctrl+Z leaves it alone", [&] { return p.surfaces.size() == 1; });

    // ---- a whole drag is one step
    ui.drag(ui.stagePos(0.2f, 0.2f), ui.stagePos(0.1f, 0.1f)); ui.wait(5);
    ui.check("dragging a corner moves it", [&] { return nearf(corner().x, 0.1, 0.02); });
    undo();
    ui.check("Ctrl+Z puts the corner back, in one step", [&] { return nearf(corner().x, 0.2, 1e-5) && !ui.editor->canUndo(); });
    ui.check("...and the project is as saved again", [&] { return !ui.editor->dirty(); });
    ui.check("...with the surface still selected", [&] { return p.selectedSurfaceId == id; });
    redo();
    ui.check("Ctrl+Y does it again", [&] { return nearf(corner().x, 0.1, 0.02); });
    ui.ctrl(ImGuiKey_Z); ui.wait(2);
    ui.ctrl(ImGuiKey_Z, true); ui.wait(2);
    ui.check("Ctrl+Shift+Z redoes too", [&] { return nearf(corner().x, 0.1, 0.02); });

    // ---- several steps, then a new edit drops the redo
    ui.click(ui.uiCenter("button:+ Circle")); ui.wait(5);
    ui.click(ui.uiCenter("button:+ Triangle")); ui.wait(5);
    ui.check("two surfaces are added", [&] { return p.surfaces.size() == 3; });
    undo();
    ui.check("undo removes the last one", [&] { return p.surfaces.size() == 2 && p.surfaces[0].shape == Shape::Circle; });
    undo();
    ui.check("...then the one before", [&] { return p.surfaces.size() == 1; });
    ui.click(ui.uiCenter("button:+ Line")); ui.wait(5);
    ui.check("a new edit drops what could be redone", [&] { return p.surfaces.size() == 2 && !ui.editor->canRedo(); });
    undo();

    // ---- deleting a surface takes its animation with it; undo brings both back
    ui.act([&] { p.timeline.addTrack(surfaceParamPath(id, "opacity")); }); noticed();
    ui.act([&] { p.removeSurface(id); }); noticed();
    ui.check("deleting a surface deletes its track", [&] { return p.surfaces.empty() && p.timeline.tracks.empty(); });
    undo();
    ui.check("undo brings back the surface and its track", [&] { return p.find(id) && p.timeline.findTrackByPath(surfaceParamPath(id, "opacity")); });
    undo();

    // ---- a generator keeps its picture going (it isn't made again), and its settings go back
    int gen = 0; ContentSource* src = nullptr;
    ui.act([&] { gen = p.media.addGenerator(GenType::Strobe); p.find(id)->mediaId = gen; src = p.media.source(gen); }); noticed();
    ui.act([&] { p.media.find(gen)->generator()->settings.setSize(640, 360); }); noticed();
    undo();
    ui.check("undo puts a generator's size back, on the same source", [&] { return p.media.source(gen) == src && p.media.source(gen)->width() != 640; });
    undo();
    ui.check("undo of adding it removes it", [&] { return !p.media.find(gen) && p.find(id)->mediaId == MediaPool::kPatternId; });
    redo();
    ui.check("redo brings it back, on the surface", [&] { return p.media.find(gen) && p.find(id)->mediaId == gen; });

    // ---- a song's sound stays loaded, and playback isn't disturbed
    int song = 0;
    ui.act([&] {
        AudioAsset a; a.name = "song"; a.path = "song.wav"; a.bpm = 100;
        auto b = std::make_shared<SongBuffer>(); b->frames = kAudioRate * 4; b->samples.assign(b->frames * 2, 0.0f); a.buf = b;
        computePeaks(*b, a.peakMin, a.peakMax);
        song = p.addAudioAsset(std::move(a)); p.placeAudio(song);
    });
    noticed();
    ui.act([&] { p.findAsset(song)->bpm = 128; }); noticed();
    ui.act([&] { p.transport.time = 1.0; p.transport.playing = true; }); ui.wait(5);
    undo();
    ui.check("undoing a tempo change keeps the sound loaded", [&] { AudioAsset* a = p.findAsset(song); return a && nearf(a->bpm, 100) && a->buf && !a->peakMin.empty() && !a->loading; });
    ui.check("...and the show plays on", [&] { return p.transport.playing && p.transport.time > 1.0; });
    ui.act([&] { p.transport.playing = false; }); ui.wait(2);

    // ---- playing or selecting is not a step
    redo();
    ui.act([&] { p.transport.time = 0.5; p.timelineDirty = true; p.selectedSurfaceId = 0; }); noticed();
    undo();
    ui.check("moving the playhead and selecting are not steps", [&] { return nearf(p.findAsset(song)->bpm, 100); });

    // ---- the menu
    ui.click(ui.uiCenter("menu:Edit")); ui.wait(2);
    ui.click(ui.uiCenter("item:Redo", 0.2f)); ui.wait(3);
    ui.check("Edit > Redo works", [&] { return nearf(p.findAsset(song)->bpm, 128); });
    ui.click(ui.uiCenter("menu:Edit")); ui.wait(2);
    ui.click(ui.uiCenter("item:Undo", 0.2f)); ui.wait(3);
    ui.check("Edit > Undo works", [&] { return nearf(p.findAsset(song)->bpm, 100); });

    // ---- a new project starts a new history
    ui.act([&] { ui.editor->markClean(); ui.editor->requestNew(); }); noticed();
    ui.check("a new project has nothing to undo", [&] { return !ui.editor->canUndo() && !ui.editor->canRedo(); });

    ui.run();
    return finishTest();
}
