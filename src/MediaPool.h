#pragma once
#include "ContentSource.h"
#include "PatternSource.h"
#include "VideoSource.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

enum class MediaKind { Generator, Video };

struct MediaItem {
    int         id = 0;
    std::string name;
    MediaKind   kind = MediaKind::Generator;
    std::string path;
    std::unique_ptr<ContentSource> source;
    bool playing = true, loop = true;

    VideoSource* video() { return kind == MediaKind::Video ? static_cast<VideoSource*>(source.get()) : nullptr; }
    const VideoSource* video() const { return kind == MediaKind::Video ? static_cast<const VideoSource*>(source.get()) : nullptr; }
    float aspect() const {
        int w = source ? source->width() : 0, h = source ? source->height() : 0;
        return (w > 0 && h > 0) ? (float)w / (float)h : 16.0f / 9.0f;
    }
};

// Everything that can be shown on a surface. Several surfaces can use the same item,
// and each item is decoded/rendered once per frame no matter how many use it.
class MediaPool {
public:
    static constexpr int kPatternId = 1;

    MediaPool() {
        auto m = std::make_unique<MediaItem>();
        m->id = nextId_++;                         // always 1
        m->name = "Test Pattern";
        m->kind = MediaKind::Generator;
        m->source = std::make_unique<PatternSource>();
        items.push_back(std::move(m));
    }

    // Returns the new item's id, or 0 on failure (with the reason in `error`).
    int addVideo(std::string path, std::string& error) {
        while (!path.empty() && (path.front() == '"' || path.front() == ' ')) path.erase(path.begin());
        while (!path.empty() && (path.back()  == '"' || path.back()  == ' ')) path.pop_back();
        if (path.empty()) { error = "No path given"; return 0; }

        auto v = std::make_unique<VideoSource>();
        if (!v->open(path)) { error = v->error().empty() ? "Could not open file" : v->error(); return 0; }

        auto m = std::make_unique<MediaItem>();
        m->id = nextId_++;
        size_t slash = path.find_last_of("/\\");
        m->name = (slash == std::string::npos) ? path : path.substr(slash + 1);
        m->kind = MediaKind::Video;
        m->path = path;
        m->source = std::move(v);
        error.clear();
        int id = m->id;
        items.push_back(std::move(m));
        return id;
    }

    MediaItem* find(int id) {
        for (auto& m : items) if (m->id == id) return m.get();
        return nullptr;
    }
    const MediaItem* find(int id) const {
        for (auto& m : items) if (m->id == id) return m.get();
        return nullptr;
    }
    ContentSource* source(int id) { MediaItem* m = find(id); return m ? m->source.get() : nullptr; }

    bool remove(int id) {
        if (id == kPatternId) return false;
        auto it = std::remove_if(items.begin(), items.end(), [id](const std::unique_ptr<MediaItem>& m) { return m->id == id; });
        bool removed = it != items.end();
        items.erase(it, items.end());
        return removed;
    }

    void update(double dt) {
        for (auto& m : items) {
            if (VideoSource* v = m->video()) v->setLoop(m->loop);
            m->source->update(m->playing ? dt : 0.0);
        }
    }

    std::vector<std::unique_ptr<MediaItem>> items;   // unique_ptr so pointers stay valid when the list grows

private:
    int nextId_ = 1;
};
