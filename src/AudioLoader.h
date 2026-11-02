#pragma once
#include "Audio.h"
#include "BeatDetect.h"
#include <cctype>
#include <chrono>
#include <future>
#include <string>
#include <vector>

inline std::string baseName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

inline std::string lowerExtension(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string e = path.substr(dot + 1);
    for (char& c : e) c = (char)std::tolower((unsigned char)c);
    return e;
}
inline bool isAudioPath(const std::string& path) {
    std::string e = lowerExtension(path);
    return e == "wav" || e == "mp3" || e == "flac";
}

// Decodes a song and finds its tempo on a worker thread, so a big file never freezes the UI.
class AudioLoader {
public:
    // assetId == 0: a new song (decode + find tempo). assetId > 0: re-load a song a project remembers
    // (decode only, so a tempo you corrected by hand isn't overwritten).
    struct Result { bool ok = false; std::string error; AudioAsset asset; int assetId = 0; };

    void start(const std::string& path, int assetId = 0) {
        Job j;
        j.name = baseName(path);
        j.fut = std::async(std::launch::async, [path, assetId] { return run(path, assetId); });
        jobs_.push_back(std::move(j));
    }
    bool busy() const { return !jobs_.empty(); }
    std::string busyName() const { return jobs_.empty() ? std::string() : jobs_.front().name; }

    // Call every frame. Returns true (once) per finished song.
    bool poll(Result& out) {
        for (size_t i = 0; i < jobs_.size(); i++) {
            if (jobs_[i].fut.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                out = jobs_[i].fut.get();
                jobs_.erase(jobs_.begin() + i);
                return true;
            }
        }
        return false;
    }

private:
    struct Job { std::string name; std::future<Result> fut; };
    static Result run(std::string path, int assetId) {
        Result r;
        r.assetId = assetId;
        AudioBuffer buf;
        if (!decodeAudioFile(path, buf, r.error)) return r;
        r.asset.name = baseName(path);
        r.asset.path = path;
        computePeaks(buf, r.asset.peakMin, r.asset.peakMax);
        if (assetId == 0) {
            BeatInfo bi = detectBeats(buf);
            r.asset.bpm = bi.bpm;
            r.asset.beatOffset = bi.offset;
        }
        r.asset.buf = std::make_shared<const AudioBuffer>(std::move(buf));
        r.ok = true;
        return r;
    }
    std::vector<Job> jobs_;
};
