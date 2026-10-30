#pragma once
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Everything audio is decoded to interleaved 32-bit float stereo at one fixed rate,
// so mixing never has to resample.
constexpr int kAudioRate = 48000;

struct AudioBuffer {
    std::vector<float> samples;       // interleaved L R L R ...
    size_t frames = 0;                // sample frames (one frame = L + R)
    double duration() const { return (double)frames / kAudioRate; }
};

constexpr int kPeakBlock = 128;       // frames summarised by one waveform bucket

// A decoded song plus everything we computed from it.
struct AudioAsset {
    int         id = 0;
    std::string name, path;
    std::shared_ptr<const AudioBuffer> buf;
    std::vector<float> peakMin, peakMax;      // one pair per kPeakBlock frames, for drawing the waveform
    double bpm = 0.0;                         // 0 = unknown
    double beatOffset = 0.0;                  // seconds into the file where a beat lands
    double duration() const { return buf ? buf->duration() : 0.0; }
};

// Decodes WAV / MP3 / FLAC into our fixed format. Blocking; run it on a worker thread for big files.
bool decodeAudioFile(const std::string& path, AudioBuffer& out, std::string& error);

// Min/max per block, for the waveform display.
inline void computePeaks(const AudioBuffer& b, std::vector<float>& mn, std::vector<float>& mx) {
    size_t blocks = (b.frames + kPeakBlock - 1) / kPeakBlock;
    mn.assign(blocks, 0.0f); mx.assign(blocks, 0.0f);
    for (size_t k = 0; k < blocks; k++) {
        size_t a = k * kPeakBlock, e = std::min(b.frames, a + kPeakBlock);
        float lo = 1e9f, hi = -1e9f;
        for (size_t i = a; i < e; i++) {
            float m = 0.5f * (b.samples[i * 2] + b.samples[i * 2 + 1]);
            lo = std::min(lo, m); hi = std::max(hi, m);
        }
        mn[k] = lo; mx[k] = hi;
    }
}
