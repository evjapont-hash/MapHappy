#pragma once
#include "AudioTypes.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

// Offline tempo and beat-grid detection. Runs once when a song is loaded.
//   1. Spectral flux: how much the spectrum increases from one short frame to the next (= onsets).
//   2. Autocorrelation of that onset curve finds the repeating period (the tempo).
//   3. A comb search around that tempo picks the exact BPM and where the beats fall (the phase).
// Good on music with a steady pulse; the BPM and offset can be corrected by hand in the inspector.
struct BeatInfo {
    double bpm = 0.0;           // 0 = nothing reliable found
    double offset = 0.0;        // seconds into the file where a beat lands (0 <= offset < one beat)
    double confidence = 0.0;    // roughly 0..1
};

namespace beatdetect {

constexpr int kWin = 1024, kHop = 256;
constexpr double kFps = (double)kAudioRate / kHop;      // onset-curve samples per second

inline void fft(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        float ang = -6.2831853f / (float)len;
        std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; k++) {
                std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// The onset strength curve, one value per kHop frames.
// The spectrum is split into frequency bands that are each normalised on their own, so a
// low kick counts as much as a noisy hi-hat (plain spectral flux is dominated by the hats).
inline std::vector<float> onsetCurve(const AudioBuffer& b) {
    const size_t n = b.frames;
    std::vector<float> empty;
    if (n < (size_t)kWin * 4) return empty;
    std::vector<float> window(kWin);
    for (int i = 0; i < kWin; i++) window[i] = 0.5f - 0.5f * std::cos(6.2831853f * i / (kWin - 1));

    // FFT bin edges (bin width = 48000/1024 = 46.9 Hz): ~45-140, 140-375, 375-1100, 1100-3000, 3000-6000, 6000-12000 Hz
    static const int edge[] = {1, 4, 9, 25, 65, 129, 257};
    constexpr int kBands = 6;
    static const float bandWeight[kBands] = {1.6f, 1.2f, 1.0f, 0.8f, 0.6f, 0.5f};

    const size_t frames = (n - kWin) / kHop;
    std::vector<std::vector<float>> band(kBands, std::vector<float>(frames, 0.0f));
    std::vector<float> prev(257, 0.0f), cur(257, 0.0f);
    std::vector<std::complex<float>> buf(kWin);
    for (size_t f = 0; f < frames; f++) {
        const size_t s = f * kHop;
        for (int i = 0; i < kWin; i++) {
            float m = 0.5f * (b.samples[(s + i) * 2] + b.samples[(s + i) * 2 + 1]);
            buf[i] = std::complex<float>(m * window[i], 0.0f);
        }
        fft(buf);
        for (int k = 1; k < 257; k++) cur[k] = std::log1p(80.0f * std::abs(buf[k]) / (float)kWin);
        for (int bd = 0; bd < kBands; bd++) {
            float flux = 0.0f;
            for (int k = edge[bd]; k < edge[bd + 1]; k++) flux += std::max(0.0f, cur[k] - prev[k]);
            band[bd][f] = flux / (float)(edge[bd + 1] - edge[bd]);
        }
        prev = cur;
    }

    // per band: remove the moving average (keep only sudden rises), normalise, then sum
    std::vector<float> out(frames, 0.0f);
    const int half = (int)(0.25 * kFps);
    for (int bd = 0; bd < kBands; bd++) {
        const std::vector<float>& o = band[bd];
        std::vector<float> r(frames);
        double run = 0.0; size_t lo = 0, hi = 0;
        for (size_t i = 0; i < frames; i++) {
            while (hi < frames && hi <= i + half) run += o[hi++];
            while (lo + half < i) run -= o[lo++];
            r[i] = std::max(0.0f, o[i] - (float)(run / (double)(hi - lo)));
        }
        double sum = 0.0;
        for (float v : r) sum += (double)v * v;
        double rms = std::sqrt(sum / (double)std::max<size_t>(1, frames));
        if (rms < 1e-9) continue;
        for (size_t i = 0; i < frames; i++) out[i] += bandWeight[bd] * (float)(r[i] / rms);
    }
    double sum = 0.0;
    for (float v : out) sum += (double)v * v;
    double rms = std::sqrt(sum / (double)std::max<size_t>(1, frames));
    if (rms > 1e-9) for (float& v : out) v = (float)(v / rms);
    return out;
}

inline double sampleAt(const std::vector<float>& o, double x) {
    if (x < 0 || x >= (double)o.size() - 1) return 0.0;
    size_t i = (size_t)x; double f = x - (double)i;
    return o[i] * (1.0 - f) + o[i + 1] * f;
}

}  // namespace beatdetect

inline BeatInfo detectBeats(const AudioBuffer& b) {
    using namespace beatdetect;
    BeatInfo info;
    std::vector<float> o = onsetCurve(b);
    if (o.size() < (size_t)(kFps * 6)) return info;                 // too short to find a tempo

    // --- tempo by autocorrelation, favouring plausible dance/pop tempos
    const double bpmLo = 70.0, bpmHi = 190.0;
    const int lagMin = (int)std::floor(60.0 * kFps / bpmHi), lagMax = (int)std::ceil(60.0 * kFps / bpmLo);
    auto ac = [&](int lag) {
        double s = 0.0; size_t cnt = 0;
        for (size_t i = 0; i + lag < o.size(); i++) { s += (double)o[i] * o[i + lag]; cnt++; }
        return cnt ? s / (double)cnt : 0.0;
    };
    // Two views of the same onset curve that make different mistakes:
    //   autocorrelation is strong at the beat period AND at "ghost" periods like 1.5 beats;
    //   the Fourier magnitude is strong at the beat rate AND its harmonics, but not at 2/3 of it.
    // Their product only keeps tempos that both agree on.
    auto fourier = [&](double bpm) {
        const double w = 6.283185307179586 * (bpm / 60.0) / kFps;
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < o.size(); i++) { re += o[i] * std::cos(w * (double)i); im += o[i] * std::sin(w * (double)i); }
        return std::sqrt(re * re + im * im) / (double)o.size();
    };
    double bestScore = -1.0; int bestLag = 0;
    for (int lag = lagMin; lag <= lagMax; lag++) {
        double bpm = 60.0 * kFps / lag;
        double prior = std::exp(-0.5 * std::pow(std::log2(bpm / 125.0) / 0.9, 2.0));
        double a = std::max(0.0, ac(lag) + 0.5 * ac(lag * 2) + 0.25 * ac(lag * 4));
        double f = fourier(bpm) + 0.5 * fourier(bpm * 2.0);
        double score = a * f * prior;
        if (score > bestScore) { bestScore = score; bestLag = lag; }
    }
    if (bestLag == 0 || bestScore <= 0.0) return info;

