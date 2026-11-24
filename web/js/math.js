// Geometry for the web player: the same maths as the desktop app (Homography.h, Surface.h, Mesh.cpp), in plain JS.
// Points are {x, y} in normalized coordinates, (0,0) = top-left. 3x3 matrices are row-major arrays of 9 numbers;
// 4x4 matrices are column-major Float32Arrays of 16, as WebGL wants them.

// The perspective transform mapping the unit square (0,0) (1,0) (1,1) (0,1) to the quad p[0..3]
// (Paul Heckbert, "Fundamentals of Texture Mapping"). Returns null if the quad is degenerate.
export function squareToQuad(p) {
  const dx1 = p[1].x - p[2].x, dx2 = p[3].x - p[2].x, dy1 = p[1].y - p[2].y, dy2 = p[3].y - p[2].y;
  const dx3 = p[0].x - p[1].x + p[2].x - p[3].x, dy3 = p[0].y - p[1].y + p[2].y - p[3].y;
  let a, b, c, d, e, f, g, h;
  if (Math.abs(dx3) < 1e-9 && Math.abs(dy3) < 1e-9) {          // a parallelogram: affine
    a = p[1].x - p[0].x; b = p[3].x - p[0].x; c = p[0].x;
    d = p[1].y - p[0].y; e = p[3].y - p[0].y; f = p[0].y;
    g = 0; h = 0;
  } else {
    const den = dx1 * dy2 - dx2 * dy1;
    if (Math.abs(den) < 1e-9) return null;
    g = (dx3 * dy2 - dx2 * dy3) / den;
    h = (dx1 * dy3 - dx3 * dy1) / den;
    a = p[1].x - p[0].x + g * p[1].x; b = p[3].x - p[0].x + h * p[3].x; c = p[0].x;
    d = p[1].y - p[0].y + g * p[1].y; e = p[3].y - p[0].y + h * p[3].y; f = p[0].y;
  }
  return [a, b, c, d, e, f, g, h, 1];
}

export function invert3(m) {
  const c00 = m[4] * m[8] - m[5] * m[7], c01 = -(m[3] * m[8] - m[5] * m[6]), c02 = m[3] * m[7] - m[4] * m[6];
  const det = m[0] * c00 + m[1] * c01 + m[2] * c02;
  if (Math.abs(det) < 1e-9) return null;
  const k = 1 / det;
  return [c00 * k, -(m[1] * m[8] - m[2] * m[7]) * k, (m[1] * m[5] - m[2] * m[4]) * k,
          c01 * k, (m[0] * m[8] - m[2] * m[6]) * k, -(m[0] * m[5] - m[2] * m[3]) * k,
          c02 * k, -(m[0] * m[7] - m[1] * m[6]) * k, (m[0] * m[4] - m[1] * m[3]) * k];
}

export function mapPoint(m, p) {
  const w = m[6] * p.x + m[7] * p.y + m[8];
  return { x: (m[0] * p.x + m[1] * p.y + m[2]) / w, y: (m[3] * p.x + m[4] * p.y + m[5]) / w };
}

// Where the quad's diagonals cross.
export function quadCenter(q) {
  const d1 = { x: q[2].x - q[0].x, y: q[2].y - q[0].y }, d2 = { x: q[3].x - q[1].x, y: q[3].y - q[1].y };
  const den = d1.x * d2.y - d1.y * d2.x;
  if (Math.abs(den) < 1e-9) return { x: (q[0].x + q[1].x + q[2].x + q[3].x) / 4, y: (q[0].y + q[1].y + q[2].y + q[3].y) / 4 };
  const t = ((q[1].x - q[0].x) * d2.y - (q[1].y - q[0].y) * d2.x) / den;
  return { x: q[0].x + t * d1.x, y: q[0].y + t * d1.y };
}

function rotateAspect(v, degrees, aspect) {
  const a = degrees * Math.PI / 180, c = Math.cos(a), s = Math.sin(a), px = v.x * aspect, py = v.y;
  return { x: (px * c - py * s) / aspect, y: px * s + py * c };
}

