#pragma once
#include <string>
#include <vector>

// Turns text into an 8-bit coverage image (255 = ink), for the Text and Test Card generators.
// Uses the font builder that comes with Dear ImGui, so there is no extra dependency, and needs no OpenGL.
//
// Lines are kTextLineHeight pixels tall; the generators scale the image to whatever size they show it at.
constexpr int kTextLineHeight = 96;

enum class TextFont { Sans = 0, Pixel = 1 };
enum class TextAlign { Left = 0, Center = 1, Right = 2 };

struct TextImage {
    int w = 0, h = 0;                    // h = number of lines * kTextLineHeight (0 for empty text)
    std::vector<unsigned char> alpha;    // w * h, row 0 = TOP line
    bool empty() const { return w == 0 || h == 0; }
};

// Lays out UTF-8 text ('\n' starts a new line), each line aligned within the widest one.
TextImage rasterizeText(const std::string& utf8, TextFont font = TextFont::Sans, TextAlign align = TextAlign::Center);

// True when the Sans font is a real system font (false = it fell back to the built-in pixel font).
bool systemSansAvailable();