    // --- refine tempo and find the phase with a comb filter
    const double bpm0 = 60.0 * kFps / bestLag;
    double bestComb = -1.0, bestBpm = bpm0, bestPhase = 0.0;
    for (double bpm = bpm0 - 3.0; bpm <= bpm0 + 3.0; bpm += 0.05) {
        if (bpm < 60.0) continue;
        const double period = 60.0 * kFps / bpm;
        for (double ph = 0.0; ph < period; ph += 0.5) {
            double s = 0.0; size_t cnt = 0;
            for (double x = ph; x < (double)o.size() - 1; x += period) { s += sampleAt(o, x); cnt++; }
            s /= (double)std::max<size_t>(1, cnt);
            if (s > bestComb) { bestComb = s; bestBpm = bpm; bestPhase = ph; }
        }
    }
    // Calibrated against synthetic songs of known tempo: the peak lands ~13 ms after the frame start.
    double offset = bestPhase / kFps + 0.0129;
    const double beat = 60.0 / bestBpm;
    offset = std::fmod(std::fmod(offset, beat) + beat, beat);

    info.bpm = std::round(bestBpm * 100.0) / 100.0;
    info.offset = offset;
    info.confidence = std::max(0.0, std::min(1.0, bestComb / 4.0));
    return info;
}
