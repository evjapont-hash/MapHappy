#pragma once
// Helpers for the tests that need an OpenGL context (run under a virtual display in CI).
#include "TestSupport.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

// A GLFW window with an OpenGL 3.3 core context, made current. `ok` is false if the machine can't provide one
// (then a test should call skipTest() instead of failing).
struct GlContext {
    GLFWwindow* window = nullptr;
    bool ok = false;
    explicit GlContext(int w = 64, int h = 64, int x = 0, int y = 0) {
        if (!glfwInit()) return;
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
        window = glfwCreateWindow(w, h, "MapHappy test", nullptr, nullptr);
        if (!window) return;
        glfwSetWindowPos(window, x, y);
        glfwMakeContextCurrent(window);
        ok = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress) != 0;
    }
    ~GlContext() { if (window) glfwDestroyWindow(window); glfwTerminate(); }
    GlContext(const GlContext&) = delete; GlContext& operator=(const GlContext&) = delete;
};

// Saves the current framebuffer as a PPM image into $MAPHAPPY_TEST_SHOTS (if that folder is set). Handy when a UI test fails.
inline void saveShot(const char* name, int w, int h) {
    const std::string dir = getTestEnv("MAPHAPPY_TEST_SHOTS");
    if (dir.empty()) return;
    std::vector<unsigned char> px((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    std::ofstream f(std::filesystem::path(dir) / (std::string(name) + ".ppm"), std::ios::binary);
    f << "P6\n" << w << " " << h << "\n255\n";
    for (int y = h - 1; y >= 0; y--) f.write((const char*)&px[(size_t)y * w * 3], (std::streamsize)w * 3);
}

struct Px { int r = 0, g = 0, b = 0; };
inline bool isColor(Px p, int r, int g, int b, int tol = 4) { return std::abs(p.r - r) <= tol && std::abs(p.g - g) <= tol && std::abs(p.b - b) <= tol; }
