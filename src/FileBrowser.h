#pragma once
#include "PathUtil.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

struct DirEntry {
    std::string name;
    bool isDir = false;
    uintmax_t size = 0;
    std::time_t mtime = 0;
};

inline std::string lowerCase(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Lists a folder: sub-folders first, then files that match (or all files), each sorted by name.
// Hidden entries (names starting with ".") are skipped.
inline bool listDirectory(const fs::path& dir, const std::vector<std::string>& exts, bool showAll,
                          std::vector<DirEntry>& out, std::string& error) {
    out.clear(); error.clear();
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) { error = "Can't open this folder"; return false; }
    for (fs::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) break;
        const fs::directory_entry& e = *it;
        std::string name = pathToUtf8(e.path().filename());
        if (name.empty() || name[0] == '.') continue;
        std::error_code e2;
        DirEntry d;
        d.name = name;
        d.isDir = e.is_directory(e2);
        if (!d.isDir) {
            if (!showAll && !exts.empty()) {
                std::string ext = lowerCase(pathToUtf8(e.path().extension()));
                if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
                if (std::find(exts.begin(), exts.end(), ext) == exts.end()) continue;
            }
            d.size = e.file_size(e2);
        }
        auto ft = e.last_write_time(e2);
        if (!e2) {
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            d.mtime = std::chrono::system_clock::to_time_t(sctp);
        }
        out.push_back(d);
    }
    std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.isDir != b.isDir) return a.isDir;
        return lowerCase(a.name) < lowerCase(b.name);
    });
    return true;
}

// A simple open/save dialog drawn with ImGui, so it looks and behaves the same on every platform.
class FileBrowser {
public:
    enum class Mode { Open, Save };

    // extensions: lowercase, no dot (e.g. {"mp4","mov"}). startPath may be a folder or a file.
    void open(Mode mode, const std::string& title, std::vector<std::string> extensions,
              const std::string& startPath, const std::string& defaultName = "", const std::string& saveExtension = "") {
        mode_ = mode; title_ = title; exts_ = std::move(extensions); saveExt_ = saveExtension;
        std::error_code ec;
        fs::path p = pathFromUtf8(startPath);
        if (startPath.empty() || !fs::exists(p, ec)) p = pathFromUtf8(documentsDirectory());
        else if (!fs::is_directory(p, ec)) p = p.parent_path();
        goTo(p);
        selected_.clear();
        std::snprintf(nameBuf_, sizeof(nameBuf_), "%s", defaultName.c_str());
        showAll_ = false; wantOpen_ = true; open_ = true; confirmOverwrite_ = false; focusName_ = true;
    }
    bool isOpen() const { return open_; }
    const std::string& currentFolder() const { return dirUtf8_; }

    // Call every frame. Returns true on the frame the user confirms a choice; the full path is put in `out`.
    bool draw(std::string& out) {
        if (!open_) return false;
        if (wantOpen_) { ImGui::OpenPopup(title_.c_str()); wantOpen_ = false; }
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(780, 500), ImGuiCond_Appearing);
        bool confirmed = false;
        if (ImGui::BeginPopupModal(title_.c_str(), &open_, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
            confirmed = drawContents(out);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !confirmOverwrite_) open_ = false;
            if (!open_ || confirmed) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        } else if (!wantOpen_) {
            open_ = false;
        }
        if (confirmed) open_ = false;
        return confirmed;
    }

