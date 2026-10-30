#pragma once
#include "AudioMixer.h"
#include "AudioTypes.h"
#include <memory>
#include <string>

// Owns the sound card connection. The OS audio thread calls AudioMixer::render(); everything
// else (play/pause/seek/time) is a thread-safe call into the mixer.
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Opens the default output device. useNullDevice = a fake device that just keeps time (for tests).
    bool start(std::string& error, bool useNullDevice = false);
    bool available() const { return ok_; }
    AudioMixer& mixer() { return mixer_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    AudioMixer mixer_;
    bool ok_ = false;
};
