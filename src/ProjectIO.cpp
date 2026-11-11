#include "ProjectIO.h"
#include "Params.h"
#include "PathUtil.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

using json = nlohmann::json;

namespace {

const int kFormatVersion = 1;

json jv(Vec2 v) { return json::array({v.x, v.y}); }
json jquad(const Vec2 q[4]) { json a = json::array(); for (int i = 0; i < 4; i++) a.push_back(jv(q[i])); return a; }

bool readQuad(const json& j, Vec2 q[4]) {
    if (!j.is_array() || j.size() != 4) return false;
    for (int i = 0; i < 4; i++) {
        const json& p = j[i];
        if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number()) return false;
        q[i] = Vec2{p[0].get<float>(), p[1].get<float>()};
    }
    return true;
}

const char* shapeKey(Shape s) { return s == Shape::Circle ? "circle" : s == Shape::Triangle ? "triangle" : "quad"; }
Shape shapeFrom(const std::string& s) { return s == "circle" ? Shape::Circle : s == "triangle" ? Shape::Triangle : Shape::Quad; }
const char* interpKey(Interp i) { return i == Interp::Linear ? "linear" : i == Interp::Hold ? "hold" : "smooth"; }
Interp interpFrom(const std::string& s) { return s == "linear" ? Interp::Linear : s == "hold" ? Interp::Hold : Interp::Smooth; }

template <class T> T get(const json& j, const char* key, T def) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    try { return it->get<T>(); } catch (...) { return def; }
}

// does a timeline track drive this surface parameter?
bool isDriven(const Project& p, int surfaceId, const char* param) {
    std::string path = "surface/" + std::to_string(surfaceId) + "/" + param;
    for (const Track& t : p.timeline.tracks) if (t.kind == TrackKind::Param && t.path == path && !t.clips.empty()) return true;
    return false;
}

void putPath(json& o, const char* key, const std::string& abs, const fs::path& base, bool haveBase) {
    o[key] = abs;
    if (haveBase) {
        std::string rel = relativeUtf8(base, pathFromUtf8(abs));
        if (!rel.empty()) o[std::string(key) + "Rel"] = rel;
    }
}

}  // namespace

