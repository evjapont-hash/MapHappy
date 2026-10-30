// The only file that includes miniaudio. Kept separate so the rest of the project compiles fast.
#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#endif

#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "Audio.h"
#include <algorithm>

bool decodeAudioFile(const std::string& path, AudioBuffer& out, std::string& error) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, kAudioRate);
    ma_decoder dec;
#ifdef _WIN32
    // Paths arrive as UTF-8 (from the window's drop event); Windows file APIs want UTF-16.
    int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wpath(n > 0 ? (size_t)n - 1 : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wpath[0], n);
    ma_result r = ma_decoder_init_file_w(wpath.c_str(), &cfg, &dec);
#else
    ma_result r = ma_decoder_init_file(path.c_str(), &cfg, &dec);
#endif
    if (r != MA_SUCCESS) { error = "Could not open this audio file (WAV, MP3 and FLAC are supported)"; return false; }

    ma_uint64 total = 0;
    ma_decoder_get_length_in_pcm_frames(&dec, &total);
    out.samples.clear();
    if (total > 0 && total < (ma_uint64)1 << 31) out.samples.reserve((size_t)total * 2);

    std::vector<float> chunk(4096 * 2);
    for (;;) {
        ma_uint64 got = 0;
        ma_result rr = ma_decoder_read_pcm_frames(&dec, chunk.data(), 4096, &got);
        if (got > 0) out.samples.insert(out.samples.end(), chunk.begin(), chunk.begin() + (size_t)got * 2);
        if (rr != MA_SUCCESS || got == 0) break;
    }
    ma_decoder_uninit(&dec);
    out.frames = out.samples.size() / 2;
    if (out.frames == 0) { error = "This file contains no audio"; return false; }
    return true;
}

struct AudioEngine::Impl {
    ma_context context;
    ma_device  device;
    bool contextInit = false, deviceInit = false;
};

static void dataCallback(ma_device* dev, void* output, const void*, ma_uint32 frames) {
    static_cast<AudioMixer*>(dev->pUserData)->render(static_cast<float*>(output), frames);
}

AudioEngine::AudioEngine() : impl_(new Impl) {}

AudioEngine::~AudioEngine() {
    if (impl_->deviceInit)  ma_device_uninit(&impl_->device);      // stops the audio thread before the mixer goes away
    if (impl_->contextInit) ma_context_uninit(&impl_->context);
}

bool AudioEngine::start(std::string& error, bool useNullDevice) {
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate        = kAudioRate;
    cfg.periodSizeInMilliseconds = 10;
    cfg.performanceProfile = ma_performance_profile_low_latency;
    cfg.dataCallback = dataCallback;
    cfg.pUserData    = &mixer_;

    ma_context* ctx = nullptr;
    if (useNullDevice) {
        ma_backend backends[] = {ma_backend_null};
        if (ma_context_init(backends, 1, nullptr, &impl_->context) != MA_SUCCESS) { error = "Could not create the test audio context"; return false; }
        impl_->contextInit = true;
        ctx = &impl_->context;
    }
    if (ma_device_init(ctx, &cfg, &impl_->device) != MA_SUCCESS) { error = "No audio output device found"; return false; }
    impl_->deviceInit = true;
    if (ma_device_start(&impl_->device) != MA_SUCCESS) { error = "Could not start the audio output device"; return false; }
    ok_ = true;
    return true;
}
