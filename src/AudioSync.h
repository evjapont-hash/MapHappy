#pragma once
#include "AudioMixer.h"
#include "Project.h"
#include "TransportClock.h"

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
