#pragma once
#include "ContentSource.h"
#include <vector>

// A flat-colour picture. Stands in for media whose file can't be found, so the project still
// opens and the surfaces that used it keep their place until the file is relinked.
class SolidSource : public ContentSource {
public:
    SolidSource(unsigned char r, unsigned char g, unsigned char b, int w = 64, int h = 36) : w_(w), h_(h) {
        std::vector<unsigned char> px((size_t)w * h * 4);
        for (size_t i = 0; i < px.size(); i += 4) { px[i] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = 255; }
        glGenTextures(1, &tex_);
        glBindTexture(GL_TEXTURE_2D, tex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    ~SolidSource() override { if (tex_) glDeleteTextures(1, &tex_); }
    void update(double) override {}
    GLuint texture() const override { return tex_; }
    int width() const override { return w_; }
    int height() const override { return h_; }
private:
    GLuint tex_ = 0;
    int w_, h_;
};
