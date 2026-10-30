#pragma once
#include "AudioTypes.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

// What the audio thread needs to know about one clip. The UI builds a fresh list whenever
// something changes and swaps it in atomically; the audio thread only ever reads a finished list.
struct MixClip {
    std::shared_ptr<const AudioBuffer> buf;
    double start = 0.0;               // timeline seconds where the clip begins
    double offset = 0.0;              // seconds skipped at the start of the file
    double duration = 0.0;            // seconds of the file that play
    float  gain = 1.0f;
    bool operator==(const MixClip& o) const {
        return buf == o.buf && start == o.start && offset == o.offset && duration == o.duration && gain == o.gain;
    }
};
using MixSnapshot = std::vector<MixClip>;

// Renders the timeline's audio for the device. Contains no OS or library calls, so it is unit-testable.
// The clock is a frame counter that advances only while playing; it IS the show's master clock.
class AudioMixer {
public:
    void setSnapshot(std::shared_ptr<const MixSnapshot> s) { std::atomic_store(&snap_, std::move(s)); }

    void play()  { wantPlay_ = true; }
    void pause() { wantPlay_ = false; }
    void seek(double t) { seekTo_ = (int64_t)std::llround(std::max(0.0, t) * kAudioRate); }
    bool playing() const { return wantPlay_; }
    bool seekPending() const { return seekTo_.load() >= 0; }     // a seek has been requested but the audio thread hasn't applied it yet
    double time() const { return (double)pos_.load() / kAudioRate; }

    // Like time(), but advances smoothly between audio callbacks (which arrive every ~10 ms)
    // so animation doesn't step. Never runs more than 30 ms ahead of the last callback.
    double timeSmooth() const {
        double t = time();
        if (!wantPlay_) return t;
        int64_t ns = lastNs_.load();
        if (ns == 0) return t;
        double dt = (nowNs() - ns) * 1e-9;
        return t + std::max(0.0, std::min(0.03, dt));
    }
    // Called from the audio thread. Writes `frames` stereo frames.
    void render(float* out, uint32_t frames) {
        std::memset(out, 0, sizeof(float) * 2 * frames);
        int64_t s = seekTo_.exchange(-1);
        if (s >= 0) { pos_ = s; gain_ = 0.0f; }                     // a jump: fade in to avoid a click
        const bool want = wantPlay_;
        if (want && !running_) { running_ = true; gain_ = 0.0f; }
        if (!running_) return;

        std::shared_ptr<const MixSnapshot> snap = std::atomic_load(&snap_);
        const int64_t p0 = pos_;
        if (snap) {
            for (const MixClip& c : *snap) {
                if (!c.buf || c.duration <= 0.0) continue;
                const int64_t cs = (int64_t)std::llround(c.start * kAudioRate);
                const int64_t ce = cs + (int64_t)std::llround(c.duration * kAudioRate);
                const int64_t off = (int64_t)std::llround(c.offset * kAudioRate);
                const int64_t a = std::max(p0, cs), b = std::min(p0 + (int64_t)frames, ce);
                for (int64_t t = a; t < b; t++) {
                    int64_t src = t - cs + off;
                    if (src < 0 || src >= (int64_t)c.buf->frames) continue;
                    const float* in = &c.buf->samples[(size_t)src * 2];
                    float* o = out + (t - p0) * 2;
                    o[0] += in[0] * c.gain; o[1] += in[1] * c.gain;
                }
            }
        }
        // fade in/out and hard-limit
        const float target = want ? 1.0f : 0.0f, step = 1.0f / 384.0f;
        float g = gain_;
        int advanced = (int)frames;
        for (uint32_t i = 0; i < frames; i++) {
            g = (g < target) ? std::min(target, g + step) : std::max(target, g - step);
            for (int ch = 0; ch < 2; ch++) out[i * 2 + ch] = std::max(-1.0f, std::min(1.0f, out[i * 2 + ch] * g));
            if (!want && g <= 0.0f) { advanced = (int)i + 1; running_ = false; std::memset(out + (i + 1) * 2, 0, sizeof(float) * 2 * (frames - i - 1)); break; }
        }
        gain_ = g;
        pos_ = p0 + advanced;
        lastNs_ = nowNs();
    }

private:
    static int64_t nowNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    std::shared_ptr<const MixSnapshot> snap_;
    std::atomic<int64_t> lastNs_{0};
    std::atomic<bool>    wantPlay_{false};
    std::atomic<int64_t> seekTo_{-1};
    std::atomic<int64_t> pos_{0};
    bool  running_ = false;           // audio-thread only
    float gain_ = 0.0f;               // audio-thread only
};
