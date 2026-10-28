#pragma once
#include "Homography.h"
#include <cmath>
#include <string>
#include <vector>

// A surface's shape is a mask inside its quad. The quad is what you warp;
// the shape (circle, triangle) is cut out of it.
enum class Shape { Quad = 0, Circle = 1, Triangle = 2 };

inline const char* shapeName(Shape s) {
    switch (s) {
        case Shape::Circle:   return "Circle";
        case Shape::Triangle: return "Triangle";
        default:              return "Quad";
    }
}
inline const char* shapeTag(Shape s) {
    switch (s) {
        case Shape::Circle:   return "O";
        case Shape::Triangle: return "^";
        default:              return "#";
    }
}

// A surface has TWO quads:
//   in  - which region of the media it shows (normalized media coords, (0,0) = top-left)
//   out - where it lands on the stage      (normalized stage coords, (0,0) = top-left)
struct Surface {
    int         id = 0;               // stable for the life of the project; timelines will refer to this
    std::string name;
    Shape       shape   = Shape::Quad;
    bool        visible = true;
    int         mediaId = 1;
    Vec2        in[4]  = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    Vec2        out[4] = {{0.36f, 0.25f}, {0.64f, 0.25f}, {0.64f, 0.75f}, {0.36f, 0.75f}};
    float       opacity = 1.0f;       // 0..1
    bool        flipH = false, flipV = false;
};

// Maps a point through a 3x3 homography, including the perspective divide.
inline Vec2 mapPoint(const Mat3& M, Vec2 p) {
    const float* m = M.m;
    float x = m[0] * p.x + m[1] * p.y + m[2];
    float y = m[3] * p.x + m[4] * p.y + m[5];
    float w = m[6] * p.x + m[7] * p.y + m[8];
    return Vec2{x / w, y / w};
}

// The shape's outline in unit-square coordinates.
inline std::vector<Vec2> shapeOutlineUnit(Shape s) {
    std::vector<Vec2> pts;
    switch (s) {
        case Shape::Circle:
            for (int i = 0; i < 72; i++) {
                float a = 6.2831853f * i / 72.0f;
                pts.push_back(Vec2{0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)});
            }
            break;
        case Shape::Triangle:
            pts = {{0.5f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
            break;
        default:
            pts = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    }
    return pts;
}

// The shape's outline after being warped into the given quad.
inline std::vector<Vec2> outlineInQuad(Shape s, const Vec2 quad[4]) {
    std::vector<Vec2> pts;
    Mat3 H;
    if (!squareToQuad(quad, H)) return pts;
    for (Vec2 u : shapeOutlineUnit(s)) pts.push_back(mapPoint(H, u));
    return pts;
}

inline bool pointInPolygon(const std::vector<Vec2>& poly, Vec2 p) {
    if (poly.size() < 3) return false;
    bool inside = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        if (((poly[i].y > p.y) != (poly[j].y > p.y)) &&
            (p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x))
            inside = !inside;
    }
    return inside;
}

// Where the quad's diagonals cross. This is where the "move" handle sits.
inline Vec2 quadCenter(const Vec2 q[4]) {
    Vec2 d1{q[2].x - q[0].x, q[2].y - q[0].y};
    Vec2 d2{q[3].x - q[1].x, q[3].y - q[1].y};
    float denom = d1.x * d2.y - d1.y * d2.x;
    if (std::fabs(denom) < 1e-9f)
        return Vec2{(q[0].x + q[1].x + q[2].x + q[3].x) * 0.25f, (q[0].y + q[1].y + q[2].y + q[3].y) * 0.25f};
    float t = ((q[1].x - q[0].x) * d2.y - (q[1].y - q[0].y) * d2.x) / denom;
    return Vec2{q[0].x + t * d1.x, q[0].y + t * d1.y};
}
