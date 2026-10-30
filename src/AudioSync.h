#pragma once
#include "AudioMixer.h"
#include "Project.h"

// Builds the audio thread's view of the project: every unmuted audio clip, with its decoded sound.
inline std::shared_ptr<MixSnapshot> buildMixSnapshot(Project& p) {
    auto snap = std::make_shared<MixSnapshot>();
    for (const Track& t : p.timeline.tracks) {
        if (t.kind != TrackKind::Audio || t.muted) continue;
        for (const AudioClip& c : t.audioClips) {
            AudioAsset* a = p.findAsset(c.assetId);
            if (!a || !a->buf) continue;
            MixClip m;
            m.buf = a->buf; m.start = c.start; m.offset = c.offset; m.duration = c.duration; m.gain = c.gain;
            snap->push_back(m);
        }
    }
    return snap;
}

// Call every frame. Only hands the audio thread a new list when something actually changed.
inline void syncMixSnapshot(Project& p, AudioMixer& mixer, std::shared_ptr<const MixSnapshot>& last) {
    auto snap = buildMixSnapshot(p);
    if (last && *last == *snap) return;
    last = snap;
    mixer.setSnapshot(snap);
}

// Decides who owns "now". With a sound device, the AUDIO clock is the master: the playhead follows
// the sound, so picture can never drift away from the music. Without one, a plain timer is used.
inline void driveTransport(Transport& tr, AudioMixer* mixer, double dt, double length) {
    if (!mixer) { tr.seeked = false; tr.update(dt, length); return; }

    const double off = tr.avOffset;
    if (tr.seeked) { mixer->seek(tr.time + off); tr.seeked = false; }
    if (tr.playing && !mixer->playing())      { mixer->seek(tr.time + off); mixer->play(); }   // always start from the playhead
    else if (!tr.playing && mixer->playing()) { mixer->pause(); }

    if (tr.playing && !mixer->seekPending()) {
        double t = mixer->timeSmooth() - off;
        if (t > tr.time || t < tr.time - 0.05) tr.time = std::max(0.0, t);      // ignore tiny backward jitter
    }
    if (tr.playing && tr.time >= length) {
        if (tr.loop && length > 0.0) { tr.time = 0.0; mixer->seek(off); }
        else { tr.time = length; tr.playing = false; mixer->pause(); }
    }
}
