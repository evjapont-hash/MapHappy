#pragma once
// Shared helpers for the MapHappy tests. Nothing in here needs OpenGL, so every unit test can use it on any platform.
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES                          // M_PI on MSVC
#endif
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <vector>

namespace testing_ {
inline int& failures() { static int n = 0; return n; }
inline int& passes()   { static int n = 0; return n; }
}

// Records one check. Prints PASS/FAIL so a CI log shows exactly what ran.
inline void expect(const char* what, bool ok) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) testing_::passes()++; else testing_::failures()++;
}
inline bool nearf(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

// Call at the end of main(): prints a summary and returns the process exit code.
inline int finishTest() {
    std::printf("%s (%d passed, %d failed)\n", testing_::failures() ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", testing_::passes(), testing_::failures());
    return testing_::failures() ? 1 : 0;
}

// CTest treats this exit code as "skipped" (see tests/CMakeLists.txt), e.g. a test that needs a video file when none was given.
constexpr int kSkipExitCode = 77;
inline int skipTest(const char* why) { std::printf("SKIPPED: %s\n", why); return kSkipExitCode; }

inline void setTestEnv(const std::string& key, const std::string& value) {
#ifdef _WIN32
    _putenv_s(key.c_str(), value.c_str());
#else
    setenv(key.c_str(), value.c_str(), 1);
#endif
}
inline std::string getTestEnv(const char* key) { const char* v = std::getenv(key); return v ? v : ""; }

// A scratch folder that is deleted again afterwards.
class TempDir {
public:
    explicit TempDir(const std::string& name = "test") {
        std::random_device rd;
        path_ = std::filesystem::temp_directory_path() / ("maphappy_" + name + "_" + std::to_string(rd()));
        std::error_code ec; std::filesystem::remove_all(path_, ec);
        std::filesystem::create_directories(path_);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path_, ec); }
    TempDir(const TempDir&) = delete; TempDir& operator=(const TempDir&) = delete;
    const std::filesystem::path& path() const { return path_; }
    std::string str() const { return path_.u8string(); }
    std::string file(const std::string& name) const { return (path_ / name).u8string(); }
private:
    std::filesystem::path path_;
};

// A short video for the tests that need one. CI makes it with ffmpeg; locally set MAPHAPPY_TEST_VIDEO.
inline std::string testVideoPath() { return getTestEnv("MAPHAPPY_TEST_VIDEO"); }

// ---------------------------------------------------------------------------------------------
// Synthetic songs: a kick on every beat (or a pattern), snare, hi-hats and a bass note, at a known tempo
// and with the first beat at a known offset, so beat detection can be checked against the truth.
// Deterministic on every platform (only the raw bits of mt19937 are used).
// ---------------------------------------------------------------------------------------------
enum class SongPattern { House, Break, Techno, Half, Sparse };

inline std::vector<float> makeSyntheticSong(double bpm, double offsetSeconds, double seconds, SongPattern pattern, unsigned seed = 1) {
    const int sr = 48000;
    const size_t n = (size_t)(seconds * sr);
    std::vector<double> x(n, 0.0);
    std::mt19937 rng(seed);
    auto noise = [&]() { return ((double)(rng() >> 8) / (double)(1u << 24)) * 2.0 - 1.0; };     // -1..1
    const double beat = 60.0 / bpm;
    auto add = [&](double t, const std::vector<double>& s, double g) {
        long i = (long)std::llround(t * sr);
        if (i < 0 || (size_t)i >= n) return;
        for (size_t k = 0; k < s.size() && (size_t)i + k < n; k++) x[(size_t)i + k] += g * s[k];
    };
    std::vector<double> kick(sr / 4), snare(sr / 5), clap(sr / 5), hat(sr / 20);
    for (size_t i = 0; i < kick.size(); i++)  { double t = (double)i / sr; kick[i] = std::sin(2 * M_PI * (52 + 95 * std::exp(-t * 38)) * t) * std::exp(-t * 13); }
    for (size_t i = 0; i < snare.size(); i++) { double t = (double)i / sr; snare[i] = noise() * std::exp(-t * 26) * 0.6 + std::sin(2 * M_PI * 185 * t) * std::exp(-t * 21) * 0.4; clap[i] = noise() * std::exp(-t * 35) * 0.7; }
    for (size_t i = 0; i < hat.size(); i++)   { double t = (double)i / sr; hat[i] = noise() * std::exp(-t * 80) * 0.3; }
    const int beats = (int)((seconds - offsetSeconds) / beat) + 2;
    for (int k = 0; k < beats; k++) {
        const double t = offsetSeconds + k * beat; const int b = k % 4;
        switch (pattern) {
            case SongPattern::House:  add(t, kick, 0.9); if (b == 1 || b == 3) add(t, clap, 0.5); add(t + beat / 2, hat, 1.0); break;
            case SongPattern::Break:  if (b == 0) add(t, kick, 0.95); if (b == 1) add(t + beat / 2, kick, 0.8); if (b == 1 || b == 3) add(t, snare, 0.6); add(t, hat, 0.6); add(t + beat / 2, hat, 0.9); break;
            case SongPattern::Techno: add(t, kick, 0.9); for (int s = 0; s < 4; s++) add(t + s * beat / 4, hat, s % 2 == 0 ? 0.5 : 0.8); break;
            case SongPattern::Half:   if (b == 0) add(t, kick, 0.95); if (b == 2) add(t, snare, 0.7); add(t, hat, 0.7); add(t + beat / 2, hat, 0.7); break;
            case SongPattern::Sparse: add(t, kick, 0.9); if (b == 3) add(t + beat / 2, kick, 0.6); if (k % 2 == 1) add(t, hat, 0.8); break;
        }
        if (k % 8 == 0) {                                          // a bass pad, so there is some non-rhythmic energy too
            const double f = (k / 8) % 2 == 0 ? 41.2 : 55.0;
            std::vector<double> pad((size_t)(beat * 8 * sr));
            for (size_t i = 0; i < pad.size(); i++) { double tt = (double)i / sr; pad[i] = 0.12 * std::sin(2 * M_PI * f * tt) * std::min(1.0, tt * 20); }
            add(t, pad, 1.0);
        }
    }
    double peak = 1e-9;
    for (double& v : x) { v += noise() * 0.008; peak = std::max(peak, std::fabs(v)); }
    std::vector<float> out(n);
    for (size_t i = 0; i < n; i++) out[i] = (float)(x[i] / peak * 0.9);
    return out;
}

// Writes mono samples as a 16-bit stereo WAV at 48 kHz.
inline bool writeWav(const std::string& path, const std::vector<float>& mono) {
    std::ofstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) return false;
    const uint32_t sr = 48000, dataBytes = (uint32_t)(mono.size() * 4), riff = 36 + dataBytes;
    auto w32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
    auto w16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
    f.write("RIFF", 4); w32(riff); f.write("WAVEfmt ", 8); w32(16); w16(1); w16(2); w32(sr); w32(sr * 4); w16(4); w16(16);
    f.write("data", 4); w32(dataBytes);
    for (float v : mono) { int16_t s = (int16_t)std::lround(std::max(-1.0f, std::min(1.0f, v)) * 32767.0f); f.write((const char*)&s, 2); f.write((const char*)&s, 2); }
    return (bool)f;
}

inline bool writeSyntheticSongFile(const std::string& path, double bpm, double offset, double seconds, SongPattern p = SongPattern::House, unsigned seed = 1) {
    return writeWav(path, makeSyntheticSong(bpm, offset, seconds, p, seed));
}
