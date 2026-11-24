// Dropping a song in: background loading, waveform, tempo, markers, snapping to beats, splitting, and sample-accurate playback.
#include "UiTest.h"

int main() {
    UiTest ui; if (!ui.ok()) return skipTest("no OpenGL 3.3 context available");
    Project& p = *ui.project; Timeline& tl = p.timeline;
    TempDir tmp("audio");
    const std::string songPath = tmp.file("song.wav");
    writeSyntheticSongFile(songPath, 130, 0.173, 45.0, SongPattern::House);
    ui.simulateAudio = true;                                               // a stand-in for the sound card's thread

    p.addSurface(Shape::Quad); p.addSurface(Shape::Circle);                // ids 1 and 2
    const int pt = tl.addTrack("surface/2/opacity");                       // a parameter track FIRST, so it sits in the visible part of the panel
    const int pc = tl.addClip(pt, 1.0, 2.0, 1.0f);
    ui.warmup();

    auto geom = [&] { return ui.editor->conductor().geom(); };
    auto X = [&](double t) { return [=, &ui] { auto g = ui.editor->conductor().geom(); return (float)(g.canvasX0 + (t - g.scrollT) * g.pps); }; };
    auto laneTop = [&](int lane) { return [=, &ui] { return ui.editor->conductor().geom().laneTop0 + lane * 64.0f; }; };
    auto pt2 = [&](std::function<float()> x, std::function<float()> y) { return [=] { return ImVec2(x(), y()); }; };
    auto asset = [&]() -> AudioAsset* { return p.audioAssets.empty() ? nullptr : &p.audioAssets[0]; };
    auto audioTrack = [&]() -> Track* { for (auto& t : tl.tracks) if (t.kind == TrackKind::Audio) return &t; return nullptr; };

    ui.act([&] { ui.editor->onFileDropped(songPath); });
    ui.check("dropping a song starts loading it in the background", [&] { return p.audioAssets.empty() && !p.audioStatus.empty(); });
    ui.waitUntil("the song finishes loading", [&] { return asset() != nullptr; }, 1500);
    ui.wait(4);
    ui.check("the tempo was detected (130 BPM)", [&] { return asset() && nearf(asset()->bpm, 130.0, 0.6); });
    ui.check("the waveform was computed", [&] { return asset() && !asset()->peakMax.empty(); });
    ui.check("an audio track was created with one clip at the start, as long as the song", [&] { Track* t = audioTrack(); return t && t->audioClips.size() == 1 && nearf(t->audioClips[0].start, 0.0, 1e-6) && asset() && nearf(t->audioClips[0].duration, asset()->duration(), 1e-3); });
    ui.check("the loading message is gone", [&] { return p.audioStatus.empty(); });

    ui.act([&] { p.transport.time = 2.0; p.transport.seeked = true; });
    ui.key(ImGuiKey_M);
    ui.check("M drops a marker at the playhead", [&] { return tl.markers.size() == 1 && nearf(tl.markers[0].time, 2.0, 0.02); });

    // drag the parameter clip so an edge is within a few pixels of a beat: it must snap exactly onto the beat
    auto beat = [&](long k) { return asset()->beatOffset + (double)k * 60.0 / asset()->bpm; };
    ui.drag(pt2(X(1.5), [&] { return laneTop(0)() + 10; }),
            pt2([&] { const long k = (long)std::ceil((4.0 - asset()->beatOffset) / (60.0 / asset()->bpm)); return X(beat(k) - 0.025 + 0.5)(); }, [&] { return laneTop(0)() + 10; }));
    ui.check("dragging a clip near a beat snaps one of its edges exactly onto the beat", [&] {
        Clip* c = tl.findTrack(pt)->findClip(pc); const double period = 60.0 / asset()->bpm;
        auto onBeat = [&](double t) { const double q = (t - asset()->beatOffset) / period; return std::fabs(q - std::round(q)) < 0.004 / period; };
        return onBeat(c->start) || onBeat(c->end()); });

    // split the song at the playhead
    ui.act([&] { p.transport.time = 20.0; p.transport.seeked = true; p.timelineDirty = true; });
    ui.rclick(pt2(X(8.0), [&] { return laneTop(1)() + 10; }));              // the audio clip's header (the menu then has room below it)
    ui.click(UiTest::popupItem("##Popup_", 0, 22)); ui.wait(2);             // "Split at playhead"
    ui.check("right-click an audio clip > Split at playhead makes two clips", [&] { Track* t = audioTrack(); return t && t->audioClips.size() == 2; });
    ui.check("the halves join exactly, the right one starting 20 s into the file", [&] {
        Track* t = audioTrack(); if (!t || t->audioClips.size() != 2) return false;
        auto& l = t->audioClips[0]; auto& r = t->audioClips[1];
        return nearf(l.end(), r.start, 1e-9) && nearf(r.offset, 20.0, 1e-6) && nearf(l.duration, 20.0, 1e-6) && nearf(r.end(), asset()->duration(), 1e-6); });

    // play across the split: the sound must be continuous and match the original file sample for sample
    ui.act([&] { p.transport.time = 19.5; p.transport.seeked = true; p.transport.playing = true; ui.capturing = true; });
    ui.wait(90);                                                             // 1.5 s of show
    double captureEnd = 0;                                                    // the mixer's clock at the instant capturing stops (pausing fades out a little more, which is not captured)
    ui.act([&] { captureEnd = ui.mixer.time(); ui.capturing = false; p.transport.playing = false; });
    ui.check("the playhead followed the audio clock through the split", [&] { return nearf(p.transport.time, 21.0, 0.08); });
    ui.check("the sound after the split point matches the original file sample for sample", [&] {
        const double t0 = captureEnd - (double)(ui.captured.size() / 2) / kAudioRate;            // the exact timeline time of the first captured sample
        auto sampleAt = [&](double t) { return ui.captured[(size_t)std::llround((t - t0) * kAudioRate) * 2]; };
        auto original = [&](double t) { return asset()->buf->samples[(size_t)std::llround(t * kAudioRate) * 2]; };
        return nearf(sampleAt(20.5), original(20.5), 1e-4) && nearf(sampleAt(19.9), original(19.9), 1e-4); });
    ui.check("there is no dropout at the seam", [&] {
        const double t0 = captureEnd - (double)(ui.captured.size() / 2) / kAudioRate;
        auto rms = [&](double a, double b) { double s = 0; const size_t i0 = (size_t)std::llround((a - t0) * kAudioRate), i1 = (size_t)std::llround((b - t0) * kAudioRate); for (size_t i = i0; i < i1; i++) s += (double)ui.captured[i * 2] * ui.captured[i * 2]; return std::sqrt(s / (double)(i1 - i0)); };
        const double l = rms(19.98, 20.0), r = rms(20.0, 20.02);
        return r > 0.2 * l && r > 1e-4; });
    // opening the saved show loads its song again, and then says nothing more about it
    const std::string show = ui.home.file("song.maphappy");
    ui.act([&] { std::string e; saveProjectToFile(p, show, e); ui.editor->markClean(); ui.editor->requestOpenFile(show); });
    ui.waitUntil("a reopened show loads its song", [&] { return !p.audioAssets.empty() && p.audioAssets[0].buf && samePath(ui.editor->filePath(), show); }, 1500);
    ui.wait(3);
    ui.check("...and the loading message goes away", [&] { return p.audioStatus.empty(); });
    ui.run();
    return finishTest();
}