std::string projectToJson(const Project& p, const std::string& projectFilePath, bool forDirtyCheck) {
    const bool haveBase = !projectFilePath.empty();
    const fs::path base = haveBase ? pathFromUtf8(projectFilePath).parent_path() : fs::path();
    json j;
    j["format"] = "maphappy";
    j["version"] = kFormatVersion;
    j["output"] = {{"width", p.outW}, {"height", p.outH}};

    // media
    json media = json::array();
    for (const auto& m : p.media.items) {
        json o;
        o["id"] = m->id; o["name"] = m->name; o["playing"] = m->playing; o["loop"] = m->loop;
        if (m->kind == MediaKind::Generator) { o["kind"] = "generator"; o["type"] = "pattern"; }
        else { o["kind"] = "video"; putPath(o, "path", m->path, base, haveBase); }
        media.push_back(o);
    }
    j["media"] = media;

    // surfaces (front-most first, as in the list)
    json surfaces = json::array();
    for (const Surface& s : p.surfaces) {
        json o;
        o["id"] = s.id; o["name"] = s.name; o["shape"] = shapeKey(s.shape); o["media"] = s.mediaId;
        o["flipH"] = s.flipH; o["flipV"] = s.flipV;
        o["blend"] = blendKey(s.blend);
        // every animatable value comes from the parameter table; ones a timeline is driving are left out of the
        // dirty check so that merely playing a show doesn't look like an edit
        for (const ParamDef& d : paramDefs()) {
            if (forDirtyCheck && isDriven(p, s.id, d.key)) continue;
            if (d.format == ParamFormat::Bool) o[d.key] = d.get(s) >= 0.5f;
            else                                o[d.key] = d.get(s);
        }
        o["in"] = jquad(s.in); o["out"] = jquad(s.out);
        surfaces.push_back(o);
    }
    j["surfaces"] = surfaces;
    if (!forDirtyCheck) j["selectedSurface"] = p.selectedSurfaceId;      // selecting something is not an edit

    // audio assets (the sound itself is re-read from the file; we keep the tempo analysis, which may have been corrected by hand)
    json assets = json::array();
    for (const AudioAsset& a : p.audioAssets) {
        json o;
        o["id"] = a.id; o["name"] = a.name; o["bpm"] = a.bpm; o["beatOffset"] = a.beatOffset;
        putPath(o, "path", a.path, base, haveBase);
        assets.push_back(o);
    }
    j["audioAssets"] = assets;

    // timeline
    json tl;
    tl["length"] = p.timeline.length;
    json tracks = json::array();
    for (const Track& t : p.timeline.tracks) {
        json o;
        o["id"] = t.id; o["kind"] = t.kind == TrackKind::Audio ? "audio" : "param";
        o["name"] = t.name; o["path"] = t.path; o["muted"] = t.muted;
        if (t.kind == TrackKind::Param) {
            // keyframes are stored once per distinct ClipData, so ghost copies stay linked after a reload
            std::vector<const ClipData*> distinct;
            json datas = json::array(), clips = json::array();
            for (const Clip& c : t.clips) {
                int idx = -1;
                for (size_t i = 0; i < distinct.size(); i++) if (distinct[i] == c.data.get()) idx = (int)i;
                if (idx < 0) {
                    idx = (int)distinct.size(); distinct.push_back(c.data.get());
                    json keys = json::array();
                    for (const Keyframe& k : c.data->keys) keys.push_back({{"t", k.t}, {"v", k.v}, {"curve", interpKey(k.interp)}});
                    datas.push_back({{"keys", keys}});
                }
                clips.push_back({{"id", c.id}, {"name", c.name}, {"start", c.start}, {"duration", c.duration}, {"ghost", c.ghost}, {"data", idx}});
            }
            o["clipData"] = datas; o["clips"] = clips;
        } else {
            json ac = json::array();
            for (const AudioClip& c : t.audioClips)
                ac.push_back({{"id", c.id}, {"name", c.name}, {"asset", c.assetId}, {"start", c.start}, {"duration", c.duration}, {"offset", c.offset}, {"gain", c.gain}});
            o["audioClips"] = ac;
        }
        tracks.push_back(o);
    }
    tl["tracks"] = tracks;
    json markers = json::array();
    for (const Marker& m : p.timeline.markers) markers.push_back({{"id", m.id}, {"time", m.time}});
    tl["markers"] = markers;
    Timeline::Counters tc = p.timeline.counters();
    tl["counters"] = {{"track", tc.track}, {"clip", tc.clip}, {"marker", tc.marker}, {"audioTrack", tc.audioTrack}};
    j["timeline"] = tl;

    j["transport"] = {{"avOffset", p.transport.avOffset}, {"loop", p.transport.loop}};
    Project::Counters c = p.counters();
    j["counters"] = {{"surface", c.surface}, {"asset", c.asset}, {"shape", json::array({c.shape[0], c.shape[1], c.shape[2]})}};
    return j.dump(2);
}

