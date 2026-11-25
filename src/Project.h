#pragma once
#include "AudioTypes.h"
#include "MediaPool.h"
#include "Surface.h"
#include "Timeline.h"
#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <vector>

// A folder in the Surfaces list. Folders nest; hiding one hides everything inside it. A folder only organises:
// the stacking order of surfaces stays their order in Project::surfaces.
struct Folder {
    int         id = 0;
    std::string name;
    int         parentId = 0;         // 0 = top level
    bool        visible = true;
    bool        open = true;          // expanded in the list
};

// A look to recall live (see Presets.h): every surface's parameters and media, and which folders are visible.
// Ten slots, played from the keys 1..9 and 0 in Live mode.
constexpr int kPresetSlots = 10;
struct PresetSurface { int id = 0; int mediaId = 0; std::map<std::string, float> values; };
struct Preset {
    bool        used = false;
    std::string name;
    float       fade = 0.5f;                          // seconds to cross-fade into it (0 = cut)
    std::vector<PresetSurface> surfaces;
    std::vector<std::pair<int, bool>> folders;        // folder id, visible
};
// Slot 0..9 <-> the key that plays it: 1, 2, ... 9, then 0 (the order of the keys on a keyboard).
inline int presetKeyDigit(int slot) { return slot == 9 ? 0 : slot + 1; }
inline int presetSlotForDigit(int digit) { return digit == 0 ? 9 : digit - 1; }

// Everything that makes up a show's visuals. This is what we will save and load.
struct Project {
    MediaPool            media;
    std::vector<Surface> surfaces;            // index 0 = front-most, like the list in the UI
    std::vector<Folder>  folders;             // in the order they are listed
    std::array<Preset, kPresetSlots> presets; // live presets, played from the number keys
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

    // Is the surface drawn? Its own Visible switch, and that of every folder it is inside.
    bool isShown(const Surface& s) const { return s.visible && folderShown(s.folderId); }

    // ---- folders
    Folder* findFolder(int id) { for (auto& f : folders) if (f.id == id) return &f; return nullptr; }
    const Folder* findFolder(int id) const { for (auto& f : folders) if (f.id == id) return &f; return nullptr; }
    // A folder (0 = the top level) and all the folders above it are visible.
    bool folderShown(int id) const {
        for (int guard = 0; id != 0 && guard <= (int)folders.size(); guard++) {
            const Folder* f = findFolder(id);
            if (!f) return true;
            if (!f->visible) return false;
            id = f->parentId;
        }
        return true;
    }
    // Is `inner` the folder `outer`, or somewhere inside it? (0 = the top level contains everything.)
    bool folderWithin(int inner, int outer) const {
        if (outer == 0) return true;
        for (int guard = 0; inner != 0 && guard <= (int)folders.size(); guard++) {
            if (inner == outer) return true;
            const Folder* f = findFolder(inner);
            if (!f) return false;
            inner = f->parentId;
        }
        return false;
    }
    int folderDepth(int id) const { int d = 0; for (int guard = 0; id != 0 && guard <= (int)folders.size(); guard++, d++) { const Folder* f = findFolder(id); if (!f) break; id = f->parentId; } return d; }

    int addFolder(const std::string& name = "", int parentId = 0) {
        Folder f;
        f.id = nextFolderId_++;
        f.name = name.empty() ? "Folder " + std::to_string(f.id) : name;
        f.parentId = findFolder(parentId) ? parentId : 0;
        folders.push_back(f);
        return f.id;
    }
    // Moves a folder into another (0 = the top level). Refused if that would put it inside itself.
    bool moveFolder(int id, int newParent) {
        Folder* f = findFolder(id);
        if (!f || (newParent != 0 && !findFolder(newParent)) || folderWithin(newParent, id)) return false;
        f->parentId = newParent;
        return true;
    }
    bool moveSurfaceToFolder(int surfaceId, int folderId) {
        Surface* s = find(surfaceId);
        if (!s || (folderId != 0 && !findFolder(folderId))) return false;
        s->folderId = folderId;
        return true;
    }
    // Deletes a folder but nothing in it: its surfaces and folders move up to its parent.
    void removeFolder(int id) {
        Folder* f = findFolder(id);
        if (!f) return;
        const int parent = f->parentId;
        for (Folder& o : folders) if (o.parentId == id) o.parentId = parent;
        for (Surface& s : surfaces) if (s.folderId == id) s.folderId = parent;
        folders.erase(std::remove_if(folders.begin(), folders.end(), [id](const Folder& x) { return x.id == id; }), folders.end());
    }
    int nextFolderId() const { return nextFolderId_; }
    void setNextFolderId(int n) { nextFolderId_ = std::max(nextFolderId_, n); }

