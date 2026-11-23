// Saving and loading shows: round trips, moved projects, missing files, damaged files, settings and autosave.
#include "GlTestSupport.h"
#include "ProjectIO.h"
#include "PathUtil.h"
#include "Params.h"
#include "AudioLoader.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>
using json = nlohmann::json;

static bool hasWarning(const LoadReport& r, const std::string& needle) { for (auto& w : r.warnings) if (w.find(needle) != std::string::npos) return true; return false; }

static void buildShow(Project& p, const std::string& video, const std::string& song) {
    std::string err;
    const int vid = p.media.addVideo(video, err);
    p.addSurface(Shape::Quad, vid);                                   // id 1
    Surface& b = p.addSurface(Shape::Circle);                         // id 2
    b.opacity = 0.6f; b.flipH = true;
    { const Vec2 o[4] = {{0.62f, 0.30f}, {0.90f, 0.22f}, {0.88f, 0.80f}, {0.66f, 0.70f}}; for (int i = 0; i < 4; i++) b.out[i] = o[i];
      const Vec2 in[4] = {{0.25f, 0.0f}, {0.75f, 0.0f}, {0.75f, 1.0f}, {0.25f, 1.0f}}; for (int i = 0; i < 4; i++) b.in[i] = in[i]; }
    b.posX = 0.21f; b.posY = -0.13f; b.rotation = 33.5f; b.scale = 1.4f; b.scaleX = 0.8f; b.scaleY = 1.1f;
    b.contentX = 0.07f; b.contentY = -0.2f; b.contentScale = 2.5f; b.contentRotation = -45.0f;
    b.tintR = 0.9f; b.tintG = 0.4f; b.tintB = 0.15f; b.hue = 120.0f; b.saturation = 1.3f; b.contrast = 0.7f; b.brightness = -0.15f; b.feather = 0.35f; b.blend = Blend::Add;
    Surface& c = p.addSurface(Shape::Triangle, vid);                  // id 3
    c.visible = false; c.opacity = 0.25f; c.flipV = true; c.name = "Tri \xC3\xA9\xC3\xA8 \"quoted\"";
    p.selectedSurfaceId = 2;

    Timeline& tl = p.timeline;
    const int t1 = tl.addTrack("surface/2/opacity");
    const int c1 = tl.addClip(t1, 1.0, 2.0, 1.0f);
    tl.findTrack(t1)->findClip(c1)->data->keys = {{0, 1.0f, Interp::Smooth}, {1.0, 0.0f, Interp::Linear}, {2.0, 0.5f, Interp::Hold}};
    tl.duplicateClip(t1, c1, true);                                   // a ghost: shares keyframes with the original
    const int c3 = tl.addClip(t1, 9.0, 4.0, 0.0f);                    // loops: a 2 s cycle in a 4 s clip
    tl.findTrack(t1)->findClip(c3)->data->keys = {{0, 0.0f, Interp::Linear}, {2.0, 1.0f, Interp::Linear}};
    const int t2 = tl.addTrack("surface/3/visible");
    const int c4 = tl.addClip(t2, 2.0, 2.0, 1.0f);
    tl.findTrack(t2)->findClip(c4)->data->keys = {{0, 1.0f, Interp::Hold}, {1.0, 0.0f, Interp::Hold}, {2.0, 1.0f, Interp::Hold}};
    tl.findTrack(t2)->muted = true;

    AudioAsset a1; a1.name = fileNameOf(song); a1.path = song;
    SongBuffer buf; decodeAudioFile(song, buf, err);
    computePeaks(buf, a1.peakMin, a1.peakMax);
    a1.buf = std::make_shared<const SongBuffer>(std::move(buf));
    a1.bpm = 129.37; a1.beatOffset = 0.2;                             // as if corrected by hand
    const int aid = p.addAudioAsset(std::move(a1));
    const Project::Placement pl = p.placeAudio(aid);
    tl.findTrack(pl.trackId)->findAudioClip(pl.clipId)->gain = 0.5f;
    tl.splitAudioClip(pl.trackId, pl.clipId, 10.0);
    tl.addMarker(2.0); tl.addMarker(7.5); tl.addMarker(33.25);
    p.transport.avOffset = 0.037; p.transport.loop = true;
}

