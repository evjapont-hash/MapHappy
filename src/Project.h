#pragma once
#include "AudioTypes.h"
#include "MediaPool.h"
#include "Surface.h"
#include "Timeline.h"
#include <algorithm>
#include <string>
#include <vector>

// Everything that makes up a show's visuals. This is what we will save and load.
struct Project {
    MediaPool            media;
    std::vector<Surface> surfaces;            // index 0 = front-most, like the list in the UI
    int selectedSurfaceId = 0;                // 0 = nothing selected
    int outW = 1920, outH = 1080;             // stage (projector) resolution

    Timeline  timeline;                       // part of the show: tracks, clips, keyframes
    Transport transport;                      // runtime playback state (not saved)
    bool      timelineDirty = false;          // set when the playhead moves or the timeline is edited

    std::vector<AudioAsset> audioAssets;      // decoded songs; audio clips refer to them by id
    std::string audioStatus;                  // "Loading ..." / error text shown in the Conductor (not saved)
    bool        audioStatusIsError = false;

    AudioAsset* findAsset(int id) {
        for (auto& a : audioAssets) if (a.id == id) return &a;
        return nullptr;
    }
    int addAudioAsset(AudioAsset&& a) {
        a.id = nextAssetId_++;
        audioAssets.push_back(std::move(a));
        return audioAssets.back().id;
    }
    struct Placement { int trackId = 0, clipId = 0; };
    // Puts a whole song on the first audio track (made if needed), at the playhead or the next free spot.
    Placement placeAudio(int assetId) {
        Placement p;
        AudioAsset* a = findAsset(assetId);
        if (!a) return p;
        Track* tr = nullptr;
        for (auto& t : timeline.tracks) if (t.kind == TrackKind::Audio) { tr = &t; break; }
        if (!tr) tr = timeline.findTrack(timeline.addAudioTrack());
        double start = Timeline::freeAudioStart(*tr, transport.time, a->duration());
        p.trackId = tr->id;
        p.clipId = timeline.addAudioClip(tr->id, assetId, start, a->duration(), 0.0);
        return p;
    }

    Surface* find(int id) {
        for (auto& s : surfaces) if (s.id == id) return &s;
        return nullptr;
    }
    Surface* selected() { return find(selectedSurfaceId); }

    int indexOf(int id) const {
        for (size_t i = 0; i < surfaces.size(); i++) if (surfaces[i].id == id) return (int)i;
        return -1;
    }

    Surface& addSurface(Shape shape, int mediaId = MediaPool::kPatternId) {
        Surface s;
        s.id = nextSurfaceId_++;
        s.shape = shape;
        s.mediaId = media.find(mediaId) ? mediaId : MediaPool::kPatternId;
        s.name = std::string(shapeName(shape)) + "-" + std::to_string(++shapeCounter_[(int)shape]);
        resetInput(s);
        // Stagger new surfaces so they don't land exactly on top of each other.
        float shift = 0.04f * (float)(surfaces.size() % 5) - 0.08f;
        for (int i = 0; i < 4; i++) { s.out[i].x += shift; s.out[i].y += shift; }
        surfaces.insert(surfaces.begin(), s);
        selectedSurfaceId = s.id;
        return surfaces.front();
    }

    // Default input region: the whole media for a quad, or a centered square for circles and
    // triangles, so a circle samples a round area instead of a stretched oval.
    void resetInput(Surface& s) const {
        float hx = 0.5f, hy = 0.5f;
        if (s.shape != Shape::Quad) {
            const MediaItem* m = media.find(s.mediaId);
            float a = m ? m->aspect() : 16.0f / 9.0f;
            if (a >= 1.0f) hx = 0.5f / a; else hy = 0.5f * a;
        }
        s.in[0] = {0.5f - hx, 0.5f - hy}; s.in[1] = {0.5f + hx, 0.5f - hy};
        s.in[2] = {0.5f + hx, 0.5f + hy}; s.in[3] = {0.5f - hx, 0.5f + hy};
    }

    void removeSurface(int id) {
        int i = indexOf(id);
        if (i < 0) return;
        surfaces.erase(surfaces.begin() + i);
        timeline.removeTracksWithPrefix("surface/" + std::to_string(id) + "/");
        if (selectedSurfaceId == id) selectedSurfaceId = surfaces.empty() ? 0 : surfaces[std::min<size_t>(i, surfaces.size() - 1)].id;
    }

    void moveSurface(int id, int delta) {      // delta -1 = toward the front, +1 = toward the back
        int i = indexOf(id), j = i + delta;
        if (i < 0 || j < 0 || j >= (int)surfaces.size()) return;
        std::swap(surfaces[i], surfaces[j]);
    }

    // Removes a media item; any surface using it falls back to the test pattern.
    void removeMedia(int id) {
        if (!media.remove(id)) return;
        for (auto& s : surfaces) if (s.mediaId == id) s.mediaId = MediaPool::kPatternId;
    }

private:
    int nextSurfaceId_ = 1, nextAssetId_ = 1;
    int shapeCounter_[3] = {0, 0, 0};
};
