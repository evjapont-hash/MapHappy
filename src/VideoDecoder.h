#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;

// Decodes a video file on a background thread into RGBA frames.
// No OpenGL in here, so it can be tested on its own.
//
// Frame data is RGBA, 4 bytes per pixel, rows ordered BOTTOM-TO-TOP
// (OpenGL's convention), so it can be uploaded to a texture as-is.
class VideoDecoder {
public:
    VideoDecoder() = default;
    ~VideoDecoder() { close(); }
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    bool open(const std::string& path, std::string& error);
    void close();

    bool   isOpen() const { return fmt_ != nullptr; }
    int    width()  const { return width_; }
    int    height() const { return height_; }
    double fps()    const { return fps_; }
    bool   finished() const { return finished_; }   // reached the end (only when not looping)

    void setLoop(bool loop) { loop_ = loop; }

    // Call from the main thread. If one or more frames are due at `clock` seconds,
    // moves the newest due frame into `out` and returns true.
    bool getFrameFor(double clock, std::vector<uint8_t>& out);

private:
    struct Frame { double pts = 0.0; std::vector<uint8_t> rgba; };

    void run();                       // body of the decoder thread

    AVFormatContext* fmt_   = nullptr;
    AVCodecContext*  codec_ = nullptr;
    int    vstream_   = -1;
    int    width_     = 0, height_ = 0;
    double fps_       = 30.0;
    double startTime_ = 0.0;

    std::thread             thread_;
    std::mutex              mtx_;
    std::condition_variable cv_;
    std::deque<Frame>       queue_;
    static constexpr size_t kMaxQueued = 8;

    std::atomic<bool> stop_{false};
    std::atomic<bool> loop_{true};
    std::atomic<bool> finished_{false};
};
