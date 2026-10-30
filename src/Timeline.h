#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// The timeline engine. No OpenGL and no UI in here, so it can be tested on its own.
//
//   Timeline -> Track (one per animated parameter) -> Clip -> Keyframes
//
// A clip's keyframes live in a ClipData that can be SHARED between clips. A "ghost" copy
// shares its data with the original, so editing one edits all. A duplicate gets its own copy.
// ---------------------------------------------------------------------------

constexpr int kTimelineFps = 60;      // frame grid used for snapping and the timecode display

enum class Interp { Linear = 0, Smooth = 1, Hold = 2 };    // how the value travels to the NEXT keyframe

struct Keyframe {
    double t = 0.0;                   // seconds from the start of the clip
    float  v = 0.0f;
    Interp interp = Interp::Smooth;
};

struct ClipData {
    std::vector<Keyframe> keys;       // sorted by t
    double length() const { return keys.empty() ? 0.0 : keys.back().t; }    // one loop cycle

    float valueAt(double t) const {
        if (keys.empty()) return 0.0f;
        if (t <= keys.front().t) return keys.front().v;
        if (t >= keys.back().t)  return keys.back().v;
        size_t i = 1;
        while (keys[i].t < t) i++;
        const Keyframe& a = keys[i - 1];
        const Keyframe& b = keys[i];
        double span = b.t - a.t;
        if (span <= 1e-12) return b.v;
        float u = (float)((t - a.t) / span);
        switch (a.interp) {
            case Interp::Hold:   return a.v;
            case Interp::Smooth: u = u * u * (3.0f - 2.0f * u); break;
            default: break;
        }
        return a.v + (b.v - a.v) * u;
    }
};

struct Clip {
    int         id = 0;
    std::string name;
    double      start = 0.0;          // seconds on the timeline
    double      duration = 2.0;       // longer than one cycle of keyframes = the animation loops
    bool        ghost = false;        // display only: this clip shares its ClipData with another
    std::shared_ptr<ClipData> data;

    double end() const { return start + duration; }

    // Value `local` seconds after the clip starts. Holds the end value once the clip is over.
    float valueAtLocal(double local) const {
        if (!data || data->keys.empty()) return 0.0f;
        local = std::max(0.0, std::min(local, duration));
        double len = data->length();
        if (len > 1e-9 && local > len) {
            double m = std::fmod(local, len);
            local = (m < 1e-9) ? len : m;      // landing exactly on a cycle boundary = end of the cycle
        }
        return data->valueAt(local);
    }
};

enum class TrackKind { Param, Audio };

// A piece of a song placed on the timeline. The sound itself lives in the project's audio assets.
struct AudioClip {
    int         id = 0;
    std::string name;
    int         assetId = 0;
    double      start = 0.0;          // timeline seconds
    double      duration = 0.0;       // seconds of the file that play
    double      offset = 0.0;         // seconds skipped at the start of the file
    float       gain = 1.0f;
    double end() const { return start + duration; }
};

struct Marker { int id = 0; double time = 0.0; };

struct Track {
    int         id = 0;
    TrackKind   kind = TrackKind::Param;
    std::string name;                 // shown for audio tracks ("Audio-1")
    std::string path;                 // which parameter this track drives, e.g. "surface/3/opacity" (audio: "audio/1")
    bool        muted = false;
    std::vector<Clip> clips;          // parameter clips: sorted by start, never overlapping
    std::vector<AudioClip> audioClips;  // audio clips: sorted by start, never overlapping

    AudioClip* findAudioClip(int clipId) {
        for (auto& c : audioClips) if (c.id == clipId) return &c;
        return nullptr;
    }
    int indexOfAudioClip(int clipId) const {
        for (size_t i = 0; i < audioClips.size(); i++) if (audioClips[i].id == clipId) return (int)i;
        return -1;
    }
    double minStartForAudio(int clipId) const {
        int i = indexOfAudioClip(clipId);
        return (i > 0) ? audioClips[i - 1].end() : 0.0;
    }
    double maxEndForAudio(int clipId) const {
        int i = indexOfAudioClip(clipId);
        return (i >= 0 && i + 1 < (int)audioClips.size()) ? audioClips[i + 1].start : 1e18;
    }

    Clip* findClip(int clipId) {
        for (auto& c : clips) if (c.id == clipId) return &c;
        return nullptr;
    }
    int indexOfClip(int clipId) const {
        for (size_t i = 0; i < clips.size(); i++) if (clips[i].id == clipId) return (int)i;
        return -1;
    }
    double minStartFor(int clipId) const {      // clip can't slide left past the previous clip's end
        int i = indexOfClip(clipId);
        return (i > 0) ? clips[i - 1].end() : 0.0;
    }
    double maxEndFor(int clipId) const {        // ...or right past the next clip's start
        int i = indexOfClip(clipId);
        return (i >= 0 && i + 1 < (int)clips.size()) ? clips[i + 1].start : 1e18;
    }

