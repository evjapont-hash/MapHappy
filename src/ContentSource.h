#pragma once
#include <glad/glad.h>

// Anything that can produce a picture for the warper: a test pattern, a video,
// later a webcam, Syphon/Spout, and so on. The warp code only needs a texture.
//
// Texture convention: row 0 of the texture is the BOTTOM of the image, the same as
// anything rendered into a framebuffer. Sources that load top-down data must flip it.
class ContentSource {
public:
    virtual ~ContentSource() = default;
    virtual void   update(double dt) = 0;       // advance by dt seconds and refresh the texture
    virtual GLuint texture() const = 0;
    virtual int    width()   const = 0;
    virtual int    height()  const = 0;
};
