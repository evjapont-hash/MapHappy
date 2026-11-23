// The surface transform math: position, scale and rotation (aspect-correct) and the content transform.
#include "TestSupport.h"
#include "Surface.h"

static float pixelDist(Vec2 a, Vec2 b, float aspect) { return std::hypot((a.x - b.x) * aspect, a.y - b.y); }

int main() {
    const float A = 16.0f / 9.0f;
    Surface s; Vec2 e[4];
    effectiveOut(s, A, e);
    bool same = true; for (int i = 0; i < 4; i++) if (!nearf(e[i].x, s.out[i].x, 1e-5) || !nearf(e[i].y, s.out[i].y, 1e-5)) same = false;
    expect("the identity transform leaves the quad exactly as mapped", same && outTransformIsIdentity(s));

    s.posX = 0.1f; s.posY = -0.05f; effectiveOut(s, A, e);
    expect("position moves every corner by the same amount", nearf(e[0].x, s.out[0].x + 0.1f, 1e-5) && nearf(e[2].y, s.out[2].y - 0.05f, 1e-5) && !outTransformIsIdentity(s));
    expect("the mapped corners are never changed by animation", nearf(s.out[0].x, 0.36f, 1e-6) && nearf(s.out[0].y, 0.25f, 1e-6));

    s = Surface(); s.scale = 2.0f; effectiveOut(s, A, e);
    expect("scale 2 doubles the quad about its centre", nearf(e[0].x, 0.5f + (0.36f - 0.5f) * 2, 1e-5) && nearf(e[2].y, 0.5f + (0.75f - 0.5f) * 2, 1e-5));
    expect("...and the centre stays put", nearf(quadCenter(e).x, 0.5f, 1e-5) && nearf(quadCenter(e).y, 0.5f, 1e-5));
    s = Surface(); s.scaleX = 0.5f; s.scaleY = 3.0f; effectiveOut(s, A, e);
    expect("scale X and Y are independent", nearf(e[1].x - e[0].x, (0.64f - 0.36f) * 0.5f, 1e-5) && nearf(e[3].y - e[0].y, (0.75f - 0.25f) * 3.0f, 1e-5));
    s = Surface(); s.scale = 0.0f; effectiveOut(s, A, e);
    Mat3 m, mi;
    expect("scale 0 collapses the quad, which can't be inverted (the renderer skips it)", nearf(e[0].x, 0.5f, 1e-6) && !(squareToQuad(e, m) && invert(m, mi)));

    // rotation: a square (in pixels) must stay a square at every angle on a 16:9 stage
    s = Surface();
    const float sideY = 0.5f, sideX = sideY / A;
    s.out[0] = {0.5f - sideX / 2, 0.25f}; s.out[1] = {0.5f + sideX / 2, 0.25f}; s.out[2] = {0.5f + sideX / 2, 0.75f}; s.out[3] = {0.5f - sideX / 2, 0.75f};
    bool square = true;
    for (float deg : {0.0f, 15.0f, 45.0f, 90.0f, 137.0f, 180.0f, 270.0f}) {
        s.rotation = deg; effectiveOut(s, A, e);
        const float d01 = pixelDist(e[0], e[1], A), d12 = pixelDist(e[1], e[2], A), d23 = pixelDist(e[2], e[3], A), d30 = pixelDist(e[3], e[0], A);
        if (!(nearf(d01, d12, 1e-4) && nearf(d12, d23, 1e-4) && nearf(d23, d30, 1e-4) && nearf(pixelDist(e[0], e[2], A), pixelDist(e[1], e[3], A), 1e-4) && nearf(d01, 0.5, 1e-4))) square = false;
    }
    expect("a pixel-square stays a perfect square at every rotation (aspect-correct)", square);
    s.rotation = 90.0f; effectiveOut(s, A, e);
    expect("90 degrees is clockwise: the top-left corner moves to the top-right", e[0].x > 0.5f && e[0].y < 0.5f);
    s.rotation = 360.0f; effectiveOut(s, A, e);
    expect("a full turn returns to the start", nearf(e[0].x, s.out[0].x, 1e-4) && nearf(e[0].y, s.out[0].y, 1e-4));

    // order of operations: scale, then rotate, then move
    s = Surface(); s.scaleX = 2.0f; s.rotation = 90.0f; s.posX = 0.1f; effectiveOut(s, A, e);
    expect("scale happens before rotation: the (scaled) top edge becomes vertical", nearf(e[1].x, e[0].x, 1e-4) && std::fabs(e[1].y - e[0].y) > 0.1f);
    expect("...with its scaled length", nearf(pixelDist(e[0], e[1], A), (0.64f - 0.36f) * 2.0f * A, 1e-3));
    expect("...and the move comes last", nearf(quadCenter(e).x, 0.6f, 1e-4) && nearf(quadCenter(e).y, 0.5f, 1e-4));

    // ---- content transform: the picture moves, so the window that looks at it moves the other way
    s = Surface(); Vec2 in[4];
    effectiveIn(s, A, in);
    same = true; for (int i = 0; i < 4; i++) if (!nearf(in[i].x, s.in[i].x, 1e-5) || !nearf(in[i].y, s.in[i].y, 1e-5)) same = false;
    expect("the identity content transform samples the whole input quad", same && inTransformIsIdentity(s));
    s.contentX = 0.1f; effectiveIn(s, A, in);
    expect("moving the picture right moves the sampling window left", nearf(in[0].x, -0.1f, 1e-5) && nearf(in[1].x, 0.9f, 1e-5) && nearf(in[0].y, 0.0f, 1e-5));
    s = Surface(); s.contentScale = 2.0f; effectiveIn(s, A, in);
    expect("zoom 2 samples the middle half of the picture", nearf(in[0].x, 0.25f, 1e-5) && nearf(in[2].x, 0.75f, 1e-5) && nearf(in[0].y, 0.25f, 1e-5));
    s = Surface(); s.contentScale = 0.5f; effectiveIn(s, A, in);
    expect("zoom 0.5 samples beyond the picture", nearf(in[0].x, -0.5f, 1e-5) && nearf(in[2].x, 1.5f, 1e-5));
    s = Surface(); s.contentScale = 0.0f; effectiveIn(s, A, in);
    expect("zoom 0 is clamped, with no division by zero", std::isfinite(in[0].x) && std::isfinite(in[2].y));
    s = Surface(); s.contentRotation = 90.0f; effectiveIn(s, 1.0f, in);
    expect("rotating the picture clockwise turns the sampling window anticlockwise", nearf(in[0].x, 0.0f, 1e-5) && nearf(in[0].y, 1.0f, 1e-5) && nearf(in[2].x, 1.0f, 1e-5) && nearf(in[2].y, 0.0f, 1e-5));
    s = Surface(); s.contentRotation = 360.0f; effectiveIn(s, A, in);
    expect("a full content turn returns to the start", nearf(in[0].x, 0.0f, 1e-4) && nearf(in[0].y, 0.0f, 1e-4));

    expect("blend modes round-trip through their names", blendFromKey(blendKey(Blend::Add)) == Blend::Add && blendFromKey(blendKey(Blend::Multiply)) == Blend::Multiply &&
           blendFromKey(blendKey(Blend::Screen)) == Blend::Screen && blendFromKey("nonsense") == Blend::Over);

    // ---- geometry helpers used for hit-testing
    std::vector<Vec2> tri = {{0, 0}, {1, 0}, {0, 1}};
    expect("point-in-polygon: inside, outside and degenerate", pointInPolygon(tri, {0.2f, 0.2f}) && !pointInPolygon(tri, {0.8f, 0.8f}) && !pointInPolygon({}, {0, 0}));
    return finishTest();
}