int main() {
    const std::string videoSrc = testVideoPath();
    if (videoSrc.empty()) return skipTest("set MAPHAPPY_TEST_VIDEO to a short video file (CI makes one with ffmpeg)");
    GlContext gl; if (!gl.ok) return skipTest("no OpenGL 3.3 context available");

    TempDir tmp("saveload"); const fs::path root = tmp.path();
    setTestEnv("HOME", (root / "home").u8string()); setTestEnv("XDG_CONFIG_HOME", (root / "cfg").u8string()); setTestEnv("APPDATA", (root / "cfg").u8string());
    fs::create_directories(root / "proj" / "media"); fs::create_directories(root / "proj" / "music");
    fs::copy_file(videoSrc, root / "proj" / "media" / "testvideo.mp4");
    writeSyntheticSongFile((root / "proj" / "music" / "song.wav").u8string(), 130, 0.17, 30.0, SongPattern::House);
    const std::string video = pathToUtf8(root / "proj" / "media" / "testvideo.mp4"), song = pathToUtf8(root / "proj" / "music" / "song.wav");
    const std::string file = pathToUtf8(root / "proj" / "show.maphappy");

    // ---------------------------------------------------------------- round trip
    Project p1; buildShow(p1, video, song);
    std::string err;
    expect("saving succeeds and leaves no temp file behind", saveProjectToFile(p1, file, err) && !fs::exists(pathFromUtf8(file + ".tmp")));
    std::string text; readTextFile(file, text);
    const json parsed = json::parse(text, nullptr, false);
    expect("the file is valid, readable JSON", !parsed.is_discarded() && parsed["format"] == "maphappy");
    expect("media paths are also stored relative to the project", parsed["media"][1]["pathRel"] == "media/testvideo.mp4");
    expect("non-English and quoted names survive", text.find("Tri \xC3\xA9\xC3\xA8 \\\"quoted\\\"") != std::string::npos);

    Project p2; LoadReport rep;
    expect("loading succeeds with no warnings", loadProjectFromFile(p2, file, rep) && rep.error.empty() && rep.warnings.empty());
    expect("saving what was loaded gives the identical file", projectToJson(p1, file) == projectToJson(p2, file));
    expect("3 surfaces come back in order with their ids", p2.surfaces.size() == 3 && p2.surfaces[0].id == 3 && p2.surfaces[1].id == 2 && p2.surfaces[2].id == 1);
    expect("surface geometry is exact", nearf(p2.find(2)->out[1].x, 0.90, 1e-6) && nearf(p2.find(2)->in[0].x, 0.25, 1e-6));
    expect("surface settings come back", p2.find(2)->flipH && nearf(p2.find(2)->opacity, 0.6, 1e-6) && !p2.find(3)->visible && p2.find(3)->flipV && p2.selectedSurfaceId == 2);
    expect("the video is reopened and linked to the same surfaces", p2.media.find(p2.find(1)->mediaId) && p2.media.find(p2.find(1)->mediaId)->video() && p2.find(1)->mediaId == p2.find(3)->mediaId);
    expect("...and it decodes", p2.media.find(p2.find(1)->mediaId)->source->width() > 0);
    Track* tr = p2.timeline.findTrackByPath("surface/2/opacity");
    expect("tracks and clips come back", tr && tr->clips.size() == 3 && p2.timeline.tracks.size() == 3);
    expect("a ghost clip is still linked to its original", tr && tr->clips[0].data == tr->clips[1].data && tr->clips[1].ghost && tr->clips[2].data != tr->clips[0].data);
    expect("editing the original still changes the ghost after a reload", tr && (tr->clips[0].data->keys[0].v = 0.77f, tr->clips[1].data->keys[0].v == 0.77f));
    tr->clips[0].data->keys[0].v = 1.0f;
    bool sameEval = true;
    for (const Track& a : p1.timeline.tracks) {
        const Track* b = nullptr; for (const Track& x : p2.timeline.tracks) if (x.path == a.path) b = &x;
        for (double t = 0; t < 16; t += 0.37) { float va = -1, vb = -2; const bool ea = a.evaluate(t, va), eb = b && b->evaluate(t, vb); if (ea != eb || (ea && std::fabs(va - vb) > 1e-6)) sameEval = false; }
    }
    expect("the timeline evaluates identically at 44 sample times on every track", sameEval);
    expect("a muted track stays muted", p2.timeline.findTrackByPath("surface/3/visible")->muted);
    Track* at = nullptr; for (auto& t : p2.timeline.tracks) if (t.kind == TrackKind::Audio) at = &t;
    expect("the audio track, split clips, gain and offsets come back", at && at->audioClips.size() == 2 && nearf(at->audioClips[1].offset, 10.0, 1e-9) && nearf(at->audioClips[0].gain, 0.5, 1e-6));
    expect("a tempo corrected by hand is kept", p2.audioAssets.size() == 1 && nearf(p2.audioAssets[0].bpm, 129.37, 1e-9) && nearf(p2.audioAssets[0].beatOffset, 0.2, 1e-9));
    expect("markers, timeline length and transport settings come back", p2.timeline.markers.size() == 3 && nearf(p2.timeline.markers[2].time, 33.25, 1e-9) && nearf(p2.transport.avOffset, 0.037, 1e-9) && p2.transport.loop);
    expect("the song is queued for background loading", rep.audioToLoad.size() == 1 && p2.audioAssets[0].loading && !p2.audioAssets[0].buf);

    AudioLoader loader; loader.start(p2.audioAssets[0].path, p2.audioAssets[0].id);
    AudioLoader::Result res; bool got = false;
    for (int i = 0; i < 600 && !got; i++) { got = loader.poll(res); if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    expect("the song's sound reloads in the background", got && res.ok && res.assetId == p2.audioAssets[0].id && res.asset.buf && res.asset.buf->frames > 100000);
    expect("reloading does not re-detect the tempo (so a hand correction isn't overwritten)", res.asset.bpm == 0.0);

    Surface& ns = p2.addSurface(Shape::Quad);
    expect("new surfaces get fresh ids after a load", ns.id == 4 && ns.name == "Quad-2");
    const int nt = p2.timeline.addTrack("surface/1/opacity"), nc = p2.timeline.addClip(nt, 0, 1, 1.0f);
    bool clash = false; for (auto& t : p2.timeline.tracks) for (auto& c : t.clips) if (c.id == nc && &t != p2.timeline.findTrack(nt)) clash = true;
    expect("new clips and tracks don't reuse ids", !clash && nc > 6);

    // ---------------------------------------------------------------- the newer parameters
    { Surface* o = p2.find(2);
      expect("transform parameters survive save and load", o && nearf(o->posX, 0.21, 1e-6) && nearf(o->posY, -0.13, 1e-6) && nearf(o->rotation, 33.5, 1e-5) && nearf(o->scale, 1.4, 1e-6) && nearf(o->scaleX, 0.8, 1e-6) && nearf(o->scaleY, 1.1, 1e-6));
      expect("content parameters survive", o && nearf(o->contentX, 0.07, 1e-6) && nearf(o->contentY, -0.2, 1e-6) && nearf(o->contentScale, 2.5, 1e-6) && nearf(o->contentRotation, -45.0, 1e-5));
      expect("colour parameters survive", o && nearf(o->tintR, 0.9, 1e-6) && nearf(o->tintG, 0.4, 1e-6) && nearf(o->tintB, 0.15, 1e-6) && nearf(o->hue, 120.0, 1e-5) && nearf(o->saturation, 1.3, 1e-6) && nearf(o->contrast, 0.7, 1e-6) && nearf(o->brightness, -0.15, 1e-6));
      expect("feather and blend mode survive, the blend by name", o && nearf(o->feather, 0.35, 1e-6) && o->blend == Blend::Add && text.find("\"blend\": \"add\"") != std::string::npos);

      json old = json::parse(projectToJson(p1, file));
      for (auto& sj : old["surfaces"]) { for (const ParamDef& d : paramDefs()) if (std::string(d.key) != "opacity" && std::string(d.key) != "visible") sj.erase(d.key); sj.erase("blend"); }
      Project pOld; LoadReport rOld;
      expect("a project from before those parameters existed still loads", loadProjectFromJson(pOld, old.dump(), pathToUtf8(root / "proj"), rOld) && pOld.surfaces.size() == 3);
      Surface* od = pOld.find(2); const Surface def;
      expect("...with the newer parameters at their defaults and the old ones intact", od && nearf(od->posX, def.posX, 1e-9) && nearf(od->scale, 1.0, 1e-9) && nearf(od->contentScale, 1.0, 1e-9) && nearf(od->tintG, 1.0, 1e-9) && od->blend == Blend::Over && nearf(od->opacity, 0.6, 1e-6) && od->flipH);

      json bad = json::parse(projectToJson(p1, file));
      bad["surfaces"][1]["scale"] = 99.0; bad["surfaces"][1]["rotation"] = -123456.0; bad["surfaces"][1]["contentScale"] = 0.0; bad["surfaces"][1]["blend"] = "plasma"; bad["surfaces"][1]["tintR"] = "red";
      Project pBad; LoadReport rBad;
      expect("absurd parameter values still load", loadProjectFromJson(pBad, bad.dump(), pathToUtf8(root / "proj"), rBad));
      Surface* bs = pBad.find(2);
      expect("...clamped into range, an unknown blend becomes Over, junk becomes the default", bs && nearf(bs->scale, 4.0, 1e-6) && nearf(bs->rotation, -720.0, 1e-6) && nearf(bs->contentScale, 0.1, 1e-6) && bs->blend == Blend::Over && nearf(bs->tintR, 1.0, 1e-6)); }

    // ---------------------------------------------------------------- a moved project still finds its media
    const fs::path moved = root / "elsewhere" / "deep";
    fs::create_directories(moved / "media"); fs::create_directories(moved / "music");
    fs::copy_file(root / "proj" / "media" / "testvideo.mp4", moved / "media" / "testvideo.mp4"); fs::copy_file(root / "proj" / "music" / "song.wav", moved / "music" / "song.wav");
    fs::copy_file(pathFromUtf8(file), moved / "show.maphappy");
    fs::rename(root / "proj" / "media", root / "proj" / "media_gone"); fs::rename(root / "proj" / "music", root / "proj" / "music_gone");     // the original absolute paths are now dead
    { Project p3; LoadReport r3;
      expect("a project moved with its media next to it loads cleanly", loadProjectFromFile(p3, pathToUtf8(moved / "show.maphappy"), r3) && r3.warnings.empty());
      expect("...using the media next to it, not the dead absolute paths", p3.media.find(p3.find(1)->mediaId)->video() != nullptr && r3.audioToLoad.size() == 1 && p3.audioAssets[0].path.find("deep") != std::string::npos); }

    // ---------------------------------------------------------------- missing media
    fs::remove(moved / "media" / "testvideo.mp4"); fs::remove(moved / "music" / "song.wav");
    { Project p4; LoadReport r4;
      expect("a project whose media is gone still opens", loadProjectFromFile(p4, pathToUtf8(moved / "show.maphappy"), r4));
      expect("it warns about the missing video and song, once each", hasWarning(r4, "testvideo.mp4") && hasWarning(r4, "song.wav") && r4.warnings.size() == 2);
      MediaItem* mm = p4.media.find(p4.find(1)->mediaId);
      expect("the missing video is a placeholder that keeps its place", mm && mm->missing && mm->video() == nullptr && mm->source->width() > 0 && p4.find(1)->mediaId == p4.find(3)->mediaId);
      expect("the missing song is flagged and its clips stay", p4.audioAssets[0].missing && r4.audioToLoad.empty() && p4.timeline.tracks.back().audioClips.size() == 2);
      expect("a placeholder survives being saved again (the link isn't lost)", projectToJson(p4, pathToUtf8(moved / "show.maphappy")).find("testvideo.mp4") != std::string::npos);
      fs::copy_file(root / "proj" / "media_gone" / "testvideo.mp4", moved / "relinked.mp4");
      std::string rerr;
      expect("relinking a missing video works", p4.media.relink(mm->id, pathToUtf8(moved / "relinked.mp4"), rerr) && !mm->missing && mm->video() != nullptr);
      expect("relinking to a bad file fails cleanly and changes nothing", !p4.media.relink(mm->id, pathToUtf8(moved / "nope.mp4"), rerr) && !rerr.empty() && !mm->missing); }

    // ---------------------------------------------------------------- damaged files leave the open project alone
    { Project p5; buildShow(p5, pathToUtf8(root / "proj" / "media_gone" / "testvideo.mp4"), pathToUtf8(root / "proj" / "music_gone" / "song.wav"));
      const std::string before = projectToJson(p5, ""); LoadReport bad;
      expect("garbage is rejected", !loadProjectFromJson(p5, "this is not json {{{", "", bad) && !bad.error.empty());
      expect("JSON that isn't a project is rejected", !loadProjectFromJson(p5, "{\"hello\": 1}", "", bad));
      expect("an empty file is rejected", !loadProjectFromJson(p5, "", "", bad));
      expect("a project from a newer version is rejected with a clear message", !loadProjectFromJson(p5, "{\"format\":\"maphappy\",\"version\":99}", "", bad) && bad.error.find("newer") != std::string::npos);
      expect("a missing file is reported", !loadProjectFromFile(p5, pathToUtf8(root / "none.maphappy"), bad));
      expect("after all those failures the open project is untouched", projectToJson(p5, "") == before); }

    // ---------------------------------------------------------------- hand-edited or corrupted content is repaired, not trusted
    { json j = json::parse(projectToJson(p1, file));
      j["surfaces"][0]["out"] = json::array({json::array({0.1, 0.1}), json::array({0.9, 0.9}), json::array({0.9, 0.1}), json::array({0.1, 0.9})});    // a bow-tie
      j["surfaces"][1]["media"] = 999;                                              // media that doesn't exist
      j["surfaces"][2]["id"] = j["surfaces"][0]["id"];                              // a duplicate id
      j["timeline"]["tracks"][0]["clips"][1]["start"] = j["timeline"]["tracks"][0]["clips"][0]["start"];     // overlapping clips
      std::reverse(j["timeline"]["tracks"][0]["clipData"][0]["keys"].begin(), j["timeline"]["tracks"][0]["clipData"][0]["keys"].end());   // unsorted keyframes
      j["timeline"]["tracks"][2]["audioClips"].push_back({{"id", 500}, {"asset", 12345}, {"start", 0}, {"duration", 1}});                     // a clip for a song that isn't there
      j["unknownFutureField"] = {{"a", 1}};
      Project p6; LoadReport r6;
      expect("a damaged project still loads", loadProjectFromJson(p6, j.dump(), pathToUtf8(root / "proj"), r6));
      expect("a bow-tie quad was reset, with a warning (surfaces are listed front-most first, so this is #3)", [&] { Surface* s = p6.find(3); return s && isConvex(s->out) && nearf(s->out[0].x, Surface().out[0].x, 1e-6); }() && hasWarning(r6, "unusable output shape"));
      expect("media that doesn't exist falls back to the test pattern", p6.find(2) && p6.find(2)->mediaId == MediaPool::kPatternId);
      expect("a duplicate surface id was skipped", p6.surfaces.size() == 2);
      expect("overlapping clips were moved apart", [&] { Track* t = p6.timeline.findTrackByPath("surface/2/opacity"); for (size_t i = 1; i < t->clips.size(); i++) if (t->clips[i].start < t->clips[i - 1].end() - 1e-9) return false; return true; }());
      expect("unsorted keyframes were sorted", [&] { auto& k = p6.timeline.findTrackByPath("surface/2/opacity")->clips[0].data->keys; for (size_t i = 1; i < k.size(); i++) if (k[i].t < k[i - 1].t) return false; return true; }());
      expect("a clip for a missing song was dropped", [&] { for (auto& t : p6.timeline.tracks) for (auto& c : t.audioClips) if (c.assetId == 12345) return false; return true; }());
      float v; expect("each repair produced a warning and the repaired timeline still evaluates", r6.warnings.size() >= 5 && p6.timeline.findTrackByPath("surface/2/opacity")->evaluate(5.0, v)); }

    // ---------------------------------------------------------------- the 'unsaved changes' check
    { Project p7; buildShow(p7, pathToUtf8(root / "proj" / "media_gone" / "testvideo.mp4"), pathToUtf8(root / "proj" / "music_gone" / "song.wav"));
      std::string base = projectToJson(p7, file, true);
      expect("the dirty-check snapshot is stable", projectToJson(p7, file, true) == base);
      p7.find(2)->opacity = 0.123f; p7.find(3)->visible = true;                     // both are driven by timeline tracks
      expect("playing a show (timeline-driven values changing) is not an unsaved change", projectToJson(p7, file, true) == base);
      p7.timeline.addTrack("surface/1/posX"); p7.timeline.addClip(p7.timeline.findTrackByPath("surface/1/posX")->id, 0, 2, 0.0f);
      const std::string withTrack = projectToJson(p7, file, true);
      p7.find(1)->posX = 0.3f; p7.find(1)->rotation = 20.0f;
      expect("a newer parameter being driven isn't an unsaved change, but an undriven one is", projectToJson(p7, file, true) != withTrack);
      p7.find(1)->rotation = 0.0f;
      expect("...driven posX changing alone is ignored", projectToJson(p7, file, true) == withTrack);
      p7.timeline.removeTrack(p7.timeline.findTrackByPath("surface/1/posX")->id); p7.find(1)->posX = 0.0f; base = projectToJson(p7, file, true);
      p7.find(1)->opacity = 0.5f;
      expect("changing a value nothing animates IS an unsaved change", projectToJson(p7, file, true) != base);
      p7.find(1)->opacity = 1.0f; p7.timeline.addMarker(40.0);
      expect("adding a marker is an unsaved change", projectToJson(p7, file, true) != base); }

    // ---------------------------------------------------------------- settings and autosave
    { AppSettings s; addRecentFile(s, "/a.maphappy"); addRecentFile(s, "/b.maphappy"); addRecentFile(s, "/a.maphappy"); s.lastFolder = "/some/dir";
      expect("recent files: newest first, no duplicates", s.recent.size() == 2 && s.recent[0] == "/a.maphappy" && s.recent[1] == "/b.maphappy");
      for (int i = 0; i < 15; i++) addRecentFile(s, "/f" + std::to_string(i));
      expect("recent files are capped at 10", s.recent.size() == 10);
      saveSettings(s); const AppSettings s2 = loadSettings();
      expect("settings persist between runs", s2.recent == s.recent && s2.lastFolder == "/some/dir");
      const std::string proj = pathToUtf8(root / "x.maphappy");
      expect("there is no autosave to start with", !autosaveIsNewer(proj));
      { std::ofstream(pathFromUtf8(proj)) << "{}"; }
      std::this_thread::sleep_for(std::chrono::milliseconds(60));
      { std::ofstream(pathFromUtf8(autosavePathFor(proj))) << "{}"; }
      expect("a newer autosave is detected", autosaveIsNewer(proj));
      std::this_thread::sleep_for(std::chrono::milliseconds(60));
      { std::ofstream(pathFromUtf8(proj)) << "{}"; }
      expect("an autosave older than the saved file is ignored", !autosaveIsNewer(proj));
      deleteAutosave(proj);
      expect("deleting an autosave removes it", !fs::exists(pathFromUtf8(autosavePathFor(proj))));
      expect("untitled projects autosave into the app data folder", autosavePathFor("").find("untitled.autosave") != std::string::npos); }

    expect("saving over an existing file works, and the result loads", saveProjectToFile(p1, file, err) && saveProjectToFile(p1, file, err) && [&] { Project p8; LoadReport r8; return loadProjectFromFile(p8, file, r8); }());
    expect("saving into a folder that doesn't exist fails with a message, not a crash", !saveProjectToFile(p1, pathToUtf8(root / "no" / "such" / "dir" / "x.maphappy"), err) && !err.empty());
    return finishTest();
}
