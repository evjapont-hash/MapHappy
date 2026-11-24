#pragma once
#include "Homography.h"
#include "Mesh.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// A surface's shape is a mask inside its quad. The quad is what you warp;
// the shape (circle, triangle, freeform polygon) is cut out of it.
//   Line     - the quad is built from two end points and a width (lineA, lineB, lineWidth)
//   Freeform - a polygon of any number of points (poly), in the quad's own 0..1 square
//   Object3D - a 3D model (built-in or an OBJ file) wearing the media as its texture, rendered, then mapped by the quad
enum class Shape { Quad = 0, Circle = 1, Triangle = 2, Line = 3, Freeform = 4, Object3D = 5 };
constexpr int kShapeCount = 6;

inline const char* shapeName(Shape s) {
    switch (s) {
        case Shape::Circle:   return "Circle";
        case Shape::Triangle: return "Triangle";
        case Shape::Line:     return "Line";
        case Shape::Freeform: return "Freeform";
        case Shape::Object3D: return "3D Object";
        default:              return "Quad";
    }
}
inline const char* shapeTag(Shape s) {
    switch (s) {
        case Shape::Circle:   return "O";
        case Shape::Triangle: return "^";
        case Shape::Line:     return "/";
        case Shape::Freeform: return "*";
        case Shape::Object3D: return "3D";
        default:              return "#";
    }
}
inline const char* shapeKey(Shape s) {
    switch (s) {
        case Shape::Circle:   return "circle";
        case Shape::Triangle: return "triangle";
        case Shape::Line:     return "line";
        case Shape::Freeform: return "freeform";
        case Shape::Object3D: return "object3d";
        default:              return "quad";
    }
}
inline Shape shapeFromKey(const std::string& k) {
    for (int i = 0; i < kShapeCount; i++) if (k == shapeKey((Shape)i)) return (Shape)i;
    return Shape::Quad;
}

constexpr int kMaxPolyPoints = 64;
// Freeform points live inside their quad (0..1). The picture is cut at the quad's edges, so a point outside would
// show nothing there; to reach further, warp the quad's corners.
inline Vec2 clampUnit(Vec2 v) { return Vec2{std::max(0.0f, std::min(1.0f, v.x)), std::max(0.0f, std::min(1.0f, v.y))}; }          // a freeform polygon's points (the shader has room for this many)
inline std::vector<Vec2> defaultPolygon() {  // a hexagon filling the quad
    std::vector<Vec2> p;
    for (int i = 0; i < 6; i++) { const float a = 6.2831853f * (i + 0.5f) / 6.0f; p.push_back(Vec2{0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)}); }
    return p;
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
    int         folderId = 0;         // the folder it is in (0 = top level); see Project::folders
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

    // ---- Line: its end points on the stage and its width (a fraction of the stage height)
    Vec2  lineA{0.30f, 0.50f}, lineB{0.70f, 0.50f};
    float lineWidth = 0.02f;
    // ---- Freeform: the polygon's points, in the quad's 0..1 square
    std::vector<Vec2> poly = defaultPolygon();
    // ---- 3D object: which model, and how it is turned and viewed
    MeshKind    meshKind = MeshKind::Cube;
    std::string meshPath;                                     // MeshKind::File only
    float rotX = -20.0f, rotY = 30.0f, rotZ = 0.0f;           // degrees: turned around Y, then X, then Z
    float objScale = 1.0f;
    float fov = 40.0f;                                        // the virtual camera's field of view, degrees
    float shading = 0.0f;                                     // 0 = flat (the projector is the light), 1 = lit from the front
};

// A line's quad: the segment from lineA to lineB, lineWidth thick, square in PIXELS (aspect = stage w / h).
inline bool lineQuad(const Surface& s, float aspect, Vec2 q[4]) {
    const float dx = (s.lineB.x - s.lineA.x) * aspect, dy = s.lineB.y - s.lineA.y;
    const float len = std::hypot(dx, dy);
    if (len < 1e-6f) return false;
    const float hw = std::max(0.0005f, s.lineWidth) * 0.5f;
    const float nx = -dy / len * hw / aspect, ny = dx / len * hw;     // the normal, back in stage units
    q[0] = {s.lineA.x - nx, s.lineA.y - ny}; q[1] = {s.lineB.x - nx, s.lineB.y - ny};
    q[2] = {s.lineB.x + nx, s.lineB.y + ny}; q[3] = {s.lineA.x + nx, s.lineA.y + ny};
    return true;
}
// Keeps a line's mapped quad in step with its end points (other shapes are left alone).
inline void syncLineQuad(Surface& s, float aspect) {
    if (s.shape != Shape::Line) return;
    Vec2 q[4];
    if (lineQuad(s, aspect, q)) for (int i = 0; i < 4; i++) s.out[i] = q[i];
}

// Maps a point through a 3x3 homography, including the perspective divide.
inline Vec2 mapPoint(const Mat3& M, Vec2 p) {
    const float* m = M.m;
    float x = m[0] * p.x + m[1] * p.y + m[2];
    float y = m[3] * p.x + m[4] * p.y + m[5];
    float w = m[6] * p.x + m[7] * p.y + m[8];
    return Vec2{x / w, y / w};
}

// The shape's outline in unit-square coordinates.
inline std::vector<Vec2> shapeOutlineUnit(Shape s, const std::vector<Vec2>* poly = nullptr) {
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
        case Shape::Freeform:
            pts = poly ? *poly : defaultPolygon();
            break;
        default:
            pts = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    }
    return pts;
}

// The shape's outline after being warped into the given quad.
inline std::vector<Vec2> outlineInQuad(const Surface& s, const Vec2 quad[4]) {
    std::vector<Vec2> pts;
    Mat3 H;
    if (!squareToQuad(quad, H)) return pts;
    for (Vec2 u : shapeOutlineUnit(s.shape, &s.poly)) pts.push_back(mapPoint(H, u));
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
    Vec2 base[4] = {s.out[0], s.out[1], s.out[2], s.out[3]};
    if (s.shape == Shape::Line) lineQuad(s, aspect, base);           // a line's quad always follows its end points and width
    const Vec2 c = quadCenter(base);
    const float sx = s.scale * s.scaleX, sy = s.scale * s.scaleY;
    for (int i = 0; i < 4; i++) {
        Vec2 v{(base[i].x - c.x) * sx, (base[i].y - c.y) * sy};      // scale first, in the quad's own directions...
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
