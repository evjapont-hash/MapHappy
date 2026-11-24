// 3D models for 3D object surfaces, as in the desktop app's Mesh.cpp: the built-in shapes and an OBJ reader.
// A mesh is a Float32Array of vertices, 8 numbers each (position, normal, u, v), three per triangle,
// centred and scaled to fit a ball of radius 1. Texture v points DOWN ((0,0) = the picture's top-left).

function normalize(verts) {
  const n = verts.length / 8;
  if (!n) return verts;
  const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < n; i++) for (let k = 0; k < 3; k++) { lo[k] = Math.min(lo[k], verts[i * 8 + k]); hi[k] = Math.max(hi[k], verts[i * 8 + k]); }
  const c = [0, 1, 2].map(k => (lo[k] + hi[k]) / 2);
  let r = 0;
  for (let i = 0; i < n; i++) {
    for (let k = 0; k < 3; k++) verts[i * 8 + k] -= c[k];
    r = Math.max(r, Math.hypot(verts[i * 8], verts[i * 8 + 1], verts[i * 8 + 2]));
  }
  if (r > 1e-12) for (let i = 0; i < n; i++) for (let k = 0; k < 3; k++) verts[i * 8 + k] /= r;
  return verts;
}

const v8 = (x, y, z, nx, ny, nz, u, v) => [x, y, z, nx, ny, nz, u, v];

export function makeCube() {
  const out = [];
  const faces = [   // normal, the direction of u (right) and of v (down) on it, seen from outside
    [[0, 0, 1], [1, 0, 0], [0, -1, 0]], [[1, 0, 0], [0, 0, -1], [0, -1, 0]], [[0, 0, -1], [-1, 0, 0], [0, -1, 0]],
    [[-1, 0, 0], [0, 0, 1], [0, -1, 0]], [[0, 1, 0], [1, 0, 0], [0, 0, 1]], [[0, -1, 0], [1, 0, 0], [0, 0, -1]],
  ];
  for (const [n, r, d] of faces) {
    const corner = (u, v) => { const a = u * 2 - 1, b = v * 2 - 1; return v8(n[0] + a * r[0] + b * d[0], n[1] + a * r[1] + b * d[1], n[2] + a * r[2] + b * d[2], n[0], n[1], n[2], u, v); };
    const tl = corner(0, 0), tr = corner(1, 0), br = corner(1, 1), bl = corner(0, 1);
    out.push(...tl, ...bl, ...br, ...tl, ...br, ...tr);
  }
  return normalize(new Float32Array(out));
}

export function makeSphere(segments = 48, rings = 24) {
  const out = [];
  const at = (s, r) => {
    const u = s / segments, v = r / rings, th = (u - 0.5) * 2 * Math.PI, ph = v * Math.PI;
    const x = Math.sin(ph) * Math.sin(th), y = Math.cos(ph), z = Math.sin(ph) * Math.cos(th);
    return v8(x, y, z, x, y, z, u, v);
  };
  for (let r = 0; r < rings; r++) for (let s = 0; s < segments; s++) {
    const a = at(s, r), b = at(s + 1, r), c = at(s + 1, r + 1), d = at(s, r + 1);
    if (r > 0) out.push(...a, ...d, ...c);
    if (r < rings - 1) out.push(...a, ...c, ...b);
  }
  return normalize(new Float32Array(out));
}

export function makeCylinder(segments = 48) {
  const out = [];
  for (let s = 0; s < segments; s++) {
    const u0 = s / segments, u1 = (s + 1) / segments, a0 = (u0 - 0.5) * 2 * Math.PI, a1 = (u1 - 0.5) * 2 * Math.PI;
    const x0 = Math.sin(a0), z0 = Math.cos(a0), x1 = Math.sin(a1), z1 = Math.cos(a1);
    const t0 = v8(x0, 1, z0, x0, 0, z0, u0, 0), t1 = v8(x1, 1, z1, x1, 0, z1, u1, 0), b0 = v8(x0, -1, z0, x0, 0, z0, u0, 1), b1 = v8(x1, -1, z1, x1, 0, z1, u1, 1);
    out.push(...t0, ...b0, ...b1, ...t0, ...b1, ...t1);
    out.push(...v8(0, 1, 0, 0, 1, 0, 0.5, 0.5), ...v8(x1, 1, z1, 0, 1, 0, 0.5 + 0.5 * x1, 0.5 + 0.5 * z1), ...v8(x0, 1, z0, 0, 1, 0, 0.5 + 0.5 * x0, 0.5 + 0.5 * z0));
    out.push(...v8(0, -1, 0, 0, -1, 0, 0.5, 0.5), ...v8(x0, -1, z0, 0, -1, 0, 0.5 + 0.5 * x0, 0.5 - 0.5 * z0), ...v8(x1, -1, z1, 0, -1, 0, 0.5 + 0.5 * x1, 0.5 - 0.5 * z1));
  }
  return normalize(new Float32Array(out));
}

