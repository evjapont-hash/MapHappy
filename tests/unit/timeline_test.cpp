// The timeline engine: interpolation, looping, ghost clips, audio clips, markers, transport, timecode.
#include "TestSupport.h"
#include "Timeline.h"

int main() {
    Timeline tl;
    const int tid = tl.addTrack("surface/1/opacity");
    expect("a track is created once per parameter path", tl.addTrack("surface/1/opacity") == tid && tl.tracks.size() == 1);
    Track& tr = *tl.findTrack(tid);
    float v = -1;
    expect("an empty track has no value", !tr.evaluate(0.0, v));

    // ---- interpolation and holding
    const int a = tl.addClip(tid, 1.0, 2.0, 1.0f);
    tr.findClip(a)->data->keys = {{0.0, 1.0f, Interp::Linear}, {1.0, 0.0f, Interp::Linear}, {2.0, 0.5f, Interp::Hold}};
    expect("before the first clip the track holds the first clip's starting value", tr.evaluate(0.0, v) && nearf(v, 1.0f, 1e-5));
    expect("linear interpolation: halfway down", tr.evaluate(1.5, v) && nearf(v, 0.5f, 1e-5));
    expect("linear interpolation reaches the keyframe", tr.evaluate(2.0, v) && nearf(v, 0.0f, 1e-5));
    expect("the last keyframe's value at the clip's end", tr.evaluate(3.0, v) && nearf(v, 0.5f, 1e-5));
    expect("the value is held after the last clip", tr.evaluate(99.0, v) && nearf(v, 0.5f, 1e-5));

    const int b = tl.addClip(tid, 5.0, 2.0, 0.0f);
    tr.findClip(b)->data->keys = {{0.0, 0.0f, Interp::Smooth}, {2.0, 1.0f, Interp::Smooth}};
    expect("a gap between clips holds the previous clip's end value", tr.evaluate(4.0, v) && nearf(v, 0.5f, 1e-5));
    expect("smooth interpolation: the midpoint is the midpoint", tr.evaluate(6.0, v) && nearf(v, 0.5f, 1e-5));
    expect("smooth interpolation eases in (below linear at a quarter)", tr.evaluate(5.5, v) && v < 0.25f && v > 0.05f);
    tr.findClip(b)->data->keys = {{0.0, 0.2f, Interp::Hold}, {2.0, 0.9f, Interp::Hold}};
    expect("hold stays on the left value until the next keyframe", tr.evaluate(6.9, v) && nearf(v, 0.2f, 1e-5));

    // ---- looping: a clip longer than its keyframes repeats them
    const int c = tl.addClip(tid, 10.0, 4.0, 0.0f);
    tr.findClip(c)->data->keys = {{0.0, 0.0f, Interp::Linear}, {2.0, 1.0f, Interp::Linear}};
    expect("loop: first cycle", tr.evaluate(11.0, v) && nearf(v, 0.5f, 1e-5));
    expect("loop: second cycle repeats the first", tr.evaluate(13.0, v) && nearf(v, 0.5f, 1e-5));
    expect("loop: a cycle boundary restarts the curve", tr.evaluate(12.0 + 1e-6, v) && v < 0.01f);
    expect("loop: the end of the clip holds the end of the last cycle", tr.evaluate(14.0, v) && nearf(v, 1.0f, 1e-5));

    // ---- overlap rules
    expect("a clip may not overlap another", tl.addClip(tid, 1.5, 1.0, 0.0f) == 0);
    expect("a clip can sit exactly against another", tl.addClip(tid, 3.0, 1.0, 0.0f) > 0);
    expect("minStartFor is the previous clip's end", nearf(tr.minStartFor(b), 4.0));
    expect("maxEndFor is the next clip's start", nearf(tr.maxEndFor(b), 10.0));

    // ---- duplicate versus ghost
    Timeline t2; const int t2id = t2.addTrack("p"); Track& r2 = *t2.findTrack(t2id);
    const int o = t2.addClip(t2id, 0.0, 2.0, 1.0f);
    const int dup = t2.duplicateClip(t2id, o, false), gho = t2.duplicateClip(t2id, o, true);
    Clip* co = r2.findClip(o); Clip* cd = r2.findClip(dup); Clip* cg = r2.findClip(gho);
    expect("a duplicate owns its own keyframes", cd && cd->data != co->data && !cd->ghost);
    expect("a ghost shares the original's keyframes and is flagged", cg && cg->data == co->data && cg->ghost);
    co->data->keys[1].v = 0.9f;
    expect("editing the original changes its ghost", nearf(cg->data->keys[1].v, 0.9f, 1e-6));
    expect("...but not a duplicate", nearf(cd->data->keys[1].v, 1.0f, 1e-6));
    bool sorted = true, apart = true;
    for (size_t i = 0; i + 1 < r2.clips.size(); i++) { if (r2.clips[i].start > r2.clips[i + 1].start) sorted = false; if (r2.clips[i].end() > r2.clips[i + 1].start + 1e-9) apart = false; }
    expect("copies are placed in free space and the clips stay sorted", sorted && apart);
    t2.removeClip(t2id, dup);
    expect("a clip can be removed", r2.findClip(dup) == nullptr && r2.findClip(gho) != nullptr);

    // ---- removing tracks
    Timeline t3; t3.addTrack("surface/1/opacity"); t3.addTrack("surface/12/opacity"); t3.addTrack("surface/2/opacity");
    t3.removeTracksWithPrefix("surface/1/");
    expect("removing by prefix is exact (surface/1/ leaves surface/12/)", t3.tracks.size() == 2 && t3.findTrackByPath("surface/12/opacity") && t3.findTrackByPath("surface/2/opacity"));
    Timeline t4; const int t4id = t4.addTrack("p"); t4.addClip(t4id, 100.0, 5.0, 0.0f);
    expect("the timeline grows to fit a clip beyond its end", t4.length >= 105.0);

    // ---- audio tracks and clips
    Timeline t5; const int at = t5.addAudioTrack(), at2 = t5.addAudioTrack();
    expect("audio tracks get names and unique paths", t5.findTrack(at)->name == "Audio-1" && t5.findTrack(at2)->name == "Audio-2" && t5.findTrack(at)->path != t5.findTrack(at2)->path);
    expect("an audio track has no parameter value", [&] { float x; return !t5.findTrack(at)->evaluate(1.0, x); }());
    const int ac = t5.addAudioClip(at, 7, 2.0, 10.0);
    expect("an audio clip can be placed", ac > 0 && t5.length >= 12.0);
    expect("audio clips may not overlap", t5.addAudioClip(at, 7, 5.0, 10.0) == 0 && t5.addAudioClip(at, 7, 0.0, 2.5) == 0);
    expect("a parameter clip can't go on an audio track", t5.addClip(at, 0.0, 1.0, 0.0f) == 0 && t5.duplicateClip(at, ac, false) == 0);
    expect("freeAudioStart skips past an existing clip", nearf(Timeline::freeAudioStart(*t5.findTrack(at), 3.0, 1.0), 12.0));
    const int right = t5.splitAudioClip(at, ac, 6.0);
    Track* atr = t5.findTrack(at); AudioClip* L = atr->findAudioClip(ac); AudioClip* R = atr->findAudioClip(right);
    expect("splitting makes a right-hand clip", right > 0 && L && R && atr->audioClips.size() == 2);
    expect("the halves are contiguous", nearf(L->end(), R->start) && nearf(L->duration, 4.0) && nearf(R->duration, 6.0));
    expect("the right half starts further into the file by the cut position", nearf(R->offset, 4.0));
    expect("splitting at a clip's edge is refused", t5.splitAudioClip(at, ac, 2.0) == 0);
    t5.removeAudioClip(at, right);
    expect("an audio clip can be removed", atr->audioClips.size() == 1);

    // ---- markers
    const int m2 = t5.addMarker(5.0); t5.addMarker(1.0); const int m3 = t5.addMarker(3.0);
    expect("markers stay sorted by time", t5.markers.size() == 3 && t5.markers[0].time < t5.markers[1].time && t5.markers[1].time < t5.markers[2].time);
    expect("a second marker on the same spot isn't added", t5.addMarker(3.0) == m3 && t5.markers.size() == 3);
    t5.removeMarker(m2);
    expect("a marker can be removed", t5.markers.size() == 2);

    // ---- transport
    Transport tp; tp.playing = true; tp.update(0.5, 10.0);
    expect("the transport advances", nearf(tp.time, 0.5) && tp.playing);
    tp.update(20.0, 10.0);
    expect("the transport stops at the end", nearf(tp.time, 10.0) && !tp.playing);
    Transport lp; lp.playing = true; lp.loop = true; lp.time = 9.9; lp.update(0.3, 10.0);
    expect("the transport loops", lp.playing && nearf(lp.time, 0.2, 1e-3));

    // ---- time display
    expect("timecode is h:mm:ss:ff at 60 fps", formatTimecode(0) == "0:00:00:00" && formatTimecode(61.5) == "0:01:01:30" && formatTimecode(3661.0 + 1.0 / 60) == "1:01:01:01");
    expect("times snap to the frame grid", nearf(snapToFrame(0.0123), 1.0 / 60.0, 1e-9));
    return finishTest();
}
