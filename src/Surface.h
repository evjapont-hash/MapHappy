#pragma once
#include "Homography.h"
#include <algorithm>
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

// How a surface combines with what is already on the stage.
enum class Blend { Over = 0, Add = 1, Multiply = 2, Screen = 3 };
inline const char* blendKey(Blend b)  { return b == Blend::Add ? "add" : b == Blend::Multiply ? "multiply" : b == Blend::Screen ? "screen" : "over"; }
inline const char* blendName(Blend b) { return b == Blend::Add ? "Add" : b == Blend::Multiply ? "Multiply" : b == Blend::Screen ? "Screen" : "Over"; }
inline Blend blendFromKey(const std::string& k) { return k == "add" ? Blend::Add : k == "multiply" ? Blend::Multiply : k == "screen" ? Blend::Screen : Blend::Over; }

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
    Blend       blend = Blend::Over;

    // ---- animatable look (the timeline can drive every one of these; see Params.h)
    float feather = 0.0f;                                     // soft edge, 0 = crisp
    // Transform of the OUTPUT quad, applied on top of the mapped corners about the quad's centre:
    float posX = 0.0f, posY = 0.0f;                           // move, as a fraction of the stage's width / height
    float rotation = 0.0f;                                    // degrees, clockwise
    float scale = 1.0f, scaleX = 1.0f, scaleY = 1.0f;         // multipliers
    // Transform of the PICTURE inside the surface (scroll, zoom, spin):
    float contentX = 0.0f, contentY = 0.0f;                   // move the picture, as a fraction of the picture's width / height
    float contentScale = 1.0f;                                // zoom (2 = twice as big)
    float contentRotation = 0.0f;                             // degrees, clockwise
    // Colour:
    float tintR = 1.0f, tintG = 1.0f, tintB = 1.0f;
    float hue = 0.0f;                                         // degrees around the colour wheel
    float saturation = 1.0f, contrast = 1.0f, brightness = 0.0f;
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

// ---------------------------------------------------------------------------------------------
// The quads that are actually drawn: the mapped quads moved / spun / scaled by the animatable
// transform. The mapped corners (what you drag) are never changed by animation, so the alignment
// you set up on the real object stays put.
//
// Rotation is done in PIXEL proportions (x multiplied by the picture's aspect ratio), otherwise a
// rotated square would shear on a 16:9 stage.
// ---------------------------------------------------------------------------------------------
inline Vec2 rotateAspect(Vec2 v, float degrees, float aspect) {
    const float a = degrees * 3.14159265358979f / 180.0f, c = std::cos(a), sn = std::sin(a);
    const float px = v.x * aspect, py = v.y;
    return Vec2{(px * c - py * sn) / aspect, px * sn + py * c};
}

// aspect = stage width / stage height
inline void effectiveOut(const Surface& s, float aspect, Vec2 out[4]) {
    const Vec2 c = quadCenter(s.out);
    const float sx = s.scale * s.scaleX, sy = s.scale * s.scaleY;
    for (int i = 0; i < 4; i++) {
        Vec2 v{(s.out[i].x - c.x) * sx, (s.out[i].y - c.y) * sy};      // scale first, in the quad's own directions...
        v = rotateAspect(v, s.rotation, aspect);                      // ...then spin...
        out[i] = Vec2{c.x + v.x + s.posX, c.y + v.y + s.posY};        // ...then move
    }
}

// aspect = the media's width / height. The picture is moved, zoomed and spun, which means the window
// that looks at it moves the opposite way: the inverse of that transform is applied to the input quad.
inline void effectiveIn(const Surface& s, float aspect, Vec2 in[4]) {
    const Vec2 c = quadCenter(s.in);
    const float inv = 1.0f / std::max(0.01f, s.contentScale);
    for (int i = 0; i < 4; i++) {
        Vec2 v{(s.in[i].x - c.x - s.contentX) * inv, (s.in[i].y - c.y - s.contentY) * inv};
        v = rotateAspect(v, -s.contentRotation, aspect);
        in[i] = Vec2{c.x + v.x, c.y + v.y};
    }
}

// True when the animatable transform actually changes the quad (so the editor can show both outlines).
inline bool outTransformIsIdentity(const Surface& s) {
    return s.posX == 0.0f && s.posY == 0.0f && s.rotation == 0.0f && s.scale == 1.0f && s.scaleX == 1.0f && s.scaleY == 1.0f;
}
inline bool inTransformIsIdentity(const Surface& s) {
    return s.contentX == 0.0f && s.contentY == 0.0f && s.contentRotation == 0.0f && s.contentScale == 1.0f;
}
