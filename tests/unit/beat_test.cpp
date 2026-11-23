// Tempo and beat-grid detection, checked against synthetic songs whose tempo and first beat are known exactly.
#include "TestSupport.h"
#include "Audio.h"
#include "BeatDetect.h"

struct Case { double bpm, offset; SongPattern pattern; const char* name; };

int main() {
    TempDir tmp("beat");
    const Case cases[] = {
        {100, 0.120, SongPattern::House,  "100 BPM house"},
        {110, 0.300, SongPattern::Techno, "110 BPM techno"},
        {120, 0.050, SongPattern::Break,  "120 BPM breakbeat"},
        {128, 0.210, SongPattern::House,  "128 BPM house"},
        {130, 0.370, SongPattern::House,  "130 BPM house"},
        {130, 0.100, SongPattern::Techno, "130 BPM techno"},
        {140, 0.180, SongPattern::Sparse, "140 BPM sparse"},
        {150, 0.260, SongPattern::Break,  "150 BPM breakbeat"},
    };
    int index = 0;
    for (const Case& c : cases) {
        const std::string path = tmp.file("song" + std::to_string(index++) + ".wav");
        if (!writeSyntheticSongFile(path, c.bpm, c.offset, 45.0, c.pattern, 100 + index)) { expect("the test song could be written", false); continue; }
        AudioBuffer buf; std::string err;
        if (!decodeAudioFile(path, buf, err)) { expect("the test song could be decoded", false); continue; }
        const BeatInfo bi = detectBeats(buf);
        const double beat = 60.0 / c.bpm;
        double d = std::fmod(bi.offset - c.offset, beat); if (d > beat / 2) d -= beat; if (d < -beat / 2) d += beat;
        char label[160];
        std::snprintf(label, sizeof(label), "%s: tempo within 0.6 BPM (found %.2f)", c.name, bi.bpm);
        expect(label, std::fabs(bi.bpm - c.bpm) < 0.6);
        std::snprintf(label, sizeof(label), "%s: beats land within 12 ms (off by %.1f ms)", c.name, d * 1000.0);
        expect(label, std::fabs(d) < 0.012);
    }
    // silence and noise must not invent a tempo with confidence
    { AudioBuffer silent; silent.frames = kAudioRate * 20; silent.samples.assign(silent.frames * 2, 0.0f);
      expect("silence has no tempo", detectBeats(silent).bpm == 0.0); }
    { AudioBuffer tiny; tiny.frames = 1000; tiny.samples.assign(tiny.frames * 2, 0.1f);
      expect("a clip too short to analyse is handled without crashing", detectBeats(tiny).bpm == 0.0); }
    // decoding
    { std::string err; AudioBuffer b;
      expect("a missing audio file reports an error", !decodeAudioFile(tmp.file("nope.wav"), b, err) && !err.empty());
      { std::ofstream(tmp.file("fake.mp3")) << "this is not audio"; }
      expect("a file that isn't audio reports an error instead of crashing", !decodeAudioFile(tmp.file("fake.mp3"), b, err) && !err.empty());
      { std::ofstream(tmp.file("empty.wav")); }
      expect("an empty file reports an error", !decodeAudioFile(tmp.file("empty.wav"), b, err)); }
    return finishTest();
}
