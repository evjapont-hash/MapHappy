// The audio mixer (no sound card needed) and the device layer on miniaudio's built-in "null" device.
#include "TestSupport.h"
#include "Audio.h"
#include <chrono>
#include <thread>

static std::shared_ptr<SongBuffer> rampBuffer(size_t frames) {
    auto b = std::make_shared<SongBuffer>(); b->frames = frames; b->samples.resize(frames * 2);
    for (size_t i = 0; i < frames; i++) b->samples[i * 2] = b->samples[i * 2 + 1] = (float)i / (float)frames;
    return b;
}
static std::shared_ptr<SongBuffer> constBuffer(size_t frames, float v) {
    auto b = std::make_shared<SongBuffer>(); b->frames = frames; b->samples.assign(frames * 2, v); return b;
}
static std::shared_ptr<MixSnapshot> snapshotOf(std::initializer_list<MixClip> clips) { return std::make_shared<MixSnapshot>(clips); }

int main() {
    { AudioMixer m; std::vector<float> out(2 * 1024); m.render(out.data(), 1024);
      bool silent = true; for (float v : out) if (v != 0.0f) silent = false;
      expect("nothing plays and the clock stays still when stopped", silent && m.time() == 0.0); }

    { AudioMixer m; m.setSnapshot(snapshotOf({{constBuffer(kAudioRate * 4, 0.5f), 1.0, 0.0, 2.0, 1.0f}}));
      m.seek(0.5); m.play();
      std::vector<float> out(2 * 512); m.render(out.data(), 512);
      expect("a seek is applied by the audio thread", nearf(m.time(), 0.5 + 512.0 / kAudioRate, 1e-6));
      bool quiet = true; for (float v : out) if (v != 0.0f) quiet = false;
      expect("nothing plays before a clip begins", quiet);
      m.seek(0.999); std::vector<float> big(2 * 4800); m.render(big.data(), 4800);
      expect("silent right up to the clip's first frame", big[2 * 40] == 0.0f);
      expect("the sound fades in rather than clicking on", big[2 * 49] < 0.1f && big[2 * 49] >= 0.0f);
      expect("...and reaches full level", nearf(big[2 * 1000], 0.5, 1e-4)); }

    { AudioMixer m; auto buf = rampBuffer(kAudioRate * 10);
      m.setSnapshot(snapshotOf({{buf, 2.0, 3.0, 1.0, 0.5f}}));       // timeline 2 s plays the file from 3 s, at half gain
      m.seek(1.9); m.play();
      std::vector<float> out(2 * 4800 * 20);
      for (int i = 0; i < 200; i++) m.render(out.data() + (size_t)i * 960, 480);
      auto at = [&](double t) { return out[(size_t)std::llround((t - 1.9) * kAudioRate) * 2]; };
      expect("a clip's offset and gain land on exactly the right sample", nearf(at(2.5), ((2.5 - 2.0 + 3.0) / 10.0) * 0.5, 1e-4));
      expect("a clip stops exactly at its duration", at(3.01) == 0.0f); }

    { AudioMixer m; m.setSnapshot(snapshotOf({{constBuffer(kAudioRate * 5, 0.8f), 0.0, 0.0, 5.0, 1.0f}})); m.play();
      std::vector<float> out(2 * 1024); for (int i = 0; i < 4; i++) m.render(out.data(), 1024);
      const double before = m.time();
      m.pause(); m.render(out.data(), 1024);
      expect("pausing ramps the sound down instead of cutting it", out[0] > 0.5f && out[2 * 800] == 0.0f);
      const double paused = m.time();
      m.render(out.data(), 1024); m.render(out.data(), 1024);
      expect("the position stops advancing once paused", nearf(m.time(), paused, 1e-9) && paused - before < 0.01); }

    { AudioMixer m; m.setSnapshot(snapshotOf({{constBuffer(kAudioRate * 10, 0.6f), 0.0, 0.0, 10.0, 1.0f}})); m.play();
      std::vector<float> out(2 * 1024); for (int i = 0; i < 3; i++) m.render(out.data(), 1024);
      m.seek(7.0);
      expect("a pending seek is visible until the audio thread applies it", m.seekPending());
      m.render(out.data(), 1024);
      expect("a seek jumps the clock", !m.seekPending() && nearf(m.time(), 7.0 + 1024.0 / kAudioRate, 1e-6));
      expect("a seek during playback fades in, with no click", out[0] < 0.01f && out[2 * 100] > out[0] && nearf(out[2 * 400], 0.6, 1e-4)); }

    { AudioMixer m; m.setSnapshot(snapshotOf({{constBuffer(kAudioRate, 0.8f), 0.0, 0.0, 1.0, 1.0f}, {constBuffer(kAudioRate, 0.8f), 0.0, 0.0, 1.0, 1.0f}})); m.play();
      std::vector<float> out(2 * 2048); m.render(out.data(), 2048);
      expect("overlapping clips are summed, then limited to full scale", nearf(out[2 * 1500], 1.0, 1e-6)); }

    { AudioMixer m; m.setSnapshot(snapshotOf({{constBuffer(100, 0.5f), 0.0, 0.0, 5.0, 1.0f}})); m.play();
      std::vector<float> out(2 * 4096); m.render(out.data(), 4096);
      expect("a clip longer than its audio goes silent without reading past the end", out[2 * 2000] == 0.0f); }

    // ---- the device layer, on a fake device that just keeps time
    { AudioEngine eng; std::string err;
      const bool ok = eng.start(err, true);
      expect("the device layer starts (null device)", ok);
      if (ok) {
          eng.mixer().setSnapshot(snapshotOf({{constBuffer(kAudioRate * 10, 0.3f), 0.0, 0.0, 10.0, 1.0f}}));
          eng.mixer().seek(2.0); eng.mixer().play();
          // compare against the time that really passed: CI machines (macOS especially) can oversleep a lot
          const auto t0 = std::chrono::steady_clock::now();
          std::this_thread::sleep_for(std::chrono::milliseconds(700));
          const double t = eng.mixer().time();
          const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
          expect("the device drives the master clock at real-time speed", t - 2.0 > elapsed - 0.3 && t - 2.0 < elapsed + 0.1);
          const double ts = eng.mixer().timeSmooth();
          expect("the smoothed clock never runs more than 30 ms past the real one", ts >= t && ts - t <= 0.0305);
          eng.mixer().pause();
          std::this_thread::sleep_for(std::chrono::milliseconds(150));
          const double tp = eng.mixer().time();
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
          expect("pausing stops the clock", nearf(eng.mixer().time(), tp, 1e-9));
      } }
    return finishTest();
}