    // Deterministic: the same time always gives the same value, however you got there.
    //   before the first clip -> the first clip's starting value
    //   inside a clip         -> the animated value (looping if the clip is longer than its keys)
    //   between/after clips   -> the previous clip's final value, held
    // Returns false only if the track has no clips at all.
    bool evaluate(double time, float& out) const {
        if (kind != TrackKind::Param || clips.empty()) return false;
        const Clip* cur = &clips.front();
        for (const Clip& c : clips) { if (c.start <= time) cur = &c; else break; }
        out = cur->valueAtLocal(time - cur->start);
        return true;
    }
};

struct Transport {
    double time = 0.0;
    bool   playing = false;
    bool   loop = false;
    bool   seeked = false;            // set by the UI when it moves the playhead; the audio clock follows
    double avOffset = 0.0;            // seconds: audio clock = visual clock + avOffset (to line up sound and picture)

    void update(double dt, double length) {
        if (!playing) return;
        time += dt;
        if (time >= length) {
            if (loop && length > 0.0) time = std::fmod(time, length);
            else { time = length; playing = false; }
        }
    }
};

inline double snapToFrame(double t) { return std::round(t * kTimelineFps) / kTimelineFps; }

// "h:mm:ss:ff" with ff counted in timeline frames
inline std::string formatTimecode(double t, int fps = kTimelineFps) {
    if (t < 0) t = 0;
    long long frames = (long long)std::llround(t * fps);
    long long ff = frames % fps, secs = frames / fps;
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld:%02lld", secs / 3600, (secs / 60) % 60, secs % 60, ff);
    return buf;
}

class Timeline {
public:
    std::vector<Track> tracks;
    std::vector<Marker> markers;      // sorted by time; things snap to them
    double length = 60.0;             // seconds; grows as clips are added past the end

    Track* findTrack(int id) {
        for (auto& t : tracks) if (t.id == id) return &t;
        return nullptr;
    }
    Track* findTrackByPath(const std::string& path) {
        for (auto& t : tracks) if (t.path == path) return &t;
        return nullptr;
    }

