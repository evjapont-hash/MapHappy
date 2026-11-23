// The master-clock logic: who owns "now" (the audio device, or a plain timer).
#include "TestSupport.h"
#include "TransportClock.h"

// One UI frame of dt seconds, followed by the audio thread delivering that much sound.
static void frame(Transport& tr, AudioMixer& m, double dt, double length) {
    driveTransport(tr, &m, dt, length);
    std::vector<float> out(2 * 4096);
    uint32_t n = (uint32_t)(dt * kAudioRate);
    while (n > 0) { uint32_t c = std::min<uint32_t>(n, 480); m.render(out.data(), c); n -= c; }
}

int main() {
    { Transport tr; tr.playing = true; tr.seeked = true;
      driveTransport(tr, nullptr, 0.5, 10.0);
      expect("without a sound device the transport counts with a timer", nearf(tr.time, 0.5) && !tr.seeked); }
    { AudioMixer m; Transport tr; tr.time = 12.0; tr.playing = true;
      driveTransport(tr, &m, 1.0 / 60, 100.0);
      expect("pressing play starts the audio and queues a seek to the playhead", m.playing() && m.seekPending());
      expect("the playhead does not jump back while that seek is pending", nearf(tr.time, 12.0));
      for (int i = 0; i < 60; i++) frame(tr, m, 1.0 / 60, 100.0);
      expect("after one second the playhead has moved one second", nearf(tr.time, 13.0, 0.05));
      expect("...and it equals the audio clock", nearf(tr.time, m.time(), 0.035)); }
    { AudioMixer m; Transport tr; tr.playing = true; double prev = 0; bool mono = true;
      for (int i = 0; i < 120; i++) { frame(tr, m, 1.0 / 60, 100.0); if (tr.time + 1e-12 < prev) mono = false; prev = tr.time; }
      expect("the playhead never runs backwards from jitter", mono && tr.time > 1.5); }
    { AudioMixer m; Transport tr; tr.playing = true;
      for (int i = 0; i < 30; i++) frame(tr, m, 1.0 / 60, 100.0);
      tr.playing = false; driveTransport(tr, &m, 1.0 / 60, 100.0);
      expect("pausing stops the audio", !m.playing());
      const double held = tr.time;
      for (int i = 0; i < 20; i++) frame(tr, m, 1.0 / 60, 100.0);
      expect("the playhead holds while paused", nearf(tr.time, held));
      tr.playing = true;
      for (int i = 0; i < 30; i++) frame(tr, m, 1.0 / 60, 100.0);
      expect("resuming continues from where it paused", nearf(tr.time, held + 0.5, 0.05)); }
    { AudioMixer m; Transport tr; tr.time = 30.0; tr.seeked = true;
      frame(tr, m, 1.0 / 60, 100.0);
      expect("scrubbing while paused moves the audio clock", nearf(m.time(), 30.0, 0.02) && !tr.seeked);
      tr.playing = true;
      for (int i = 0; i < 30; i++) frame(tr, m, 1.0 / 60, 100.0);
      expect("playing after a scrub starts from the scrubbed time", nearf(tr.time, 30.5, 0.05)); }
    { AudioMixer m; Transport tr; tr.avOffset = 0.1; tr.time = 5.0; tr.playing = true;
      driveTransport(tr, &m, 1.0 / 60, 100.0);
      std::vector<float> out(2 * 480); m.render(out.data(), 480);
      expect("with a +100 ms offset the audio starts 100 ms ahead of the picture", nearf(m.time(), 5.1 + 480.0 / kAudioRate, 1e-3));
      for (int i = 0; i < 60; i++) frame(tr, m, 1.0 / 60, 100.0);
      expect("...and the picture stays 100 ms behind the audio clock", nearf(m.time() - tr.time, 0.1, 0.04)); }
    { AudioMixer m; Transport tr; tr.time = 9.9; tr.playing = true;
      for (int i = 0; i < 30; i++) frame(tr, m, 1.0 / 60, 10.0);
      expect("playback stops at the end of the timeline", !tr.playing && nearf(tr.time, 10.0) && !m.playing()); }
    { AudioMixer m; Transport tr; tr.time = 9.9; tr.playing = true; tr.loop = true;
      for (int i = 0; i < 30; i++) frame(tr, m, 1.0 / 60, 10.0);
      expect("looping returns to the start and keeps playing", tr.playing && tr.time < 1.0 && m.playing()); }
    return finishTest();
}
