#include "Mesh.h"
#include "PathUtil.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace {
constexpr float kPi = 3.14159265358979f;

void addTri(MeshData& m, const MeshVertex& a, const MeshVertex& b, const MeshVertex& c) { m.vertices.push_back(a); m.vertices.push_back(b); m.vertices.push_back(c); }
MeshVertex vtx(float x, float y, float z, float nx, float ny, float nz, float u, float v) { return MeshVertex{x, y, z, nx, ny, nz, u, v}; }

void faceNormal(const MeshVertex& a, const MeshVertex& b, const MeshVertex& c, float n[3]) {
    const float ux = b.px - a.px, uy = b.py - a.py, uz = b.pz - a.pz, vx = c.px - a.px, vy = c.py - a.py, vz = c.pz - a.pz;
    n[0] = uy * vz - uz * vy; n[1] = uz * vx - ux * vz; n[2] = ux * vy - uy * vx;
    const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (l > 1e-12f) { n[0] /= l; n[1] /= l; n[2] /= l; } else { n[0] = 0; n[1] = 0; n[2] = 1; }
}
}  // namespace

float MeshData::radius() const {
    float r = 0.0f;
    for (const MeshVertex& v : vertices) r = std::max(r, std::sqrt(v.px * v.px + v.py * v.py + v.pz * v.pz));
    return r;
}