    // Changes a surface's shape, giving the new shape sensible starting points from the current quad.
    void setShape(Surface& s, Shape shape) {
        if (s.shape == shape) return;
        if (shape == Shape::Line) {                            // along the quad's middle, from its left edge to its right edge
            s.lineA = Vec2{(s.out[0].x + s.out[3].x) * 0.5f, (s.out[0].y + s.out[3].y) * 0.5f};
            s.lineB = Vec2{(s.out[1].x + s.out[2].x) * 0.5f, (s.out[1].y + s.out[2].y) * 0.5f};
        }
        if (s.shape == Shape::Line) {                          // back to a box around where the line was
            Vec2 c{(s.lineA.x + s.lineB.x) * 0.5f, (s.lineA.y + s.lineB.y) * 0.5f};
            const float h = 0.25f, w = 0.25f / stageAspect();
            s.out[0] = {c.x - w, c.y - h}; s.out[1] = {c.x + w, c.y - h}; s.out[2] = {c.x + w, c.y + h}; s.out[3] = {c.x - w, c.y + h};
        }
        if (shape == Shape::Freeform && s.poly.size() < 3) s.poly = defaultPolygon();
        s.shape = shape;
        syncLineQuad(s, stageAspect());
        resetInput(s);
    }
    // Lines keep their quad matched to their end points and the stage's shape (call once a frame).
    void syncLines() { for (Surface& s : surfaces) syncLineQuad(s, stageAspect()); }

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
        s.lineA.x += shift; s.lineA.y += shift; s.lineB.x += shift; s.lineB.y += shift;
        syncLineQuad(s, stageAspect());
        surfaces.insert(surfaces.begin(), s);
        selectedSurfaceId = s.id;
        return surfaces.front();
    }
    // A copy of a surface in front, `offset` away from where the original was; it is selected.
    Surface& pasteSurface(const Surface& from, float offset) {
        const Surface src = from;                                  // (`from` may live in the list this grows)
        Surface& c = addSurface(src.shape, src.mediaId);
        const int id = c.id, mediaId = c.mediaId;
        c = src;
        c.id = id; c.mediaId = mediaId;
        if (!findFolder(c.folderId)) c.folderId = 0;
        std::string base = src.name;
        const size_t at = base.rfind(" copy");
        if (at != std::string::npos && base.find_first_not_of(" 0123456789", at + 5) == std::string::npos) base.resize(at);
        base += " copy";
        auto taken = [&](const std::string& n) { for (const Surface& s : surfaces) if (s.id != id && s.name == n) return true; return false; };
        c.name = base;
        for (int n = 2; taken(c.name); n++) c.name = base + " " + std::to_string(n);
        for (Vec2& q : c.out) { q.x += offset; q.y += offset; }
        c.lineA.x += offset; c.lineA.y += offset; c.lineB.x += offset; c.lineB.y += offset;
        syncLineQuad(c, stageAspect());
        return c;
    }
    float stageAspect() const { return (float)outW / (float)std::max(1, outH); }

    // Default input region: the whole media for a quad, a thin strip across the middle for a line, or a
    // centered square for the other shapes, so a circle samples a round area instead of a stretched oval.
    void resetInput(Surface& s) const {
        float hx = 0.5f, hy = 0.5f;
        if (s.shape == Shape::Line) hy = 0.05f;
        else if (s.shape != Shape::Quad && s.shape != Shape::Object3D) {      // (a 3D model's faces each show the whole picture)
            const MediaItem* m = media.find(s.mediaId);
            float a = m ? m->aspect() : 16.0f / 9.0f;
            if (a >= 1.0f) hx = 0.5f / a; else hy = 0.5f * a;
        }
        s.in[0] = {0.5f - hx, 0.5f - hy}; s.in[1] = {0.5f + hx, 0.5f - hy};
        s.in[2] = {0.5f + hx, 0.5f + hy}; s.in[3] = {0.5f - hx, 0.5f + hy};
    }

    struct Counters { int surface = 1, asset = 1, shape[kShapeCount] = {}; };
    Counters counters() const { Counters c; c.surface = nextSurfaceId_; c.asset = nextAssetId_; for (int i = 0; i < kShapeCount; i++) c.shape[i] = shapeCounter_[i]; return c; }
    void setCounters(const Counters& c) {
        nextSurfaceId_ = std::max(nextSurfaceId_, c.surface); nextAssetId_ = std::max(nextAssetId_, c.asset);
        for (int i = 0; i < kShapeCount; i++) shapeCounter_[i] = std::max(shapeCounter_[i], c.shape[i]);
    }

    // Back to a blank project (keeps the built-in test pattern). Frees GPU textures, so call it between frames.
    void resetToEmpty() {
        surfaces.clear();
        folders.clear();
        presets = {};
        nextFolderId_ = 1;
        selectedSurfaceId = 0;
        media.clearAllButPattern();
        audioAssets.clear();
        timeline = Timeline();
        transport = Transport();
        audioStatus.clear(); audioStatusIsError = false;
        outW = 1920; outH = 1080;
        nextSurfaceId_ = 1; nextAssetId_ = 1;
        for (int& c : shapeCounter_) c = 0;
        timelineDirty = true;
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
    int nextSurfaceId_ = 1, nextAssetId_ = 1, nextFolderId_ = 1;
    int shapeCounter_[kShapeCount] = {};
};
