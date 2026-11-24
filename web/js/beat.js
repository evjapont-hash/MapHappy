// Tempo and beat-grid detection, as in the desktop app's BeatDetect.h:
//   1. spectral flux per frequency band (= onsets), 2. autocorrelation x Fourier magnitude finds the tempo,
//   3. a comb search refines the BPM and finds where the beats fall.
// detectBeats(monoSamples, sampleRate) -> { bpm, offset, confidence } (bpm 0 = nothing reliable).

const WIN = 1024, HOP = 256;

function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const a = i + k, b = a + len / 2;
        const vr = re[b] * cr - im[b] * ci, vi = re[b] * ci + im[b] * cr;
        re[b] = re[a] - vr; im[b] = im[a] - vi; re[a] += vr; im[a] += vi;
        const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
      }
    }
  }
}

export function onsetCurve(x, fps) {
  const n = x.length;
  if (n < WIN * 4) return new Float32Array(0);
  const win = new Float32Array(WIN).map((_, i) => 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (WIN - 1)));
  const edge = [1, 4, 9, 25, 65, 129, 257], weight = [1.6, 1.2, 1.0, 0.8, 0.6, 0.5], B = 6;
  const frames = Math.floor((n - WIN) / HOP);
  const band = [...Array(B)].map(() => new Float32Array(frames));
  let prev = new Float32Array(257), cur = new Float32Array(257);
  const re = new Float32Array(WIN), im = new Float32Array(WIN);
  for (let f = 0; f < frames; f++) {
    const s = f * HOP;
    for (let i = 0; i < WIN; i++) { re[i] = x[s + i] * win[i]; im[i] = 0; }
    fft(re, im);
    for (let k = 1; k < 257; k++) cur[k] = Math.log1p(80 * Math.hypot(re[k], im[k]) / WIN);
    for (let b = 0; b < B; b++) {
      let flux = 0;
      for (let k = edge[b]; k < edge[b + 1]; k++) flux += Math.max(0, cur[k] - prev[k]);
      band[b][f] = flux / (edge[b + 1] - edge[b]);
    }
    [prev, cur] = [cur, prev];
  }
  const out = new Float32Array(frames), half = Math.floor(0.25 * fps);
  for (let b = 0; b < B; b++) {
    const o = band[b], r = new Float32Array(frames);
    let run = 0, lo = 0, hi = 0;
    for (let i = 0; i < frames; i++) {
      while (hi < frames && hi <= i + half) run += o[hi++];
      while (lo + half < i) run -= o[lo++];
      r[i] = Math.max(0, o[i] - run / (hi - lo));
    }
    let sum = 0; for (const v of r) sum += v * v;
    const rms = Math.sqrt(sum / Math.max(1, frames));
    if (rms < 1e-9) continue;
    for (let i = 0; i < frames; i++) out[i] += weight[b] * r[i] / rms;
  }
  let sum = 0; for (const v of out) sum += v * v;
  const rms = Math.sqrt(sum / Math.max(1, frames));
  if (rms > 1e-9) for (let i = 0; i < frames; i++) out[i] /= rms;
  return out;
}

const sampleAt = (o, x) => { if (x < 0 || x >= o.length - 1) return 0; const i = Math.floor(x), f = x - i; return o[i] * (1 - f) + o[i + 1] * f; };

export function detectBeats(samples, rate, maxSeconds = 90) {
  const info = { bpm: 0, offset: 0, confidence: 0 };
  const x = samples.length > rate * maxSeconds ? samples.subarray(0, rate * maxSeconds) : samples;
  const fps = rate / HOP, o = onsetCurve(x, fps);
  if (o.length < fps * 6) return info;
  const lagMin = Math.floor(60 * fps / 190), lagMax = Math.ceil(60 * fps / 70);
  const ac = lag => { let s = 0, c = 0; for (let i = 0; i + lag < o.length; i++) { s += o[i] * o[i + lag]; c++; } return c ? s / c : 0; };
  const fourier = bpm => {
    const w = 2 * Math.PI * (bpm / 60) / fps;
    let re = 0, im = 0;
    for (let i = 0; i < o.length; i++) { re += o[i] * Math.cos(w * i); im += o[i] * Math.sin(w * i); }
    return Math.hypot(re, im) / o.length;
  };
  let bestScore = -1, bestLag = 0;
  for (let lag = lagMin; lag <= lagMax; lag++) {
    const bpm = 60 * fps / lag;
    const prior = Math.exp(-0.5 * (Math.log2(bpm / 125) / 0.9) ** 2);
    const a = Math.max(0, ac(lag) + 0.5 * ac(lag * 2) + 0.25 * ac(lag * 4));
    const score = a * (fourier(bpm) + 0.5 * fourier(bpm * 2)) * prior;
    if (score > bestScore) { bestScore = score; bestLag = lag; }
  }
  if (!bestLag || bestScore <= 0) return info;
  const bpm0 = 60 * fps / bestLag;
  let bestComb = -1, bestBpm = bpm0, bestPhase = 0;
  for (let bpm = bpm0 - 3; bpm <= bpm0 + 3; bpm += 0.05) {
    if (bpm < 60) continue;
    const period = 60 * fps / bpm;
    for (let ph = 0; ph < period; ph += 0.5) {
      let s = 0, c = 0;
      for (let t = ph; t < o.length - 1; t += period) { s += sampleAt(o, t); c++; }
      s /= Math.max(1, c);
      if (s > bestComb) { bestComb = s; bestBpm = bpm; bestPhase = ph; }
    }
  }
  const beat = 60 / bestBpm;
  let offset = bestPhase / fps + 0.0129;
  offset = ((offset % beat) + beat) % beat;
  return { bpm: Math.round(bestBpm * 100) / 100, offset, confidence: Math.max(0, Math.min(1, bestComb / 4)) };
}