bool readTextFile(const std::string& path, std::string& out) {
    std::ifstream f(pathFromUtf8(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeTextFileAtomic(const std::string& path, const std::string& text, std::string& error) {
    fs::path target = pathFromUtf8(path);
    fs::path tmp = target; tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { error = "Couldn't write to that location"; return false; }
        f.write(text.data(), (std::streamsize)text.size());
        f.flush();
        if (!f) { error = "Writing the file failed (is the disk full?)"; return false; }
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);                 // replaces an existing file in one step
    if (ec) { error = "Couldn't replace the old file: " + ec.message(); fs::remove(tmp, ec); return false; }
    return true;
}

bool saveProjectToFile(const Project& p, const std::string& path, std::string& error) {
    return writeTextFileAtomic(path, projectToJson(p, path, false), error);
}

bool loadProjectFromFile(Project& p, const std::string& path, LoadReport& report) {
    std::string text;
    if (!readTextFile(path, text)) { report.error = "Couldn't read that file"; return false; }
    return loadProjectFromJson(p, text, pathToUtf8(pathFromUtf8(path).parent_path()), report);
}

bool loadProjectFromJson(Project& p, const std::string& text, const std::string& baseDir, LoadReport& report) {
    json j = json::parse(text, nullptr, /*allow_exceptions*/ false);
    if (j.is_discarded() || !j.is_object()) { report.error = "This isn't a MapHappy project (the file isn't valid JSON)"; return false; }
    if (get<std::string>(j, "format", "") != "maphappy") { report.error = "This isn't a MapHappy project file"; return false; }
    int version = get<int>(j, "version", 0);
    if (version < 1 || version > kFormatVersion) { report.error = "This project was made with a newer MapHappy (file version " + std::to_string(version) + "). Please update."; return false; }

    const fs::path base = pathFromUtf8(baseDir);
    auto warn = [&](const std::string& w) { report.warnings.push_back(w); };

    // ------------------------------------------------------------------ read and check everything first
    struct PMedia { int id; std::string name, path, rel; bool playing, loop, isVideo; };
    std::vector<PMedia> media;
    std::set<int> mediaIds = {MediaPool::kPatternId};
    bool patternPlaying = true, patternLoop = true;
    if (j.contains("media") && j["media"].is_array()) {
        for (const json& m : j["media"]) {
            if (!m.is_object()) continue;
            PMedia pm;
            pm.id = get<int>(m, "id", 0);
            pm.name = get<std::string>(m, "name", "");
            pm.path = get<std::string>(m, "path", ""); pm.rel = get<std::string>(m, "pathRel", "");
            pm.playing = get<bool>(m, "playing", true); pm.loop = get<bool>(m, "loop", true);
            pm.isVideo = get<std::string>(m, "kind", "") == "video";
            if (!pm.isVideo) { if (pm.id == MediaPool::kPatternId) { patternPlaying = pm.playing; patternLoop = pm.loop; } continue; }
            if (pm.id <= 1 || mediaIds.count(pm.id)) { warn("Skipped a media entry with a bad or repeated id"); continue; }
            mediaIds.insert(pm.id);
            media.push_back(pm);
        }
    }

    std::vector<Surface> surfaces;
    std::set<int> surfaceIds;
    if (j.contains("surfaces") && j["surfaces"].is_array()) {
        for (const json& o : j["surfaces"]) {
            if (!o.is_object()) continue;
            Surface s;
            s.id = get<int>(o, "id", 0);
            if (s.id <= 0 || surfaceIds.count(s.id)) { warn("Skipped a surface with a bad or repeated id"); continue; }
            surfaceIds.insert(s.id);
            s.name = get<std::string>(o, "name", "Surface-" + std::to_string(s.id));
            s.shape = shapeFrom(get<std::string>(o, "shape", "quad"));
            s.flipH = get<bool>(o, "flipH", false); s.flipV = get<bool>(o, "flipV", false);
            s.blend = blendFromKey(get<std::string>(o, "blend", "over"));
            for (const ParamDef& d : paramDefs()) {                 // files from older versions lack the newer keys: they get the defaults
                if (d.format == ParamFormat::Bool) d.set(s, get<bool>(o, d.key, d.def >= 0.5f) ? 1.0f : 0.0f);
                else {
                    float v = get<float>(o, d.key, d.def);
                    if (!std::isfinite(v)) v = d.def;
                    d.set(s, std::max(d.min, std::min(d.max, v)));
                }
            }
            s.mediaId = get<int>(o, "media", MediaPool::kPatternId);
            if (!mediaIds.count(s.mediaId)) { warn("\"" + s.name + "\" used media that isn't in the project; it now shows the test pattern"); s.mediaId = MediaPool::kPatternId; }
            Vec2 q[4];
            Mat3 h;
            Surface def;
            if (o.contains("in") && readQuad(o["in"], q) && isConvex(q) && squareToQuad(q, h)) for (int i = 0; i < 4; i++) s.in[i] = q[i];
            else { warn("\"" + s.name + "\" had an unusable input shape; it was reset"); for (int i = 0; i < 4; i++) s.in[i] = def.in[i]; }
            if (o.contains("out") && readQuad(o["out"], q) && isConvex(q) && squareToQuad(q, h)) for (int i = 0; i < 4; i++) s.out[i] = q[i];
            else { warn("\"" + s.name + "\" had an unusable output shape; it was reset"); for (int i = 0; i < 4; i++) s.out[i] = def.out[i]; }
            surfaces.push_back(s);
        }
    }

    std::vector<AudioAsset> assets;
    std::set<int> assetIds;
    if (j.contains("audioAssets") && j["audioAssets"].is_array()) {
        for (const json& o : j["audioAssets"]) {
            if (!o.is_object()) continue;
            AudioAsset a;
            a.id = get<int>(o, "id", 0);
            if (a.id <= 0 || assetIds.count(a.id)) { warn("Skipped an audio file entry with a bad or repeated id"); continue; }
            assetIds.insert(a.id);
            a.name = get<std::string>(o, "name", "audio");
            a.bpm = std::max(0.0, get<double>(o, "bpm", 0.0));
            a.beatOffset = std::max(0.0, get<double>(o, "beatOffset", 0.0));
            a.path = get<std::string>(o, "path", "");
            assets.push_back(a);
        }
    }
    std::vector<std::string> assetRel;       // stored relative path per asset, same order
    if (j.contains("audioAssets") && j["audioAssets"].is_array())
        for (const json& o : j["audioAssets"]) if (o.is_object()) assetRel.push_back(get<std::string>(o, "pathRel", ""));

    Timeline tl;
    int maxTrack = 0, maxClip = 0, maxMarker = 0;
    if (j.contains("timeline") && j["timeline"].is_object()) {
        const json& jt = j["timeline"];
        tl.length = std::max(1.0, get<double>(jt, "length", 60.0));
        std::set<int> trackIds, clipIds;
        if (jt.contains("tracks") && jt["tracks"].is_array()) {
            for (const json& o : jt["tracks"]) {
                if (!o.is_object()) continue;
                Track t;
                t.id = get<int>(o, "id", 0);
                if (t.id <= 0 || trackIds.count(t.id)) { warn("Skipped a timeline track with a bad or repeated id"); continue; }
                trackIds.insert(t.id); maxTrack = std::max(maxTrack, t.id);
                t.kind = get<std::string>(o, "kind", "param") == "audio" ? TrackKind::Audio : TrackKind::Param;
                t.name = get<std::string>(o, "name", ""); t.path = get<std::string>(o, "path", ""); t.muted = get<bool>(o, "muted", false);
                if (t.kind == TrackKind::Param) {
                    std::vector<std::shared_ptr<ClipData>> datas;
                    if (o.contains("clipData") && o["clipData"].is_array()) {
                        for (const json& d : o["clipData"]) {
                            auto cd = std::make_shared<ClipData>();
                            if (d.is_object() && d.contains("keys") && d["keys"].is_array())
                                for (const json& k : d["keys"]) {
                                    if (!k.is_object()) continue;
                                    Keyframe kf; kf.t = std::max(0.0, get<double>(k, "t", 0.0)); kf.v = get<float>(k, "v", 0.0f);
                                    kf.interp = interpFrom(get<std::string>(k, "curve", "smooth"));
                                    cd->keys.push_back(kf);
                                }
                            std::stable_sort(cd->keys.begin(), cd->keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.t < b.t; });
                            datas.push_back(cd);
                        }
                    }
                    if (o.contains("clips") && o["clips"].is_array()) {
                        for (const json& c : o["clips"]) {
                            if (!c.is_object()) continue;
                            int di = get<int>(c, "data", -1);
                            if (di < 0 || di >= (int)datas.size()) { warn("Skipped a clip that had no keyframe data"); continue; }
                            Clip cl;
                            cl.id = get<int>(c, "id", 0);
                            if (cl.id <= 0 || clipIds.count(cl.id)) { warn("Skipped a clip with a bad or repeated id"); continue; }
                            clipIds.insert(cl.id); maxClip = std::max(maxClip, cl.id);
                            cl.name = get<std::string>(c, "name", "Clip-" + std::to_string(cl.id));
                            cl.start = std::max(0.0, get<double>(c, "start", 0.0));
                            cl.duration = std::max(1.0 / kTimelineFps, get<double>(c, "duration", 1.0));
                            cl.ghost = get<bool>(c, "ghost", false);
                            cl.data = datas[di];
                            if (cl.data->keys.empty()) { cl.data->keys = {{0.0, 0.0f, Interp::Smooth}, {cl.duration, 0.0f, Interp::Smooth}}; warn("A clip had no keyframes; gave it a flat curve"); }
                            t.clips.push_back(cl);
                        }
                    }
                    std::stable_sort(t.clips.begin(), t.clips.end(), [](const Clip& a, const Clip& b) { return a.start < b.start; });
                    for (size_t i = 1; i < t.clips.size(); i++)                  // clips may never overlap
                        if (t.clips[i].start < t.clips[i - 1].end() - 1e-9) { t.clips[i].start = t.clips[i - 1].end(); warn("Overlapping clips on a track were moved apart"); }
                } else {
                    if (o.contains("audioClips") && o["audioClips"].is_array()) {
                        for (const json& c : o["audioClips"]) {
                            if (!c.is_object()) continue;
                            AudioClip ac;
                            ac.id = get<int>(c, "id", 0);
                            if (ac.id <= 0 || clipIds.count(ac.id)) { warn("Skipped an audio clip with a bad or repeated id"); continue; }
                            ac.assetId = get<int>(c, "asset", 0);
                            if (!assetIds.count(ac.assetId)) { warn("Skipped an audio clip whose song isn't in the project"); continue; }
                            clipIds.insert(ac.id); maxClip = std::max(maxClip, ac.id);
                            ac.name = get<std::string>(c, "name", "Audio-clip-" + std::to_string(ac.id));
                            ac.start = std::max(0.0, get<double>(c, "start", 0.0));
                            ac.duration = std::max(1.0 / kTimelineFps, get<double>(c, "duration", 1.0));
                            ac.offset = std::max(0.0, get<double>(c, "offset", 0.0));
                            ac.gain = std::max(0.0f, std::min(4.0f, get<float>(c, "gain", 1.0f)));
                            t.audioClips.push_back(ac);
                        }
                    }
                    std::stable_sort(t.audioClips.begin(), t.audioClips.end(), [](const AudioClip& a, const AudioClip& b) { return a.start < b.start; });
                    for (size_t i = 1; i < t.audioClips.size(); i++)
                        if (t.audioClips[i].start < t.audioClips[i - 1].end() - 1e-9) { t.audioClips[i].start = t.audioClips[i - 1].end(); warn("Overlapping audio clips were moved apart"); }
                }
                tl.tracks.push_back(std::move(t));
            }
        }
        if (jt.contains("markers") && jt["markers"].is_array()) {
            std::set<int> ids;
            for (const json& m : jt["markers"]) {
                if (!m.is_object()) continue;
                Marker mk; mk.id = get<int>(m, "id", 0); mk.time = std::max(0.0, get<double>(m, "time", 0.0));
                if (mk.id <= 0 || ids.count(mk.id)) continue;
                ids.insert(mk.id); maxMarker = std::max(maxMarker, mk.id);
                tl.markers.push_back(mk);
            }
            std::stable_sort(tl.markers.begin(), tl.markers.end(), [](const Marker& a, const Marker& b) { return a.time < b.time; });
        }
        Timeline::Counters tc;
        if (jt.contains("counters") && jt["counters"].is_object()) {
            const json& c = jt["counters"];
            tc.track = get<int>(c, "track", 1); tc.clip = get<int>(c, "clip", 1); tc.marker = get<int>(c, "marker", 1); tc.audioTrack = get<int>(c, "audioTrack", 0);
        }
        tc.track = std::max(tc.track, maxTrack + 1); tc.clip = std::max(tc.clip, maxClip + 1); tc.marker = std::max(tc.marker, maxMarker + 1);
        int audioTracks = 0; for (const Track& t : tl.tracks) if (t.kind == TrackKind::Audio) audioTracks++;
        tc.audioTrack = std::max(tc.audioTrack, audioTracks);
        tl.setCounters(tc);
        for (const Track& t : tl.tracks) { for (const Clip& c : t.clips) tl.extendTo(c.end() - 5.0); for (const AudioClip& c : t.audioClips) tl.extendTo(c.end() - 5.0); }
    }

    // ------------------------------------------------------------------ everything checked: now replace the project
    p.resetToEmpty();
    p.outW = std::max(16, std::min(16384, get<int>(j.value("output", json::object()), "width", 1920)));
    p.outH = std::max(16, std::min(16384, get<int>(j.value("output", json::object()), "height", 1080)));
    if (MediaItem* pat = p.media.find(MediaPool::kPatternId)) { pat->playing = patternPlaying; pat->loop = patternLoop; }

    for (const PMedia& pm : media) {
        std::string resolved = resolveStoredPath(base, pm.rel, pm.path);
        std::string err;
        int id = 0;
        if (!resolved.empty()) {
            id = p.media.addVideoWithId(pm.id, resolved, pm.name, err);
            if (!id) warn("Couldn't open \"" + pm.name + "\": " + err);
        } else {
            warn("Can't find \"" + pm.name + "\" (" + pm.path + ")");
        }
        if (!id) id = p.media.addMissingVideo(pm.id, pm.path, pm.name);
        if (MediaItem* m = p.media.find(id)) { m->playing = pm.playing; m->loop = pm.loop; }
    }
    p.surfaces = surfaces;
    p.selectedSurfaceId = surfaceIds.count(get<int>(j, "selectedSurface", 0)) ? get<int>(j, "selectedSurface", 0) : (surfaces.empty() ? 0 : surfaces.front().id);

    for (size_t i = 0; i < assets.size(); i++) {
        AudioAsset a = assets[i];
        std::string resolved = resolveStoredPath(base, i < assetRel.size() ? assetRel[i] : "", a.path);
        if (!resolved.empty()) { a.path = resolved; a.loading = true; report.audioToLoad.push_back(a.id); }
        else { a.missing = true; warn("Can't find the song \"" + a.name + "\" (" + a.path + ")"); }
        p.audioAssets.push_back(a);
    }
    p.timeline = std::move(tl);

    if (j.contains("transport") && j["transport"].is_object()) {
        p.transport.avOffset = std::max(-1.0, std::min(1.0, get<double>(j["transport"], "avOffset", 0.0)));
        p.transport.loop = get<bool>(j["transport"], "loop", false);
    }
    Project::Counters c;
    int maxSurface = 0, maxAsset = 0;
    for (const Surface& s : surfaces) maxSurface = std::max(maxSurface, s.id);
    for (const AudioAsset& a : assets) maxAsset = std::max(maxAsset, a.id);
    if (j.contains("counters") && j["counters"].is_object()) {
        const json& jc = j["counters"];
        c.surface = get<int>(jc, "surface", 1); c.asset = get<int>(jc, "asset", 1);
        if (jc.contains("shape") && jc["shape"].is_array() && jc["shape"].size() == 3)
            for (int i = 0; i < 3; i++) if (jc["shape"][i].is_number_integer()) c.shape[i] = jc["shape"][i].get<int>();
    }
    c.surface = std::max(c.surface, maxSurface + 1); c.asset = std::max(c.asset, maxAsset + 1);
    p.setCounters(c);
    p.timelineDirty = true;
    return true;
}

// ---------------------------------------------------------------------------------- settings
static std::string settingsPath() { return pathToUtf8(pathFromUtf8(appDataDirectory()) / "settings.json"); }

AppSettings loadSettings() {
    AppSettings s;
    std::string text;
    if (!readTextFile(settingsPath(), text)) return s;
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return s;
    s.lastFolder = get<std::string>(j, "lastFolder", "");
    s.outputMonitor = get<std::string>(j, "outputMonitor", "");
    s.outputMonitorIndex = get<int>(j, "outputMonitorIndex", -1);
    s.outputMode = get<std::string>(j, "outputMode", "auto");
    s.outputScale = get<std::string>(j, "outputScale", "fit");
    if (j.contains("recent") && j["recent"].is_array())
        for (const json& r : j["recent"]) if (r.is_string() && s.recent.size() < 10) s.recent.push_back(r.get<std::string>());
    return s;
}

void saveSettings(const AppSettings& s) {
    json j; j["recent"] = s.recent; j["lastFolder"] = s.lastFolder;
    j["outputMonitor"] = s.outputMonitor; j["outputMonitorIndex"] = s.outputMonitorIndex;
    j["outputMode"] = s.outputMode; j["outputScale"] = s.outputScale;
    std::ofstream f(pathFromUtf8(settingsPath()), std::ios::binary | std::ios::trunc);
    if (f) f << j.dump(2);
}

void addRecentFile(AppSettings& s, const std::string& path) {
    s.recent.erase(std::remove(s.recent.begin(), s.recent.end(), path), s.recent.end());
    s.recent.insert(s.recent.begin(), path);
    if (s.recent.size() > 10) s.recent.resize(10);
}

// ---------------------------------------------------------------------------------- autosave
std::string autosavePathFor(const std::string& projectPath) {
    if (projectPath.empty()) return pathToUtf8(pathFromUtf8(appDataDirectory()) / "untitled.autosave");
    return projectPath + ".autosave";
}
bool autosaveIsNewer(const std::string& projectPath) {
    std::error_code ec;
    fs::path a = pathFromUtf8(autosavePathFor(projectPath));
    if (!fs::exists(a, ec)) return false;
    if (projectPath.empty()) return true;
    fs::path p = pathFromUtf8(projectPath);
    if (!fs::exists(p, ec)) return true;
    return fs::last_write_time(a, ec) > fs::last_write_time(p, ec);
}
void deleteAutosave(const std::string& projectPath) {
    std::error_code ec;
    fs::remove(pathFromUtf8(autosavePathFor(projectPath)), ec);
}
