#pragma once
#include "Project.h"
#include <string>
#include <vector>

// Saving and loading shows. The file is plain JSON, so it is readable, diff-able in git, and
// forgiving: unknown fields are ignored and missing ones fall back to defaults.
//
// Loading never leaves a half-loaded project: the file is fully read and checked first, and the
// current project is only replaced once that succeeds.

struct LoadReport {
    std::vector<std::string> warnings;     // things that were fixed or couldn't be found (shown to the user)
    std::string error;                     // set when the file couldn't be used at all
    std::vector<int> audioToLoad;          // audio assets whose sound still needs decoding (the app does this in the background)
};

// The project as JSON text. forDirtyCheck leaves out values a timeline is currently driving, so
// merely playing a show doesn't count as an unsaved change.
std::string projectToJson(const Project& p, const std::string& projectFilePath, bool forDirtyCheck = false);

// Writes the project atomically (temp file, then rename), so a crash mid-save can't wreck the old file.
bool saveProjectToFile(const Project& p, const std::string& path, std::string& error);

// Replaces `p` with the project in the file. Must be called between frames (it frees GPU textures).
bool loadProjectFromFile(Project& p, const std::string& path, LoadReport& report);

// Same, from JSON text (used by tests and by autosave recovery). `baseDir` is where relative media paths start.
bool loadProjectFromJson(Project& p, const std::string& text, const std::string& baseDir, LoadReport& report);

// ---- app settings (recent files)
struct AppSettings {
    std::vector<std::string> recent;       // most recent first, at most 10
    std::string lastFolder;
    std::string outputMonitor;             // the display last used for the projector output
    int outputMonitorIndex = -1;
    std::string outputMode = "auto";      // auto | fullscreen | windowed
    std::string outputScale = "fit";      // fit | stretch
};
AppSettings loadSettings();
void saveSettings(const AppSettings& s);
void addRecentFile(AppSettings& s, const std::string& path);

// ---- autosave
std::string autosavePathFor(const std::string& projectPath);       // "" project = untitled
bool autosaveIsNewer(const std::string& projectPath);              // an autosave exists and is newer than the saved file
void deleteAutosave(const std::string& projectPath);
bool readTextFile(const std::string& path, std::string& out);
bool writeTextFileAtomic(const std::string& path, const std::string& text, std::string& error);
