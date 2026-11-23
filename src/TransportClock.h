#pragma once
#include "AudioMixer.h"
#include "Timeline.h"
#include <algorithm>

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
