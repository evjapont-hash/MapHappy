#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef _CRT_SECURE_NO_WARNINGS
    #define _CRT_SECURE_NO_WARNINGS
  #endif
  #include <windows.h>
#endif
#include "PathUtil.h"
#include <cstdlib>

static std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

std::vector<std::string> listDrives() {
    std::vector<std::string> d;
#ifdef _WIN32
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; i++)
        if (mask & (1u << i)) { std::string s; s += (char)('A' + i); s += ":\\"; d.push_back(s); }
#else
    d.push_back("/");
#endif
    return d;
}

std::string homeDirectory() {
#ifdef _WIN32
    std::string h = env("USERPROFILE");
#else
    std::string h = env("HOME");
#endif
    if (!h.empty()) return h;
    std::error_code ec;
    return pathToUtf8(fs::current_path(ec));
}

static std::string existingSubfolder(const std::string& name) {
    std::error_code ec;
    fs::path p = pathFromUtf8(homeDirectory()) / name;
    return fs::is_directory(p, ec) ? pathToUtf8(p) : homeDirectory();
}
std::string documentsDirectory() { return existingSubfolder("Documents"); }
std::string desktopDirectory()   { return existingSubfolder("Desktop"); }

std::string appDataDirectory() {
    fs::path p;
#ifdef _WIN32
    std::string base = env("APPDATA");
    p = base.empty() ? pathFromUtf8(homeDirectory()) / "AppData" / "Roaming" : pathFromUtf8(base);
#elif defined(__APPLE__)
    p = pathFromUtf8(homeDirectory()) / "Library" / "Application Support";
#else
    std::string xdg = env("XDG_CONFIG_HOME");
    p = xdg.empty() ? pathFromUtf8(homeDirectory()) / ".config" : pathFromUtf8(xdg);
#endif
    p /= "MapHappy";
    std::error_code ec;
    fs::create_directories(p, ec);
    return pathToUtf8(p);
}
