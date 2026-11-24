// The media generators' parameter tables, value checks, project-file format, strobe timing and text layout.
// No OpenGL: the pictures themselves are checked in tests/gl/generators_test.cpp.
#include "TestSupport.h"
#include "GeneratorDefs.h"
#include "TextRaster.h"
#include <set>
using json = nlohmann::json;

static int inkColumns(const TextImage& img) {        // columns that contain any ink
    int n = 0;
    for (int x = 0; x < img.w; x++) { bool any = false; for (int y = 0; y < img.h && !any; y++) any = img.alpha[(size_t)y * img.w + x] > 128; n += any; }
    return n;
}
static int inkRows(const TextImage& img, int y0, int y1) {
    int n = 0;
    for (int y = y0; y < y1; y++) { bool any = false; for (int x = 0; x < img.w && !any; x++) any = img.alpha[(size_t)y * img.w + x] > 128; n += any; }
    return n;
}

int main() {
    // ---- the tables
    expect("all seven generator types exist", genTypes().size() == 7);
    { std::set<std::string> keys; bool unique = true, sane = true;
      for (const GenTypeInfo& t : genTypes()) {
          unique &= keys.insert(t.key).second;
          std::set<std::string> pk;
          for (const GenParamDef& d : t.params) {
              unique &= pk.insert(d.key).second;
              if (d.kind != GenParamKind::Text && d.kind != GenParamKind::Color) sane &= d.min <= d.def[0] && d.def[0] <= d.max;
              if (d.kind == GenParamKind::Choice) sane &= !d.choices.empty() && d.max == (float)d.choices.size() - 1;
          }
      }
      expect("type keys and parameter keys are unique", unique);
      expect("every default lies inside its range", sane); }
    expect("types are found by their file key", genTypeFromKey("strobe") && genTypeFromKey("strobe")->type == GenType::Strobe && !genTypeFromKey("nope"));
    expect("only the solid colour is still; the others move", !genTypeInfo(GenType::Solid).animated && genTypeInfo(GenType::Strobe).animated && genTypeInfo(GenType::Text).animated);

    // ---- settings and value checks
    { GenSettings s = GenSettings::defaults(GenType::Grid);
      expect("defaults are filled in", s.num("columns") == 16 && s.num("rows") == 9 && s.color("lineColor")[0] == 1.0f && s.width == 1920 && s.height == 1080);
      s.setNum("columns", 1000); expect("a number above its range is clamped", s.num("columns") == 128);
      s.setNum("columns", 7.6f); expect("whole-number parameters are rounded", s.num("columns") == 8);
      s.setNum("thickness", NAN); expect("not-a-number falls back to the default", s.num("thickness") == 3);
      s.setColor("background", {2.0f, -1.0f, 0.5f, 1.0f}); expect("colours are kept within 0..1", s.color("background") == GenColor{1.0f, 0.0f, 0.5f, 1.0f});
      expect("unknown parameters are refused", !s.setNum("nope", 1) && !s.setText("columns", "x") && !s.setColor("columns", {0, 0, 0, 1}));
      s.setSize(5, 100000); expect("the picture size is kept between 16 and 8192", s.width == 16 && s.height == 8192); }
    { GenSettings s = GenSettings::defaults(GenType::Text);
      expect("text defaults", s.text("text") == "HELLO" && s.num("align") == 1);
      s.setNum("align", 9); expect("a choice is clamped to the last option", s.num("align") == 2); }

    // ---- project file
    { GenSettings s = GenSettings::defaults(GenType::Text);
      s.setText("text", "Line one\nL\xC3\xADnea \"dos\""); s.setNum("align", 0); s.setNum("size", 0.4f); s.setColor("color", {0.1f, 0.2f, 0.3f, 0.5f}); s.setSize(1280, 720);
      const json j = genToJson(s);
      expect("choices are saved by name, not by number", j["params"]["align"] == "left" && j["params"]["font"] == "sans");
      expect("colours are saved as RGBA lists", j["params"]["color"].is_array() && j["params"]["color"].size() == 4);
      GenSettings back; const bool ok = genFromJson(json::parse(j.dump()), back);
      expect("a generator survives a save and load exactly", ok && back == s); }
    { bool all = true;
      for (const GenTypeInfo& t : genTypes()) { GenSettings s = GenSettings::defaults(t.type), back; all &= genFromJson(genToJson(s), back) && back == s; }
      expect("every type's defaults survive a round trip", all); }
    { GenSettings back; std::vector<std::string> warn;
      const json bad = {{"type", "grid"}, {"width", 99999}, {"params", {{"columns", 5000}, {"lineColor", "red"}, {"background", {0.2, 0.3, 0.4}}, {"scrollX", 1.5}}}};
      expect("a damaged generator still loads", genFromJson(bad, back, &warn));
      expect("...bad values are corrected and reported", back.num("columns") == 128 && back.color("lineColor")[0] == 1.0f && back.width == 8192 && !warn.empty());
      expect("...good values are kept (an RGB colour gets full alpha)", back.color("background") == GenColor{0.2f, 0.3f, 0.4f, 1.0f} && back.num("scrollX") == 1.5f);
      GenSettings older; expect("a file from before a parameter existed gets its default", genFromJson(json{{"type", "lines"}, {"params", {{"count", 4}}}}, older) && older.num("count") == 4 && older.num("speed") == 0.5f);
      expect("an unknown generator type is refused", !genFromJson(json{{"type", "laser"}}, back) && !genFromJson(json::array(), back)); }

    // ---- strobe timing (the shader uses the same phase)
    expect("strobe phase wraps once per flash", nearf(strobePhase(0.30, 4.0f), 0.2, 1e-9) && nearf(strobePhase(10.0, 4.0f), 0.0, 1e-9));
    expect("strobe is on for the duty part of each cycle", strobeLevel(0.10, 0.25f, 0.0f) == 1.0f && strobeLevel(0.30, 0.25f, 0.0f) == 0.0f);
    expect("with fade it dims linearly after the flash", nearf(strobeLevel(0.35, 0.25f, 0.2f), 0.5, 1e-6) && strobeLevel(0.50, 0.25f, 0.2f) == 0.0f);
    expect("phase stays precise after hours of playing", nearf(strobePhase(3.0 * 3600.0 + 0.125, 4.0f), 0.5, 1e-6));

    // ---- text
    for (TextFont font : {TextFont::Sans, TextFont::Pixel}) {
        const char* name = font == TextFont::Sans ? (systemSansAvailable() ? " (system sans)" : " (sans fell back to pixel)") : " (pixel)";
        const TextImage hi = rasterizeText("HI", font), longer = rasterizeText("HI HI HI", font);
        expect((std::string("text has ink, one line tall") + name).c_str(), !hi.empty() && hi.h == kTextLineHeight && inkColumns(hi) > 10 && inkRows(hi, 0, hi.h) > kTextLineHeight / 3);
        expect((std::string("longer text is wider") + name).c_str(), longer.w > hi.w * 2);
        const TextImage two = rasterizeText("HI\nHI", font);
        expect((std::string("a new line adds a line below") + name).c_str(), two.h == 2 * kTextLineHeight && inkRows(two, kTextLineHeight, 2 * kTextLineHeight) > kTextLineHeight / 3);
    }
    expect("empty text makes an empty image", rasterizeText("").empty());
    { const TextImage l = rasterizeText("WWWW\nI", TextFont::Pixel, TextAlign::Left), r = rasterizeText("WWWW\nI", TextFont::Pixel, TextAlign::Right);
      auto firstInk = [](const TextImage& img, int line) { for (int x = 0; x < img.w; x++) for (int y = line * kTextLineHeight; y < (line + 1) * kTextLineHeight; y++) if (img.alpha[(size_t)y * img.w + x] > 128) return x; return -1; };
      expect("left and right alignment place a short line at either side", firstInk(l, 1) >= 0 && firstInk(l, 1) < l.w / 4 && firstInk(r, 1) > r.w / 2); }
    expect("accented letters and unknown characters don't crash", !rasterizeText("Caf\xC3\xA9 \xE2\x9C\x93 \xF0\x9F\x8E\x89").empty());
    return finishTest();
}