private:
    void goTo(const fs::path& p) {
        std::error_code ec;
        fs::path c = fs::weakly_canonical(p, ec);
        if (ec) c = p;
        dir_ = c;
        dirUtf8_ = pathToUtf8(dir_);
        std::snprintf(pathBuf_, sizeof(pathBuf_), "%s", dirUtf8_.c_str());
        refresh();
        selected_.clear();
    }
    void refresh() {
        listDirectory(dir_, exts_, showAll_, entries_, error_);
    }
    std::string finalName() const {
        std::string n = nameBuf_;
        if (mode_ == Mode::Save && !saveExt_.empty() && !n.empty() && lowerCase(pathToUtf8(pathFromUtf8(n).extension())) != "." + saveExt_)
            n += "." + saveExt_;
        return n;
    }
    bool tryConfirm(std::string& out) {
        std::string n = finalName();
        if (n.empty()) return false;
        fs::path full = pathFromUtf8(n);
        if (full.is_relative()) full = dir_ / full;
        std::error_code ec;
        if (fs::is_directory(full, ec)) { goTo(full); nameBuf_[0] = '\0'; return false; }
        if (mode_ == Mode::Open && !fs::exists(full, ec)) { error_ = "That file doesn't exist"; return false; }
        if (mode_ == Mode::Save && fs::exists(full, ec) && !confirmOverwrite_) { confirmOverwrite_ = true; overwritePath_ = pathToUtf8(full); return false; }
        out = pathToUtf8(full);
        return true;
    }

    bool drawContents(std::string& out) {
        bool confirmed = false;
        // --- navigation row
        if (ImGui::Button("Up")) { if (dir_.has_parent_path() && dir_.parent_path() != dir_) goTo(dir_.parent_path()); }
        ImGui::SameLine();
        if (ImGui::Button("Home"))      goTo(pathFromUtf8(homeDirectory()));
        ImGui::SameLine();
        if (ImGui::Button("Documents")) goTo(pathFromUtf8(documentsDirectory()));
        ImGui::SameLine();
        if (ImGui::Button("Desktop"))   goTo(pathFromUtf8(desktopDirectory()));
        std::vector<std::string> drives = listDrives();
        if (drives.size() > 1) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70);
            if (ImGui::BeginCombo("##drive", "Drive")) {
                for (const std::string& d : drives) if (ImGui::Selectable(d.c_str())) goTo(pathFromUtf8(d));
                ImGui::EndCombo();
            }
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##path", pathBuf_, sizeof(pathBuf_), ImGuiInputTextFlags_EnterReturnsTrue)) {
            std::error_code ec; fs::path p = pathFromUtf8(pathBuf_);
            if (fs::is_directory(p, ec)) goTo(p); else { error_ = "That folder doesn't exist"; std::snprintf(pathBuf_, sizeof(pathBuf_), "%s", dirUtf8_.c_str()); }
        }

        // --- file list
        const float footerH = ImGui::GetFrameHeightWithSpacing() * 2.0f + 14.0f;
        if (ImGui::BeginTable("##files", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable,
                              ImVec2(0, -footerH))) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            for (const DirEntry& e : entries_) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                std::string label = e.isDir ? "[" + e.name + "]" : e.name;
                bool sel = (e.name == selected_);
                if (ImGui::Selectable((label + "##" + e.name).c_str(), sel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (e.isDir) { if (ImGui::IsMouseDoubleClicked(0)) { goTo(dir_ / pathFromUtf8(e.name)); ImGui::EndTable(); return false; } selected_ = e.name; }
                    else {
                        selected_ = e.name;
                        std::snprintf(nameBuf_, sizeof(nameBuf_), "%s", e.name.c_str());
                        if (ImGui::IsMouseDoubleClicked(0)) confirmed = tryConfirm(out);
                    }
                }
                ImGui::TableSetColumnIndex(1);
                if (!e.isDir) ImGui::TextDisabled("%s", sizeText(e.size).c_str());
                ImGui::TableSetColumnIndex(2);
                if (e.mtime) { char b[32]; std::tm tmv{}; 
#ifdef _WIN32
                    localtime_s(&tmv, &e.mtime);
#else
                    localtime_r(&e.mtime, &tmv);
#endif
                    std::strftime(b, sizeof(b), "%Y-%m-%d %H:%M", &tmv); ImGui::TextDisabled("%s", b); }
            }
            ImGui::EndTable();
        }
        if (!error_.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", error_.c_str());
        else ImGui::TextDisabled("%zu items", entries_.size());

        // --- footer
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(mode_ == Mode::Save ? "Save as" : "File");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-250);
        if (focusName_) { ImGui::SetKeyboardFocusHere(); focusName_ = false; }       // so you can start typing straight away
        if (ImGui::InputText("##name", nameBuf_, sizeof(nameBuf_), ImGuiInputTextFlags_EnterReturnsTrue)) confirmed = tryConfirm(out) || confirmed;
        ImGui::SameLine();
        if (ImGui::Checkbox("All files", &showAll_)) refresh();
        ImGui::SameLine();
        if (ImGui::Button(mode_ == Mode::Save ? "Save" : "Open", ImVec2(70, 0))) confirmed = tryConfirm(out) || confirmed;
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(70, 0))) open_ = false;

        if (confirmOverwrite_) {
            ImGui::OpenPopup("Replace file?");
            confirmOverwrite_ = false; overwriteAsked_ = true;
        }
        if (ImGui::BeginPopupModal("Replace file?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::Text("\"%s\" already exists. Replace it?", fileNameOf(overwritePath_).c_str());
            if (ImGui::Button("Replace", ImVec2(90, 0))) { out = overwritePath_; confirmed = true; overwriteAsked_ = false; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(90, 0))) { overwriteAsked_ = false; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        return confirmed;
    }

    static std::string sizeText(uintmax_t b) {
        char s[32];
        if (b >= (uintmax_t)1 << 30) std::snprintf(s, sizeof(s), "%.1f GB", (double)b / (1 << 30));
        else if (b >= (uintmax_t)1 << 20) std::snprintf(s, sizeof(s), "%.1f MB", (double)b / (1 << 20));
        else if (b >= 1024) std::snprintf(s, sizeof(s), "%.0f KB", (double)b / 1024);
        else std::snprintf(s, sizeof(s), "%llu B", (unsigned long long)b);
        return s;
    }

    Mode mode_ = Mode::Open;
    std::string title_ = "Open";
    std::vector<std::string> exts_;
    std::string saveExt_;
    fs::path dir_;
    std::string dirUtf8_;
    std::vector<DirEntry> entries_;
    std::string selected_, error_, overwritePath_;
    char pathBuf_[1024] = {0};
    char nameBuf_[512] = {0};
    bool open_ = false, wantOpen_ = false, showAll_ = false, confirmOverwrite_ = false, overwriteAsked_ = false, focusName_ = false;
};
