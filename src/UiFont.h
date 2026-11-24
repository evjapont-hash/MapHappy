#pragma once
// The editor's typeface: Inter (SIL Open Font License, see assets/fonts), built into the program so there is no file to lose.
#include "InterFontData.h"        // made by CMake from assets/fonts/Inter-Regular.ttf
#include "imgui.h"

inline void loadUiFont(float size = 15.0f) {
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;                              // the data is part of the program
    ImGui::GetIO().Fonts->AddFontFromMemoryTTF((void*)kInterFont, (int)sizeof(kInterFont), size, &cfg);
}