    // One track per parameter. Returns the existing track's id if there already is one.
    int addTrack(const std::string& path) {
        if (Track* t = findTrackByPath(path)) return t->id;
        Track t; t.id = nextTrackId_++; t.path = path;
        tracks.push_back(std::move(t));
        return tracks.back().id;
    }
    void removeTrack(int id) {
        tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [id](const Track& t) { return t.id == id; }), tracks.end());
    }
    void removeTracksWithPrefix(const std::string& prefix) {
        tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
                     [&](const Track& t) { return t.path.compare(0, prefix.size(), prefix) == 0; }), tracks.end());
    }

    void extendTo(double t) { length = std::max(length, t + 5.0); }

    // ---- audio
    int addAudioTrack() {
        Track t; t.id = nextTrackId_++; t.kind = TrackKind::Audio;
        t.name = "Audio-" + std::to_string(++audioTrackCount_);
        t.path = "audio/" + std::to_string(t.id);
        tracks.push_back(std::move(t));
        return tracks.back().id;
    }
    // Places a clip of an audio asset. Returns its id, or 0 if it would overlap another clip.
    int addAudioClip(int trackId, int assetId, double start, double duration, double offset = 0.0) {
        Track* tr = findTrack(trackId);
        if (!tr || tr->kind != TrackKind::Audio || duration < 1.0 / kTimelineFps) return 0;
        start = std::max(0.0, start);
        for (const AudioClip& c : tr->audioClips)
            if (start < c.end() - 1e-9 && start + duration > c.start + 1e-9) return 0;
        AudioClip c;
        c.id = nextClipId_++;
        c.name = "Audio-clip-" + std::to_string(c.id);
        c.assetId = assetId; c.start = start; c.duration = duration; c.offset = std::max(0.0, offset);
        auto it = std::upper_bound(tr->audioClips.begin(), tr->audioClips.end(), c.start, [](double s, const AudioClip& x) { return s < x.start; });
        tr->audioClips.insert(it, c);
        extendTo(start + duration);
        return c.id;
    }
    void removeAudioClip(int trackId, int clipId) {
        if (Track* tr = findTrack(trackId))
            tr->audioClips.erase(std::remove_if(tr->audioClips.begin(), tr->audioClips.end(), [clipId](const AudioClip& c) { return c.id == clipId; }), tr->audioClips.end());
    }
    static double freeAudioStart(const Track& tr, double desired, double duration) {
        double s = desired;
        bool moved = true;
        while (moved) {
            moved = false;
            for (const AudioClip& c : tr.audioClips)
                if (s < c.end() - 1e-9 && s + duration > c.start + 1e-9) { s = c.end(); moved = true; }
        }
        return s;
    }
    // Cuts an audio clip in two at a timeline time. Returns the new (right-hand) clip's id, or 0.
    int splitAudioClip(int trackId, int clipId, double t) {
        Track* tr = findTrack(trackId);
        AudioClip* c = tr ? tr->findAudioClip(clipId) : nullptr;
        if (!c || t <= c->start + 1.0 / kTimelineFps || t >= c->end() - 1.0 / kTimelineFps) return 0;
        AudioClip right = *c;
        right.id = nextClipId_++;
        right.name = "Audio-clip-" + std::to_string(right.id);
        right.start = t;
        right.offset = c->offset + (t - c->start);
        right.duration = c->end() - t;
        c->duration = t - c->start;
        auto it = std::upper_bound(tr->audioClips.begin(), tr->audioClips.end(), right.start, [](double s, const AudioClip& x) { return s < x.start; });
        tr->audioClips.insert(it, right);
        return right.id;
    }

    // ---- markers
    int addMarker(double t) {
        for (const Marker& m : markers) if (std::fabs(m.time - t) < 1.0 / kTimelineFps / 2) return m.id;     // already one here
        Marker m; m.id = nextMarkerId_++; m.time = std::max(0.0, t);
        auto it = std::upper_bound(markers.begin(), markers.end(), m.time, [](double s, const Marker& x) { return s < x.time; });
        markers.insert(it, m);
        return m.id;
    }
    void removeMarker(int id) {
        markers.erase(std::remove_if(markers.begin(), markers.end(), [id](const Marker& m) { return m.id == id; }), markers.end());
    }

    // New clip with two keyframes at `value`. Returns its id, or 0 if there is no room.
    int addClip(int trackId, double start, double duration, float value) {
        Track* tr = findTrack(trackId);
        if (!tr || tr->kind != TrackKind::Param || duration < 1.0 / kTimelineFps) return 0;
        start = std::max(0.0, start);
        for (const Clip& c : tr->clips)
            if (start < c.end() - 1e-9 && start + duration > c.start + 1e-9) return 0;     // would overlap
        Clip c;
        c.id = nextClipId_++;
        c.name = "Clip-" + std::to_string(c.id);
        c.start = start;
        c.duration = duration;
        c.data = std::make_shared<ClipData>();
        c.data->keys = {{0.0, value, Interp::Smooth}, {duration, value, Interp::Smooth}};
        insertSorted(*tr, std::move(c));
        extendTo(start + duration);
        return nextClipId_ - 1;
    }

    // Copies a clip into the next free spot after it. A ghost copy shares the original's keyframes.
    int duplicateClip(int trackId, int clipId, bool ghost) {
        Track* tr = findTrack(trackId);
        if (!tr || tr->kind != TrackKind::Param) return 0;
        Clip* src = tr->findClip(clipId);
        if (!src) return 0;
        Clip c = *src;
        c.id = nextClipId_++;
        c.name = "Clip-" + std::to_string(c.id);
        c.ghost = ghost;
        c.data = ghost ? src->data : std::make_shared<ClipData>(*src->data);
        c.start = freeStart(*tr, src->end(), src->duration);
        double end = c.end();
        insertSorted(*tr, std::move(c));
        extendTo(end);
        return nextClipId_ - 1;
    }

    void removeClip(int trackId, int clipId) {
        if (Track* tr = findTrack(trackId))
            tr->clips.erase(std::remove_if(tr->clips.begin(), tr->clips.end(), [clipId](const Clip& c) { return c.id == clipId; }), tr->clips.end());
    }

    // Earliest start >= desired where a clip of this duration fits.
    static double freeStart(const Track& tr, double desired, double duration) {
        double s = desired;
        bool moved = true;
        while (moved) {
            moved = false;
            for (const Clip& c : tr.clips)
                if (s < c.end() - 1e-9 && s + duration > c.start + 1e-9) { s = c.end(); moved = true; }
        }
        return s;
    }

private:
    static void insertSorted(Track& tr, Clip&& c) {
        auto it = std::upper_bound(tr.clips.begin(), tr.clips.end(), c.start, [](double s, const Clip& x) { return s < x.start; });
        tr.clips.insert(it, std::move(c));
    }
    int nextTrackId_ = 1, nextClipId_ = 1, nextMarkerId_ = 1, audioTrackCount_ = 0;
};
