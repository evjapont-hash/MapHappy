#pragma once
#include <cmath>

struct Vec2 { float x, y; };

// 3x3 matrix stored row-major: m[0..2] is the first row.
struct Mat3 { float m[9] = {1,0,0, 0,1,0, 0,0,1}; };

// Builds the perspective transform that maps the unit square
// (0,0) (1,0) (1,1) (0,1)  ->  the quad p[0] p[1] p[2] p[3].
// Corner order: top-left, top-right, bottom-right, bottom-left.
// (Closed-form solution from Paul Heckbert's "Fundamentals of Texture Mapping".)
inline bool squareToQuad(const Vec2 p[4], Mat3& M) {
    float dx1 = p[1].x - p[2].x, dx2 = p[3].x - p[2].x;
    float dy1 = p[1].y - p[2].y, dy2 = p[3].y - p[2].y;
    float dx3 = p[0].x - p[1].x + p[2].x - p[3].x;
    float dy3 = p[0].y - p[1].y + p[2].y - p[3].y;

    float a, b, c, d, e, f, g, h;
    if (std::fabs(dx3) < 1e-9f && std::fabs(dy3) < 1e-9f) {
        // Quad is a parallelogram: the transform is affine, no perspective needed.
        a = p[1].x - p[0].x;  b = p[3].x - p[0].x;  c = p[0].x;
        d = p[1].y - p[0].y;  e = p[3].y - p[0].y;  f = p[0].y;
        g = 0.0f;             h = 0.0f;
    } else {
        float den = dx1 * dy2 - dx2 * dy1;
        if (std::fabs(den) < 1e-9f) return false;
        g = (dx3 * dy2 - dx2 * dy3) / den;
        h = (dx1 * dy3 - dx3 * dy1) / den;
        a = p[1].x - p[0].x + g * p[1].x;
        b = p[3].x - p[0].x + h * p[3].x;
        c = p[0].x;
        d = p[1].y - p[0].y + g * p[1].y;
        e = p[3].y - p[0].y + h * p[3].y;
        f = p[0].y;
    }
    float v[9] = {a, b, c,  d, e, f,  g, h, 1.0f};
    for (int i = 0; i < 9; i++) M.m[i] = v[i];
    return true;
}

// Inverse via the adjugate. Returns false if the matrix is singular.
inline bool invert(const Mat3& A, Mat3& out) {
    const float* m = A.m;
    float c00 =  m[4]*m[8] - m[5]*m[7];
    float c01 = -(m[3]*m[8] - m[5]*m[6]);
    float c02 =  m[3]*m[7] - m[4]*m[6];
    float det = m[0]*c00 + m[1]*c01 + m[2]*c02;
    if (std::fabs(det) < 1e-9f) return false;
    float inv = 1.0f / det;
    out.m[0] = c00 * inv;
    out.m[1] = -(m[1]*m[8] - m[2]*m[7]) * inv;
    out.m[2] =  (m[1]*m[5] - m[2]*m[4]) * inv;
    out.m[3] = c01 * inv;
    out.m[4] =  (m[0]*m[8] - m[2]*m[6]) * inv;
    out.m[5] = -(m[0]*m[5] - m[2]*m[3]) * inv;
    out.m[6] = c02 * inv;
    out.m[7] = -(m[0]*m[7] - m[1]*m[6]) * inv;
    out.m[8] =  (m[0]*m[4] - m[1]*m[3]) * inv;
    return true;
}

// A concave or self-intersecting quad has no valid projection, so we reject it.
inline bool isConvex(const Vec2 p[4]) {
    int sign = 0;
    for (int i = 0; i < 4; i++) {
        Vec2 a = p[i], b = p[(i + 1) % 4], c = p[(i + 2) % 4];
        float cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
        if (std::fabs(cross) < 1e-9f) return false;
        int s = cross > 0 ? 1 : -1;
        if (sign == 0) sign = s;
        else if (s != sign) return false;
    }
    return true;
}