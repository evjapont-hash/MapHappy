#pragma once
// 3D models for "3D object" surfaces: the built-in shapes, a small OBJ reader, and the 4x4 matrix maths to view
// them. No OpenGL in here (the renderer uploads MeshData), so all of it is unit-tested on every platform.
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

// One vertex: position, normal, and texture position (u, v) with (0, 0) = the picture's top-left.
struct MeshVertex { float px, py, pz, nx, ny, nz, u, v; };

struct MeshData {
    std::vector<MeshVertex> vertices;          // three per triangle (no index buffer: simple, and fine at these sizes)
    size_t triangles() const { return vertices.size() / 3; }
    // Centres the model and scales it so it fits in a ball of radius 1, so any model fills the view the same way.
    void normalize();
    float radius() const;                       // the furthest vertex from the origin
};

enum class MeshKind { Cube = 0, Sphere, Cylinder, Plane, File };
constexpr int kMeshKindCount = 5;
inline const char* meshKindKey(MeshKind k)  { static const char* n[] = {"cube", "sphere", "cylinder", "plane", "file"}; return n[(int)k]; }
inline const char* meshKindName(MeshKind k) { static const char* n[] = {"Cube", "Sphere", "Cylinder", "Plane", "Model file (OBJ)"}; return n[(int)k]; }
inline MeshKind meshKindFromKey(const std::string& s) { for (int i = 0; i < kMeshKindCount; i++) if (s == meshKindKey((MeshKind)i)) return (MeshKind)i; return MeshKind::Cube; }

// The built-in shapes, already normalized. Each cube face shows the whole picture.
MeshData makeCube();
MeshData makeSphere(int segments = 48, int rings = 24);
MeshData makeCylinder(int segments = 48);
MeshData makePlane();

// Reads Wavefront OBJ text (v, vt, vn, f; polygons are split into triangles; negative indices work). Missing
// normals are computed per face; missing texture positions are projected from the front. Returns false (with
// `error`) if there are no usable faces. The result is normalized.
bool parseObj(const std::string& text, MeshData& out, std::string& error);
bool loadObjFile(const std::string& path, MeshData& out, std::string& error);

// The model a 3D surface shows. Built-in shapes and files are loaded once and shared; a file that can't be read
// gives the cube (with the reason in `error`). The pointer stays valid for the life of the program.
const MeshData* meshFor(MeshKind kind, const std::string& path, std::string* error = nullptr);
void forgetMeshFile(const std::string& path);   // so a changed file is read again

// ---- 4x4 matrices, column-major (as OpenGL wants them)
struct Mat4 {
    std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float& at(int row, int col) { return m[col * 4 + row]; }
    float  at(int row, int col) const { return m[col * 4 + row]; }
};
Mat4 operator*(const Mat4& a, const Mat4& b);
Mat4 perspective(float fovYDegrees, float aspect, float zNear, float zFar);
Mat4 translation(float x, float y, float z);
Mat4 rotationX(float degrees);
Mat4 rotationY(float degrees);
Mat4 rotationZ(float degrees);
Mat4 scaling(float s);
// x, y in -1..1 (y up) and depth, after the perspective divide. w <= 0 means behind the camera.
std::array<float, 4> transformPoint(const Mat4& m, float x, float y, float z);

// How a 3D surface is viewed: the model turned (Y, then X, then Z), scaled, and pushed back so that a model of
// radius 1 just fills the view at scale 1 whatever the field of view.
Mat4 objectModel(float rotX, float rotY, float rotZ, float scale);
Mat4 objectViewProjection(float fovDegrees, float aspect);