// A line's quad: from lineA to lineB, lineWidth thick, square in pixels. null if both ends are together.
export function lineQuad(s, aspect) {
  const dx = (s.lineB.x - s.lineA.x) * aspect, dy = s.lineB.y - s.lineA.y, len = Math.hypot(dx, dy);
  if (len < 1e-6) return null;
  const hw = Math.max(0.0005, s.p.lineWidth) * 0.5, nx = -dy / len * hw / aspect, ny = dx / len * hw;
  return [{ x: s.lineA.x - nx, y: s.lineA.y - ny }, { x: s.lineB.x - nx, y: s.lineB.y - ny },
          { x: s.lineB.x + nx, y: s.lineB.y + ny }, { x: s.lineA.x + nx, y: s.lineA.y + ny }];
}

// The output quad as drawn: the mapped quad moved / spun / scaled by the animatable transform (aspect = stage w/h).
export function effectiveOut(s, aspect) {
  const base = (s.shape === 'line' && lineQuad(s, aspect)) || s.out;
  const c = quadCenter(base), p = s.p, sx = p.scale * p.scaleX, sy = p.scale * p.scaleY;
  return base.map(q => {
    let v = { x: (q.x - c.x) * sx, y: (q.y - c.y) * sy };
    v = rotateAspect(v, p.rotation, aspect);
    return { x: c.x + v.x + p.posX, y: c.y + v.y + p.posY };
  });
}

// The input quad as sampled: the picture moved / zoomed / spun inside the surface (aspect = media w/h).
export function effectiveIn(s, aspect) {
  const c = quadCenter(s.in), p = s.p, inv = 1 / Math.max(0.01, p.contentScale);
  return s.in.map(q => {
    let v = { x: (q.x - c.x - p.contentX) * inv, y: (q.y - c.y - p.contentY) * inv };
    v = rotateAspect(v, -p.contentRotation, aspect);
    return { x: c.x + v.x, y: c.y + v.y };
  });
}

// ---------------------------------------------------------------- 4x4 (column-major)
export function mat4() { const m = new Float32Array(16); m[0] = m[5] = m[10] = m[15] = 1; return m; }
export function mul4(a, b) {
  const r = new Float32Array(16);
  for (let i = 0; i < 4; i++) for (let j = 0; j < 4; j++) { let s = 0; for (let k = 0; k < 4; k++) s += a[k * 4 + i] * b[j * 4 + k]; r[j * 4 + i] = s; }
  return r;
}
export function perspective(fovY, aspect, n, f) {
  const r = new Float32Array(16), t = 1 / Math.tan(fovY * Math.PI / 360);
  r[0] = t / Math.max(1e-6, aspect); r[5] = t; r[10] = (f + n) / (n - f); r[14] = 2 * f * n / (n - f); r[11] = -1;
  return r;
}
export function translation(x, y, z) { const r = mat4(); r[12] = x; r[13] = y; r[14] = z; return r; }
export function rotX(d) { const r = mat4(), a = d * Math.PI / 180, c = Math.cos(a), s = Math.sin(a); r[5] = c; r[9] = -s; r[6] = s; r[10] = c; return r; }
export function rotY(d) { const r = mat4(), a = d * Math.PI / 180, c = Math.cos(a), s = Math.sin(a); r[0] = c; r[8] = s; r[2] = -s; r[10] = c; return r; }
export function rotZ(d) { const r = mat4(), a = d * Math.PI / 180, c = Math.cos(a), s = Math.sin(a); r[0] = c; r[4] = -s; r[1] = s; r[5] = c; return r; }
export function scaling(s) { const r = mat4(); r[0] = r[5] = r[10] = s; return r; }
export function transformPoint(m, x, y, z) {
  const o = [0, 1, 2, 3].map(i => m[i] * x + m[4 + i] * y + m[8 + i] * z + m[12 + i]);
  return [o[0] / o[3], o[1] / o[3], o[2] / o[3], o[3]];
}

export function objectModel(rx, ry, rz, scale) { return mul4(mul4(mul4(rotY(ry), rotX(rx)), rotZ(rz)), scaling(Math.max(0, scale))); }
export function objectCameraDistance(fov, aspect) {
  fov = Math.max(5, Math.min(150, fov));
  const halfV = fov * Math.PI / 360, halfH = Math.atan(Math.tan(halfV) * Math.max(1e-3, aspect));
  return 1 / Math.sin(Math.min(halfV, halfH));
}
export function objectViewProjection(fov, aspect) {
  fov = Math.max(5, Math.min(150, fov));
  const dist = objectCameraDistance(fov, aspect);
  return mul4(perspective(fov, aspect, Math.max(0.01, dist - 4), dist + 4), translation(0, 0, -dist));
}
