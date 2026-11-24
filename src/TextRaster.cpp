#include "TextRaster.h"
#include "PathUtil.h"
#include "imgui.h"
#include "imgui_internal.h"     // ImTextCharFromUtf8
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>

namespace {

// A sans-serif font that ships with the operating system. None of these are copied into MapHappy.
std::string findSystemSans() {
    std::vector<std::string> candidates;
#ifdef _WIN32
    const char* win = std::getenv("WINDIR");
    const std::string fonts = std::string(win ? win : "C:\\Windows") + "\\Fonts\\";
    candidates = {fonts + "segoeui.ttf", fonts + "arial.ttf", fonts + "tahoma.ttf"};
#elif defined(__APPLE__)
    candidates = {"/System/Library/Fonts/Supplemental/Arial.ttf", "/Library/Fonts/Arial.ttf", "/System/Library/Fonts/Helvetica.ttc",
                  "/System/Library/Fonts/Geneva.ttf"};
#else
    candidates = {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                  "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
                  "/usr/share/fonts/truetype/freefont/FreeSans.ttf", "/usr/share/fonts/noto/NotoSans-Regular.ttf"};
#endif
    for (const std::string& c : candidates) { std::error_code ec; if (fs::is_regular_file(pathFromUtf8(c), ec)) return c; }
    return "";
}

// One font atlas per font, built on first use and kept (CPU memory only, so it outlives any OpenGL context).
struct Atlas {
    ImFontAtlas atlas;
    ImFont* font = nullptr;
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    bool system = false;
};

Atlas* atlasFor(TextFont which) {
    static std::unique_ptr<Atlas> cache[2];
    std::unique_ptr<Atlas>& slot = cache[(int)which];
    if (slot) return slot.get();
    slot = std::make_unique<Atlas>();
    ImFontConfig cfg;
    cfg.OversampleH = cfg.OversampleV = 1;      // one atlas pixel per output pixel: glyphs are copied, not resampled
    cfg.PixelSnapH = true;
    cfg.SizePixels = (float)kTextLineHeight;
    if (which == TextFont::Sans) {
        const std::string path = findSystemSans();
        if (!path.empty()) {
            slot->font = slot->atlas.AddFontFromFileTTF(path.c_str(), (float)kTextLineHeight, &cfg, slot->atlas.GetGlyphRangesDefault());
            slot->system = slot->font != nullptr;
        }
    }
    if (!slot->font) slot->font = slot->atlas.AddFontDefault(&cfg);       // the built-in pixel font, scaled up
    slot->atlas.GetTexDataAsAlpha8(&slot->pixels, &slot->w, &slot->h);
    return slot.get();
}

}  // namespace

bool systemSansAvailable() { return atlasFor(TextFont::Sans)->system; }

TextImage rasterizeText(const std::string& utf8, TextFont which, TextAlign align) {
    TextImage img;
    if (utf8.empty()) return img;
    Atlas* a = atlasFor(which);
    if (!a->font || !a->pixels) return img;
    ImFont* font = a->font;
    const float scale = (float)kTextLineHeight / font->FontSize;     // 1 unless the font was built at another size

    // split into lines of code points
    std::vector<std::vector<unsigned int>> lines(1);
    const char* p = utf8.c_str();
    const char* end = p + utf8.size();
    while (p < end) {
        unsigned int c = 0;
        const int n = ImTextCharFromUtf8(&c, p, end);
        p += n > 0 ? n : 1;
        if (c == '\r') continue;
        if (c == '\n') { lines.emplace_back(); continue; }
        if (c == '\t') c = ' ';
        lines.back().push_back(c);
    }
    std::vector<float> widths;
    float maxW = 0.0f;
    for (const auto& line : lines) {
        float w = 0.0f;
        for (unsigned int c : line) if (const ImFontGlyph* g = font->FindGlyph((ImWchar)c)) w += g->AdvanceX * scale;
        widths.push_back(w);
        maxW = std::max(maxW, w);
    }
    img.w = std::max(1, (int)std::ceil(maxW));
    img.h = (int)lines.size() * kTextLineHeight;
    img.alpha.assign((size_t)img.w * img.h, 0);

    for (size_t li = 0; li < lines.size(); li++) {
        float x = align == TextAlign::Left ? 0.0f : align == TextAlign::Right ? maxW - widths[li] : (maxW - widths[li]) * 0.5f;
        const float top = (float)(li * kTextLineHeight);
        for (unsigned int c : lines[li]) {
            const ImFontGlyph* g = font->FindGlyph((ImWchar)c);
            if (!g) continue;
            if (g->Visible) {
                const int gx0 = (int)std::floor(x + g->X0 * scale), gy0 = (int)std::floor(top + g->Y0 * scale);
                const int gw = std::max(1, (int)std::lround((g->X1 - g->X0) * scale)), gh = std::max(1, (int)std::lround((g->Y1 - g->Y0) * scale));
                for (int y = 0; y < gh; y++) {
                    const int dy = gy0 + y;
                    if (dy < 0 || dy >= img.h) continue;
                    const int sy = std::min(a->h - 1, (int)((g->V0 + (g->V1 - g->V0) * ((float)y + 0.5f) / (float)gh) * (float)a->h));
                    for (int xx = 0; xx < gw; xx++) {
                        const int dx = gx0 + xx;
                        if (dx < 0 || dx >= img.w) continue;
                        const int sx = std::min(a->w - 1, (int)((g->U0 + (g->U1 - g->U0) * ((float)xx + 0.5f) / (float)gw) * (float)a->w));
                        unsigned char& d = img.alpha[(size_t)dy * img.w + dx];
                        d = std::max(d, a->pixels[(size_t)sy * a->w + sx]);
                    }
                }
            }
            x += g->AdvanceX * scale;
        }
    }
    return img;
}