void MeshData::normalize() {
    if (vertices.empty()) return;
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const MeshVertex& v : vertices) {
        const float p[3] = {v.px, v.py, v.pz};
        for (int i = 0; i < 3; i++) { lo[i] = std::min(lo[i], p[i]); hi[i] = std::max(hi[i], p[i]); }
    }
    const float c[3] = {(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
    for (MeshVertex& v : vertices) { v.px -= c[0]; v.py -= c[1]; v.pz -= c[2]; }
    const float r = radius();
    if (r > 1e-12f) for (MeshVertex& v : vertices) { v.px /= r; v.py /= r; v.pz /= r; }
}

// ---------------------------------------------------------------------------------------------- built-in shapes
MeshData makeCube() {
    MeshData m;
    // each face: its normal, and the directions of u (right) and v (down) on it, as seen from outside
    struct Face { float n[3], r[3], d[3]; };
    const Face faces[6] = {
        {{0, 0, 1},  {1, 0, 0},  {0, -1, 0}},     // front
        {{1, 0, 0},  {0, 0, -1}, {0, -1, 0}},     // right
        {{0, 0, -1}, {-1, 0, 0}, {0, -1, 0}},     // back
        {{-1, 0, 0}, {0, 0, 1},  {0, -1, 0}},     // left
        {{0, 1, 0},  {1, 0, 0},  {0, 0, 1}},      // top
        {{0, -1, 0}, {1, 0, 0},  {0, 0, -1}},     // bottom
    };
    for (const Face& f : faces) {
        auto corner = [&](float u, float v) {
            const float a = u * 2 - 1, b = v * 2 - 1;
            return vtx(f.n[0] + a * f.r[0] + b * f.d[0], f.n[1] + a * f.r[1] + b * f.d[1], f.n[2] + a * f.r[2] + b * f.d[2], f.n[0], f.n[1], f.n[2], u, v);
        };
        const MeshVertex tl = corner(0, 0), tr = corner(1, 0), br = corner(1, 1), bl = corner(0, 1);
        addTri(m, tl, bl, br); addTri(m, tl, br, tr);
    }
    m.normalize();
    return m;
}

MeshData makeSphere(int segments, int rings) {
    MeshData m;
    segments = std::max(3, segments); rings = std::max(2, rings);
    auto at = [&](int s, int r) {
        const float u = (float)s / segments, v = (float)r / rings;
        const float th = (u - 0.5f) * 2 * kPi, ph = v * kPi;   // around (the seam at the back), and from the top down
        const float x = std::sin(ph) * std::sin(th), y = std::cos(ph), z = std::sin(ph) * std::cos(th);
        return vtx(x, y, z, x, y, z, u, v);                    // u = 0.5 faces the camera
    };
    for (int r = 0; r < rings; r++)
        for (int s = 0; s < segments; s++) {
            const MeshVertex a = at(s, r), b = at(s + 1, r), c = at(s + 1, r + 1), d = at(s, r + 1);
            if (r > 0) addTri(m, a, d, c);
            if (r < rings - 1) addTri(m, a, c, b);
        }
    m.normalize();
    return m;
}

MeshData makeCylinder(int segments) {
    MeshData m;
    segments = std::max(3, segments);
    for (int s = 0; s < segments; s++) {
        const float u0 = (float)s / segments, u1 = (float)(s + 1) / segments;
        const float a0 = (u0 - 0.5f) * 2 * kPi, a1 = (u1 - 0.5f) * 2 * kPi;   // the seam at the back
        const float x0 = std::sin(a0), z0 = std::cos(a0), x1 = std::sin(a1), z1 = std::cos(a1);
        const MeshVertex t0 = vtx(x0, 1, z0, x0, 0, z0, u0, 0), t1 = vtx(x1, 1, z1, x1, 0, z1, u1, 0);
        const MeshVertex b0 = vtx(x0, -1, z0, x0, 0, z0, u0, 1), b1 = vtx(x1, -1, z1, x1, 0, z1, u1, 1);
        addTri(m, t0, b0, b1); addTri(m, t0, b1, t1);
        // caps: the picture as a disc
        addTri(m, vtx(0, 1, 0, 0, 1, 0, 0.5f, 0.5f), vtx(x1, 1, z1, 0, 1, 0, 0.5f + 0.5f * x1, 0.5f + 0.5f * z1), vtx(x0, 1, z0, 0, 1, 0, 0.5f + 0.5f * x0, 0.5f + 0.5f * z0));
        addTri(m, vtx(0, -1, 0, 0, -1, 0, 0.5f, 0.5f), vtx(x0, -1, z0, 0, -1, 0, 0.5f + 0.5f * x0, 0.5f - 0.5f * z0), vtx(x1, -1, z1, 0, -1, 0, 0.5f + 0.5f * x1, 0.5f - 0.5f * z1));
    }
    m.normalize();
    return m;
}

MeshData makePlane() {
    MeshData m;
    const MeshVertex tl = vtx(-1, 1, 0, 0, 0, 1, 0, 0), tr = vtx(1, 1, 0, 0, 0, 1, 1, 0), br = vtx(1, -1, 0, 0, 0, 1, 1, 1), bl = vtx(-1, -1, 0, 0, 0, 1, 0, 1);
    addTri(m, tl, bl, br); addTri(m, tl, br, tr);
    m.normalize();
    return m;
}

// ---------------------------------------------------------------------------------------------- OBJ
bool parseObj(const std::string& text, MeshData& out, std::string& error) {
    out = MeshData();
    std::vector<std::array<float, 3>> pos, nrm;
    std::vector<std::array<float, 2>> uv;
    std::istringstream in(text);
    std::string line;
    bool anyUv = false;
    size_t skipped = 0;
    struct Ref { int p = 0, t = 0, n = 0; };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ls(line);
        std::string tag;
        if (!(ls >> tag) || tag[0] == '#') continue;
        if (tag == "v")  { std::array<float, 3> p{0, 0, 0}; ls >> p[0] >> p[1] >> p[2]; pos.push_back(p); }
        else if (tag == "vn") { std::array<float, 3> n{0, 0, 0}; ls >> n[0] >> n[1] >> n[2]; nrm.push_back(n); }
        else if (tag == "vt") { std::array<float, 2> t{0, 0}; ls >> t[0] >> t[1]; uv.push_back(t); }
        else if (tag == "f") {
            std::vector<Ref> refs;
            std::string tok;
            while (ls >> tok) {
                Ref r; int* slot[3] = {&r.p, &r.t, &r.n}; int k = 0; std::string num;
                for (size_t i = 0; i <= tok.size(); i++) {
                    if (i == tok.size() || tok[i] == '/') { if (!num.empty() && k < 3) *slot[k] = std::atoi(num.c_str()); num.clear(); k++; }
                    else num += tok[i];
                }
                auto fix = [](int i, size_t n) { return i < 0 ? (int)n + i + 1 : i; };      // negative = counted from the end
                r.p = fix(r.p, pos.size()); r.t = fix(r.t, uv.size()); r.n = fix(r.n, nrm.size());
                refs.push_back(r);
            }
            if (refs.size() < 3) { skipped++; continue; }
            auto make = [&](const Ref& r, bool& ok) {
                MeshVertex v{0, 0, 0, 0, 0, 0, 0, 0};
                if (r.p < 1 || r.p > (int)pos.size()) { ok = false; return v; }
                const auto& p = pos[r.p - 1]; v.px = p[0]; v.py = p[1]; v.pz = p[2];
                if (r.n >= 1 && r.n <= (int)nrm.size()) { const auto& n = nrm[r.n - 1]; v.nx = n[0]; v.ny = n[1]; v.nz = n[2]; }
                if (r.t >= 1 && r.t <= (int)uv.size()) { v.u = uv[r.t - 1][0]; v.v = 1.0f - uv[r.t - 1][1]; anyUv = true; }     // OBJ's v points up; ours points down
                return v;
            };
            for (size_t i = 1; i + 1 < refs.size(); i++) {             // a fan: polygons become triangles
                bool ok = true;
                MeshVertex a = make(refs[0], ok), b = make(refs[i], ok), c = make(refs[i + 1], ok);
                if (!ok) { skipped++; continue; }
                if (a.nx == 0 && a.ny == 0 && a.nz == 0) {                   // no normals in the file: use the face's
                    float n[3]; faceNormal(a, b, c, n);
                    for (MeshVertex* v : {&a, &b, &c}) { v->nx = n[0]; v->ny = n[1]; v->nz = n[2]; }
                }
                addTri(out, a, b, c);
            }
        }
        if (out.vertices.size() > 3u * 2000000u) { error = "This model is too big (more than 2 million triangles)"; out = MeshData(); return false; }
    }
    if (out.vertices.empty()) { error = skipped ? "The model's faces refer to points that aren't in the file" : "This file has no faces (is it an OBJ model?)"; return false; }
    out.normalize();
    if (!anyUv) {                                   // no texture positions: project the picture from the front
        for (MeshVertex& v : out.vertices) { v.u = v.px * 0.5f + 0.5f; v.v = 0.5f - v.py * 0.5f; }
    }
    return true;
}

