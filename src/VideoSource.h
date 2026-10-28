#pragma once
#include "ContentSource.h"
#include "VideoDecoder.h"
#include <algorithm>
#include <string>
#include <vector>

// Plays a video file. VideoDecoder does the FFmpeg work on a background thread;
// this class owns the playback clock and uploads the current frame to a texture.
class VideoSource : public ContentSource {
public:
    ~VideoSource() override { destroyTexture(); }

    bool open(const std::string& path) {
        path_ = path;
        if (!decoder_.open(path, error_)) return false;
        error_.clear();
        clock_ = 0.0;
        destroyTexture();
        createTexture(decoder_.width(), decoder_.height());
        return true;
    }

    void update(double dt) override {
        if (!decoder_.isOpen()) return;
        clock_ += std::min(dt, 0.1);          // ignore huge hiccups (e.g. dragging the window)
        if (decoder_.getFrameFor(clock_, frame_)) {
            glBindTexture(GL_TEXTURE_2D, tex_);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, decoder_.width(), decoder_.height(),
                            GL_RGBA, GL_UNSIGNED_BYTE, frame_.data());
        }
    }
    GLuint texture() const override { return tex_; }

    bool isOpen() const { return decoder_.isOpen(); }
    void setLoop(bool loop) { decoder_.setLoop(loop); }
    double time() const { return clock_; }
    int width() const override { return decoder_.width(); }
    int height() const override { return decoder_.height(); }
    double fps() const { return decoder_.fps(); }
    const std::string& path() const { return path_; }
    const std::string& error() const { return error_; }

private:
    void createTexture(int w, int h) {
        std::vector<unsigned char> black(size_t(w) * h * 4, 0);
        glGenTextures(1, &tex_);
        glBindTexture(GL_TEXTURE_2D, tex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, black.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    void destroyTexture() {
        if (tex_) { glDeleteTextures(1, &tex_); tex_ = 0; }
    }

    VideoDecoder decoder_;
    GLuint tex_ = 0;
    double clock_ = 0.0;
    std::vector<uint8_t> frame_;
    std::string path_, error_;
};
