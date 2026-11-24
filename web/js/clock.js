// The show's master clock. With songs on the timeline it is the AUDIO clock (Web Audio), so picture can't drift
// from the music; with none, a plain timer. `avOffset` works as in the desktop app: audio = picture + avOffset.
export class ShowClock {
  constructor(show) {
    this.show = show;
    this.ctx = null; this.buffers = new Map(); this.sources = [];
    this.playing = false; this.pos = 0;              // picture time when paused / when play started
    this.startedAt = 0;                               // ctx.currentTime or performance.now()/1000 when play started
    this.loadErrors = [];
  }

  // Decodes the songs. Must follow a user gesture (browsers only start audio after a click or key press).
  async load() {
    if (!this.show.audioAssets.size) return;
    this.ctx = new AudioContext({ latencyHint: 'interactive' });
    await Promise.all([...this.show.audioAssets.values()].map(async a => {
      try {
        const r = await fetch(a.url);
        if (!r.ok) throw new Error(`${r.status} ${r.statusText}`);
        this.buffers.set(a.id, await this.ctx.decodeAudioData(await r.arrayBuffer()));
      } catch (e) { this.loadErrors.push(`Couldn't load the song "${a.name}": ${e.message || e}`); }
    }));
  }

  now() { return this.ctx ? this.ctx.currentTime : performance.now() / 1000; }
  get time() { return this.playing ? this.pos + (this.now() - this.startedAt) : this.pos; }

  play() {
    if (this.playing) return;
    if (this.pos >= this.show.timeline.length) this.pos = 0;
    if (this.ctx) this.ctx.resume();
    this.playing = true;
    this.startedAt = this.now() + (this.ctx ? 0.05 : 0);          // a moment of lead so the first sound isn't clipped
    this.schedule();
  }
  pause() { if (!this.playing) return; this.pos = this.time; this.playing = false; this.stopSources(); }
  toggle() { this.playing ? this.pause() : this.play(); }
  seek(t) {
    const was = this.playing;
    if (was) this.pause();
    this.pos = Math.max(0, Math.min(this.show.timeline.length, t));
    if (was) this.play();
  }

  // Starts every audio clip that is still to play, at its place on the timeline.
  schedule() {
    if (!this.ctx) return;
    const audioFrom = this.pos + this.show.avOffset;              // where the SOUND is when the picture is at pos
    for (const c of this.show.timeline.audioClips) {
      const buf = this.buffers.get(c.asset), end = c.start + c.duration;
      if (!buf || audioFrom >= end) continue;
      const skip = Math.max(0, audioFrom - c.start);
      const src = this.ctx.createBufferSource(), gain = this.ctx.createGain();
      src.buffer = buf; gain.gain.value = c.gain;
      src.connect(gain).connect(this.ctx.destination);
      src.start(this.startedAt + Math.max(0, c.start - audioFrom), c.offset + skip, c.duration - skip);
      this.sources.push(src);
    }
  }
  stopSources() { for (const s of this.sources) { try { s.stop(); } catch { /* already finished */ } } this.sources = []; }

  // Call once a frame: stops (or loops) at the end of the timeline.
  tick() {
    const len = this.show.timeline.length;
    if (!this.playing || this.time < len) return;
    if (this.show.loop) { this.pause(); this.pos = 0; this.play(); }
    else { this.pause(); this.pos = len; }
  }
}
