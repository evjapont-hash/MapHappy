// Folder listing, filtering and sorting for the file dialog, plus how projects remember file locations.
#include "TestSupport.h"
#include "FileBrowser.h"
#include <algorithm>

static void touch(const fs::path& p, const char* data = "x") { std::ofstream f(p, std::ios::binary); f << data; }

int main() {
    TempDir tmp("fs");
    const fs::path root = tmp.path();
    fs::create_directories(root / "Shows" / "Sub");
    fs::create_directories(root / "alpha");
    fs::create_directories(pathFromUtf8(pathToUtf8(root) + u8"/Ünïcode Folder"));
    touch(root / "song.MP3"); touch(root / "clip.mp4"); touch(root / "notes.txt"); touch(root / ".hidden.mp4"); touch(root / "Zed.wav");
    touch(pathFromUtf8(pathToUtf8(root) + u8"/vidéo ünï.mp4"));

    std::vector<DirEntry> e; std::string err;
    expect("a folder can be listed", listDirectory(root, {"mp3", "mp4", "wav"}, false, e, err) && err.empty());
    std::vector<std::string> names; for (auto& d : e) names.push_back(d.name);
    auto has = [&](const std::string& n) { return std::find(names.begin(), names.end(), n) != names.end(); };
    expect("sub-folders come first", e.size() > 3 && e[0].isDir && e[1].isDir && e[2].isDir && !e[3].isDir);
    expect("the filter keeps matching files, whatever the case of the extension", has("song.MP3") && has("clip.mp4") && has("Zed.wav"));
    expect("the filter hides other file types", !has("notes.txt"));
    expect("hidden files are skipped", !has(".hidden.mp4"));
    expect("non-English names survive", has(u8"vidéo ünï.mp4") && has(u8"Ünïcode Folder"));
    expect("entries are sorted case-insensitively", [&] { std::vector<std::string> f; for (auto& d : e) if (!d.isDir) f.push_back(lowerCase(d.name)); return std::is_sorted(f.begin(), f.end()); }());
    expect("'All files' shows everything", listDirectory(root, {"mp4"}, true, e, err) && std::any_of(e.begin(), e.end(), [](const DirEntry& d) { return d.name == "notes.txt"; }));
    expect("file sizes are reported", [&] { for (auto& d : e) if (d.name == "clip.mp4") return d.size == 1; return false; }());
    expect("modification times are filled in and recent", [&] { for (auto& d : e) if (d.name == "clip.mp4") return std::abs((long long)(std::time(nullptr) - d.mtime)) < 300; return false; }());
    expect("a missing folder reports an error", !listDirectory(root / "nope", {}, true, e, err) && !err.empty());

    // ---- how a project remembers where its media is
    const fs::path proj = root / "Shows" / "Sub";
    const std::string rel = relativeUtf8(proj, root / "clip.mp4");
    expect("a relative path goes up and over, with forward slashes", rel == "../../clip.mp4");
    expect("a stored relative path resolves from the project's folder", !resolveStoredPath(proj, rel, "").empty());
    expect("the absolute path is the fallback", !resolveStoredPath(proj, "wrong/rel.mp4", pathToUtf8(root / "clip.mp4")).empty());
    expect("a path that exists nowhere resolves to nothing", resolveStoredPath(proj, "x/y.mp4", "/nope/z.mp4").empty());
    fs::create_directories(root / "alpha" / "Shows" / "Sub"); fs::copy_file(root / "clip.mp4", root / "alpha" / "clip.mp4");
    expect("a moved project still finds its media through the relative path", !resolveStoredPath(root / "alpha" / "Shows" / "Sub", rel, pathToUtf8(root / "gone.mp4")).empty());
    expect("UTF-8 survives the trip through paths", pathToUtf8(pathFromUtf8(u8"vidéo ünï.mp4")) == u8"vidéo ünï.mp4");
    expect("the platform's standard folders exist", !homeDirectory().empty() && !documentsDirectory().empty() && !listDrives().empty());
    return finishTest();
}