bool loadObjFile(const std::string& path, MeshData& out, std::string& error) {
    std::ifstream f(pathFromUtf8(path), std::ios::binary);
    if (!f) { error = "Couldn't open the model file"; return false; }
    std::ostringstream ss; ss << f.rdbuf();
    return parseObj(ss.str(), out, error);
}

namespace {
struct MeshCache {
    std::unique_ptr<MeshData> builtin[4];
    std::map<std::string, std::unique_ptr<MeshData>> files;
    std::map<std::string, std::string> fileErrors;
};
MeshCache& cache() { static MeshCache c; return c; }
}

const MeshData* meshFor(MeshKind kind, const std::string& path, std::string* error) {
    MeshCache& c = cache();
    if (error) error->clear();
    if (kind == MeshKind::File) {
        auto it = c.files.find(path);
        if (it == c.files.end()) {
            auto m = std::make_unique<MeshData>();
            std::string err;
            if (path.empty()) err = "No model file chosen";
            else if (!loadObjFile(path, *m, err)) m.reset();
            if (!m) c.fileErrors[path] = err;
            it = c.files.emplace(path, std::move(m)).first;
        }
        if (it->second) return it->second.get();
        if (error) *error = c.fileErrors[path];
        kind = MeshKind::Cube;                                         // stands in for a file that can't be used
    }
    std::unique_ptr<MeshData>& slot = c.builtin[(int)kind];
    if (!slot) {
        switch (kind) {
            case MeshKind::Sphere:   slot = std::make_unique<MeshData>(makeSphere()); break;
            case MeshKind::Cylinder: slot = std::make_unique<MeshData>(makeCylinder()); break;
            case MeshKind::Plane:    slot = std::make_unique<MeshData>(makePlane()); break;
            default:                 slot = std::make_unique<MeshData>(makeCube());
        }
    }
    return slot.get();
}

