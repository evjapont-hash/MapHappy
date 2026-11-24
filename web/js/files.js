// The web editor's files: songs, videos and models you add are kept in the browser (IndexedDB), under the path the
// project stores for them ("music/song.mp3"), so they survive a page reload. Files of the published show (web/show/)
// are fetched from the server when they aren't in the store. Also writes the show as a .zip to publish.

const DB = 'maphappy-editor', STORE = 'files';

function openDb() {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(DB, 1);
    req.onupgradeneeded = () => req.result.createObjectStore(STORE);
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}
const tx = (db, mode, fn) => new Promise((resolve, reject) => {
  const t = db.transaction(STORE, mode), r = fn(t.objectStore(STORE));
  t.oncomplete = () => resolve(r?.result); t.onerror = () => reject(t.error);
});

export class FileStore {
  constructor(baseUrl) { this.base = baseUrl; this.blobs = new Map(); this.urls = new Map(); this.db = null; }

  // Loads every stored file (call once at start-up).
  async init() {
    try {
      this.db = await openDb();
      const keys = await tx(this.db, 'readonly', s => s.getAllKeys());
      for (const k of keys) { const b = await tx(this.db, 'readonly', s => s.get(k)); if (b) this.setLocal(k, b); }
    } catch (e) { console.warn('Files will not be kept between visits:', e); }
  }
  setLocal(path, blob) {
    if (this.urls.has(path)) URL.revokeObjectURL(this.urls.get(path));
    this.blobs.set(path, blob); this.urls.set(path, URL.createObjectURL(blob));
  }
  // Adds a file under a project path (e.g. "music/song.mp3"); returns that path.
  async add(path, blob) {
    this.setLocal(path, blob);
    if (this.db) await tx(this.db, 'readwrite', s => s.put(blob, path)).catch(() => {});
    return path;
  }
  async clear() {
    for (const u of this.urls.values()) URL.revokeObjectURL(u);
    this.blobs.clear(); this.urls.clear();
    if (this.db) await tx(this.db, 'readwrite', s => s.clear()).catch(() => {});
  }
  // A path from a project -> a URL: a file added here, or else the published show's copy on the server.
  resolve = (path, rel) => {
    const p = rel || path;
    if (!p) return null;
    if (this.urls.has(p)) return this.urls.get(p);
    const name = p.split(/[\\/]/).pop();
    for (const [k, u] of this.urls) if (k.split('/').pop() === name) return u;
    return new URL(rel || name, this.base).href;
  };
  // The bytes of a file the project uses (for the zip).
  async bytes(path) {
    if (this.blobs.has(path)) return new Uint8Array(await this.blobs.get(path).arrayBuffer());
    const r = await fetch(this.resolve(path, path));
    if (!r.ok) throw new Error(`Couldn't read ${path} (${r.status})`);
    return new Uint8Array(await r.arrayBuffer());
  }
}

// A free, unused project path for a file: "music/song.mp3", "music/song-2.mp3", ...
export function uniquePath(folder, fileName, taken) {
  const clean = fileName.replace(/[^\w.\- ]+/g, '_');
  const dot = clean.lastIndexOf('.'), stem = dot > 0 ? clean.slice(0, dot) : clean, ext = dot > 0 ? clean.slice(dot) : '';
  for (let n = 1; ; n++) {
    const p = `${folder}/${n === 1 ? stem : `${stem}-${n}`}${ext}`;
    if (!taken.has(p)) return p;
  }
}

// ---- a minimal .zip writer (files stored uncompressed: media is compressed already)
const CRC_TABLE = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
export function crc32(bytes) { let c = 0xffffffff; for (let i = 0; i < bytes.length; i++) c = CRC_TABLE[(c ^ bytes[i]) & 0xff] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; }

// files: [{ name: "show/music/song.mp3", data: Uint8Array }] -> a Blob holding the .zip
export function makeZip(files) {
  const enc = new TextEncoder(), parts = [], central = [];
  let offset = 0;
  for (const f of files) {
    const name = enc.encode(f.name), crc = crc32(f.data), size = f.data.length;
    const local = new DataView(new ArrayBuffer(30));
    local.setUint32(0, 0x04034b50, true); local.setUint16(4, 20, true); local.setUint16(6, 0x0800, true);   // version, UTF-8 names
    local.setUint16(8, 0, true); local.setUint32(14, crc, true); local.setUint32(18, size, true); local.setUint32(22, size, true);
    local.setUint16(26, name.length, true);
    parts.push(new Uint8Array(local.buffer), name, f.data);
    const cen = new DataView(new ArrayBuffer(46));
    cen.setUint32(0, 0x02014b50, true); cen.setUint16(4, 20, true); cen.setUint16(6, 20, true); cen.setUint16(8, 0x0800, true);
    cen.setUint32(16, crc, true); cen.setUint32(20, size, true); cen.setUint32(24, size, true); cen.setUint16(28, name.length, true);
    cen.setUint32(42, offset, true);
    central.push(new Uint8Array(cen.buffer), name);
    offset += 30 + name.length + size;
  }
  const cenSize = central.reduce((n, p) => n + p.length, 0);
  const end = new DataView(new ArrayBuffer(22));
  end.setUint32(0, 0x06054b50, true); end.setUint16(8, files.length, true); end.setUint16(10, files.length, true);
  end.setUint32(12, cenSize, true); end.setUint32(16, offset, true);
  return new Blob([...parts, ...central, new Uint8Array(end.buffer)], { type: 'application/zip' });
}

// Offers a Blob as a download.
export function download(blob, name) {
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob); a.download = name;
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 10000);
}