export function makePlane() {
  const tl = v8(-1, 1, 0, 0, 0, 1, 0, 0), tr = v8(1, 1, 0, 0, 0, 1, 1, 0), br = v8(1, -1, 0, 0, 0, 1, 1, 1), bl = v8(-1, -1, 0, 0, 0, 1, 0, 1);
  return normalize(new Float32Array([...tl, ...bl, ...br, ...tl, ...br, ...tr]));
}

// Wavefront OBJ: v, vt, vn, f (polygons become triangle fans; negative indices count from the end).
// Missing normals are computed per face; missing texture positions are projected from the front.
export function parseObj(text) {
  const pos = [], nrm = [], uv = [], out = [];
  let anyUv = false;
  for (const raw of text.split('\n')) {
    const parts = raw.trim().split(/\s+/);
    const tag = parts[0];
    if (tag === 'v') pos.push([+parts[1] || 0, +parts[2] || 0, +parts[3] || 0]);
    else if (tag === 'vn') nrm.push([+parts[1] || 0, +parts[2] || 0, +parts[3] || 0]);
    else if (tag === 'vt') uv.push([+parts[1] || 0, +parts[2] || 0]);
    else if (tag === 'f') {
      const refs = parts.slice(1).map(tok => {
        const [p, t, n] = tok.split('/').map(x => (x === undefined || x === '' ? 0 : parseInt(x, 10)));
        const fix = (i, len) => (i < 0 ? len + i + 1 : i);
        return { p: fix(p, pos.length), t: fix(t || 0, uv.length), n: fix(n || 0, nrm.length) };
      });
      if (refs.length < 3) continue;
      const make = r => {
        if (!(r.p >= 1 && r.p <= pos.length)) return null;
        const v = [...pos[r.p - 1], 0, 0, 0, 0, 0];
        if (r.n >= 1 && r.n <= nrm.length) [v[3], v[4], v[5]] = nrm[r.n - 1];
        if (r.t >= 1 && r.t <= uv.length) { v[6] = uv[r.t - 1][0]; v[7] = 1 - uv[r.t - 1][1]; anyUv = true; }
        return v;
      };
      for (let i = 1; i + 1 < refs.length; i++) {
        const tri = [make(refs[0]), make(refs[i]), make(refs[i + 1])];
        if (tri.some(v => !v)) continue;
        if (tri[0][3] === 0 && tri[0][4] === 0 && tri[0][5] === 0) {
          const [a, b, c] = tri, u = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], w = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
          let n = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]];
          const l = Math.hypot(...n); n = l > 1e-12 ? n.map(x => x / l) : [0, 0, 1];
          for (const v of tri) [v[3], v[4], v[5]] = n;
        }
        for (const v of tri) out.push(...v);
      }
    }
  }
  if (!out.length) throw new Error('This file has no usable faces (is it an OBJ model?)');
  const verts = normalize(new Float32Array(out));
  if (!anyUv) for (let i = 0; i < verts.length; i += 8) { verts[i + 6] = verts[i] * 0.5 + 0.5; verts[i + 7] = 0.5 - verts[i + 1] * 0.5; }
  return verts;
}

export function builtinMesh(kind) {
  if (kind === 'sphere') return makeSphere();
  if (kind === 'cylinder') return makeCylinder();
  if (kind === 'plane') return makePlane();
  return makeCube();
}
