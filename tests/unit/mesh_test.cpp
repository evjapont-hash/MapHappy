// 3D objects without OpenGL: the built-in models, the OBJ reader, the model cache and the view maths.
#include "TestSupport.h"
#include "Mesh.h"
#include <algorithm>

static bool normalsAreUnit(const MeshData& m) {
    for (const MeshVertex& v : m.vertices) if (!nearf(std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz), 1.0, 1e-3)) return false;
    return true;
}
static bool uvInRange(const MeshData& m) {
    for (const MeshVertex& v : m.vertices) if (v.u < -1e-5f || v.u > 1.00001f || v.v < -1e-5f || v.v > 1.00001f) return false;
    return true;
}

int main() {
    // ---- built-in models
    const MeshData cube = makeCube(), sphere = makeSphere(), cyl = makeCylinder(), plane = makePlane();
    expect("the cube has 12 triangles, the plane 2", cube.triangles() == 12 && plane.triangles() == 2);
    expect("sphere and cylinder are made of many triangles", sphere.triangles() > 1000 && cyl.triangles() == 48 * 4);
    expect("every built-in model fits a ball of radius 1 exactly", nearf(cube.radius(), 1, 1e-5) && nearf(sphere.radius(), 1, 1e-5) && nearf(cyl.radius(), 1, 1e-5) && nearf(plane.radius(), 1, 1e-5));
    expect("...with unit normals and texture positions inside the picture", normalsAreUnit(cube) && normalsAreUnit(sphere) && normalsAreUnit(cyl) && uvInRange(cube) && uvInRange(sphere) && uvInRange(cyl));
    { bool frontOk = false;                    // the front face's top-left corner shows the picture's top-left
      for (const MeshVertex& v : cube.vertices) if (v.nz > 0.99f && v.px < 0 && v.py > 0) frontOk = nearf(v.u, 0, 1e-6) && nearf(v.v, 0, 1e-6);
      expect("each cube face shows the picture the right way up and round", frontOk); }
    { bool facing = false;                     // the middle of the picture faces the camera, the seam is at the back
      for (const MeshVertex& v : sphere.vertices) if (nearf(v.u, 0.5, 1e-6) && nearf(v.v, 0.5, 1e-6)) facing = v.pz > 0.99f;
      expect("the sphere's picture is centred on its front", facing); }

    // ---- OBJ
    { MeshData m; std::string err;
      const std::string quad = "# a square\nv 0 0 0\nv 2 0 0\nv 2 2 0\nv 0 2 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1 4/4/1\n";
      expect("an OBJ square loads as two triangles", parseObj(quad, m, err) && m.triangles() == 2 && err.empty());
      expect("...centred and scaled to radius 1", nearf(m.radius(), 1, 1e-5) && nearf(m.vertices[0].px, -std::sqrt(0.5), 1e-5));
      expect("...with its texture positions flipped to top-down", nearf(m.vertices[0].v, 1.0, 1e-6) && nearf(m.vertices[2].v, 0.0, 1e-6));
      expect("...and its normals", nearf(m.vertices[1].nz, 1.0, 1e-6)); }
    { MeshData m; std::string err;
      expect("negative indices and v//vn faces work", parseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\nf -3//-1 -2//-1 -1//-1\n", m, err) && m.triangles() == 1 && nearf(m.vertices[0].nz, 1, 1e-6)); }
    { MeshData m; std::string err;
      expect("a face without normals gets its own", parseObj("v 0 0 0\r\nv 1 0 0\r\nv 0 1 0\r\nf 1 2 3\r\n", m, err) && nearf(m.vertices[0].nz, 1, 1e-5));
      expect("...and without texture positions the picture is projected from the front", m.vertices[1].u > m.vertices[0].u + 0.5f && m.vertices[2].v < m.vertices[0].v - 0.5f && uvInRange(m)); }
    { MeshData m; std::string err;
      expect("a file with no faces is refused with a reason", !parseObj("v 0 0 0\nv 1 0 0\n", m, err) && !err.empty());
      err.clear();
      expect("faces pointing at missing points are refused", !parseObj("v 0 0 0\nf 1 2 3\n", m, err) && !err.empty());
      expect("rubbish is refused", !parseObj("this is not a model", m, err)); }
    { TempDir tmp("mesh"); std::string err; MeshData m;
      const std::string path = tmp.file("tri.obj");
      { std::ofstream f(std::filesystem::u8path(path)); f << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"; }
      expect("an OBJ file loads from disk", loadObjFile(path, m, err) && m.triangles() == 1);
      const MeshData* a = meshFor(MeshKind::File, path, &err);
      expect("models are cached: asking twice gives the same one", a && a == meshFor(MeshKind::File, path) && a->triangles() == 1 && err.empty());
      { std::ofstream f(std::filesystem::u8path(path)); f << "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\nf 1 2 3\nf 2 4 3\n"; }
      forgetMeshFile(path);
      expect("after forgetting, a changed file is read again", meshFor(MeshKind::File, path)->triangles() == 2);
      const MeshData* missing = meshFor(MeshKind::File, tmp.file("nope.obj"), &err);
      expect("a missing model file shows a cube, and says why", missing && missing->triangles() == 12 && !err.empty());
      expect("built-in models are shared too", meshFor(MeshKind::Sphere, "") == meshFor(MeshKind::Sphere, "x")); }
    { bool all = true; for (int i = 0; i < kMeshKindCount; i++) all &= meshKindFromKey(meshKindKey((MeshKind)i)) == (MeshKind)i;
      expect("model kinds round-trip through their file names", all && meshKindFromKey("teapot") == MeshKind::Cube); }

    // ---- view maths
    { auto p = transformPoint(rotationY(90) , 1, 0, 0);
      expect("turning 90 degrees around Y takes +X to -Z", nearf(p[0], 0, 1e-6) && nearf(p[2], -1, 1e-6));
      p = transformPoint(rotationX(90), 0, 1, 0);
      expect("turning 90 degrees around X takes +Y to +Z", nearf(p[2], 1, 1e-6) && nearf(p[1], 0, 1e-6));
      p = transformPoint(translation(1, 2, 3) * scaling(2), 1, 1, 1);
      expect("matrices combine right to left (scale, then move)", nearf(p[0], 3, 1e-6) && nearf(p[1], 4, 1e-6) && nearf(p[2], 5, 1e-6)); }
    for (float fov : {20.0f, 40.0f, 90.0f}) {
        const Mat4 vp = objectViewProjection(fov, 1.0f);
        float top = 0; bool inside = true;
        for (const MeshVertex& v : sphere.vertices) { auto p = transformPoint(vp, v.px, v.py, v.pz); top = std::max(top, p[1]); inside &= p[3] > 0 && std::fabs(p[0]) <= 1.001f && p[2] > -1 && p[2] < 1; }
        expect((std::string("a radius-1 model just fills the view, at a field of view of ") + std::to_string((int)fov)).c_str(), inside && top > 0.97f);
    }
    { const Mat4 wide = objectViewProjection(40, 2.0f), tall = objectViewProjection(40, 0.5f);
      float wx = 0, ty = 0;
      for (const MeshVertex& v : sphere.vertices) { wx = std::max(wx, transformPoint(wide, v.px, v.py, v.pz)[0]); ty = std::max(ty, transformPoint(tall, v.px, v.py, v.pz)[1]); }
      expect("in a wide view the model touches the top and bottom, in a tall one the sides", wx < 0.6f && ty < 0.6f); }
    { auto p = transformPoint(objectViewProjection(40, 1) * objectModel(0, 0, 0, 0.5f), 0, 1, 0);
      expect("size 50% halves the model on screen", p[1] > 0.4f && p[1] < 0.6f); }
    return finishTest();
}