void forgetMeshFile(const std::string& path) {
    // the old data stays alive (the renderer may still hold its pointer); a fresh entry is read next time
    static std::vector<std::unique_ptr<MeshData>> retired;
    MeshCache& c = cache();
    auto it = c.files.find(path);
    if (it == c.files.end()) return;
    if (it->second) retired.push_back(std::move(it->second));
    c.files.erase(it);
    c.fileErrors.erase(path);
}

// ---------------------------------------------------------------------------------------------- matrices
Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) { float s = 0; for (int k = 0; k < 4; k++) s += a.at(i, k) * b.at(k, j); r.at(i, j) = s; }
    return r;
}
Mat4 perspective(float fovYDegrees, float aspect, float n, float f) {
    Mat4 r; r.m.fill(0);
    const float t = 1.0f / std::tan(fovYDegrees * kPi / 360.0f);
    r.at(0, 0) = t / std::max(1e-6f, aspect); r.at(1, 1) = t;
    r.at(2, 2) = (f + n) / (n - f); r.at(2, 3) = 2 * f * n / (n - f); r.at(3, 2) = -1;
    return r;
}
Mat4 translation(float x, float y, float z) { Mat4 r; r.at(0, 3) = x; r.at(1, 3) = y; r.at(2, 3) = z; return r; }
Mat4 rotationX(float d) { Mat4 r; const float a = d * kPi / 180, c = std::cos(a), s = std::sin(a); r.at(1, 1) = c; r.at(1, 2) = -s; r.at(2, 1) = s; r.at(2, 2) = c; return r; }
Mat4 rotationY(float d) { Mat4 r; const float a = d * kPi / 180, c = std::cos(a), s = std::sin(a); r.at(0, 0) = c; r.at(0, 2) = s; r.at(2, 0) = -s; r.at(2, 2) = c; return r; }
Mat4 rotationZ(float d) { Mat4 r; const float a = d * kPi / 180, c = std::cos(a), s = std::sin(a); r.at(0, 0) = c; r.at(0, 1) = -s; r.at(1, 0) = s; r.at(1, 1) = c; return r; }
Mat4 scaling(float s) { Mat4 r; r.at(0, 0) = s; r.at(1, 1) = s; r.at(2, 2) = s; return r; }

std::array<float, 4> transformPoint(const Mat4& m, float x, float y, float z) {
    float o[4];
    for (int i = 0; i < 4; i++) o[i] = m.at(i, 0) * x + m.at(i, 1) * y + m.at(i, 2) * z + m.at(i, 3);
    if (std::fabs(o[3]) < 1e-12f) return {0, 0, 0, 0};
    return {o[0] / o[3], o[1] / o[3], o[2] / o[3], o[3]};
}

Mat4 objectModel(float rotX, float rotY, float rotZ, float scale) {
    return rotationY(rotY) * rotationX(rotX) * rotationZ(rotZ) * scaling(std::max(0.0f, scale));
}
float objectCameraDistance(float fovDegrees, float aspect) {
    fovDegrees = std::max(5.0f, std::min(150.0f, fovDegrees));
    // a ball of radius 1 just touches the top and bottom of the view at this distance (or the sides, if narrower)
    const float halfV = fovDegrees * kPi / 360.0f;
    const float halfH = std::atan(std::tan(halfV) * std::max(1e-3f, aspect));
    return 1.0f / std::sin(std::min(halfV, halfH));
}
Mat4 objectViewProjection(float fovDegrees, float aspect) {
    fovDegrees = std::max(5.0f, std::min(150.0f, fovDegrees));
    const float dist = objectCameraDistance(fovDegrees, aspect);
    return perspective(fovDegrees, aspect, std::max(0.01f, dist - 4.0f), dist + 4.0f) * translation(0, 0, -dist);
}
