#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// All paths inside MapHappy are UTF-8 strings (that's what the window's drop event and the JSON
// files use). These convert to and from std::filesystem::path, which matters on Windows where
// narrow strings would otherwise be read in the local code page and break non-English file names.
inline fs::path pathFromUtf8(const std::string& s) { return fs::u8path(s); }
inline std::string pathToUtf8(const fs::path& p) { return p.u8string(); }

// A path relative to `base` (the project's folder), with forward slashes, or "" if there isn't one
// (for example a different drive on Windows).
inline std::string relativeUtf8(const fs::path& base, const fs::path& target) {
    std::error_code ec;
    fs::path r = fs::relative(target, base, ec);
    if (ec || r.empty()) return "";
    return r.generic_u8string();
}

// Finds a file a project remembers. Tries the path relative to the project first (so a project
// folder can be moved or shared), then the absolute path. Returns "" if neither exists.
inline std::string resolveStoredPath(const fs::path& base, const std::string& rel, const std::string& abs) {
    std::error_code ec;
    if (!rel.empty()) {
        fs::path p = (base / pathFromUtf8(rel)).lexically_normal();
        if (fs::exists(p, ec)) return pathToUtf8(p);
    }
    if (!abs.empty() && fs::exists(pathFromUtf8(abs), ec)) return abs;
    return "";
}

inline std::string fileNameOf(const std::string& path) { return pathToUtf8(pathFromUtf8(path).filename()); }
inline std::string parentOf(const std::string& path)   { return pathToUtf8(pathFromUtf8(path).parent_path()); }

// ---- implemented in PlatformFiles.cpp (kept out of headers so windows.h stays out of them)
std::vector<std::string> listDrives();        // "C:\", "D:\" ... on Windows; "/" elsewhere
std::string homeDirectory();
std::string documentsDirectory();
std::string desktopDirectory();
std::string appDataDirectory();               // where settings and untitled-project autosaves live (created if needed)
