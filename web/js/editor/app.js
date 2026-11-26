// The MapHappy web editor: build a projection-mapping show in the browser, then publish it with the web player.
// Plain JavaScript + WebGL 2: the stage is drawn by the same renderer (and shaders) as the player and the desktop app.
import { makeTarget } from '../gl.js';
import { readShow, writeShow, emptyShow, applyTimeline, PresetPlayer, capturePreset } from '../show.js';
import { buildMedia, createSource } from '../media.js';
import { Renderer } from '../renderer.js';
import { ShowClock } from '../clock.js';
import { FileStore, uniquePath, makeZip, download } from '../files.js';
import { detectBeats } from '../beat.js';
import * as ops from '../editor-ops.js';
import { splitAudioClip, deleteAudioClip } from '../editor-ops.js';
import { StageView } from './stage.js';
import { TimelineView } from './timeline-view.js';
import { Panels, AUDIO_EXT, VIDEO_EXT, OBJ_EXT } from './panels.js';
import { el } from './ui.js';
import { icon, fillIcons } from './icons.js';
import { CHANNEL, frameMessage } from '../output.js';

const $ = id => document.getElementById(id);
fillIcons();
const AUTOSAVE = 'maphappy-editor-show';
const PUBLISHED = new URL('show/show.maphappy', location.href).href;

class Editor {
  constructor() {
    this.gl = $('stage').getContext('webgl2', { antialias: false, alpha: false, preserveDrawingBuffer: true });
    if (!this.gl) throw new Error('This browser has no WebGL 2. Try a current Chrome, Edge, Firefox or Safari.');
    this.renderer = new Renderer(this.gl);
    this.files = new FileStore(PUBLISHED);
    this.player = new PresetPlayer();
    this.sel = { surface: 0, key: null };
    this.multi = new Set();                  // surfaces selected together in the list (Ctrl / Shift-click); includes sel.surface this.selMedia = 0; this.selFolder = 0; this.selMask = -1;
    this.snap = true; this.live = false; this.blackout = false;
    this.undo = []; this.redo = []; this.snapshot = ''; this.savedSnapshot = '';
    this.stageView = new StageView(this, $('stage'), $('overlay'));
    this.timelineView = new TimelineView(this, $('timeline'));
    this.panels = new Panels(this, $('panel'));
    // the projector output window(s): they say hello, then follow the show and the live state
    this.output = new BroadcastChannel(CHANNEL);
    this.outputs = false;
    this.output.onmessage = e => { if (e.data?.type === 'hello') { this.outputs = true; this.sendShow(true); } };
  }

  // Sends the show to the output windows (at most ~8 times a second while dragging).
  sendShow(now = false) {
    if (!this.outputs) return;
    clearTimeout(this.sendTimer);
    const send = () => { this.lastSend = performance.now(); this.output.postMessage({ type: 'show', json: writeShow(this.show) }); };
    if (now || performance.now() - (this.lastSend || 0) > 120) send(); else this.sendTimer = setTimeout(send, 120);
  }
  openOutput() {
    const w = window.open('output.html', 'maphappy-output', 'popup,width=1024,height=600');
    if (!w) this.status('The browser blocked the output window: allow pop-ups for this page, then press Output again.');
    else this.status('Output window opened: drag it onto the projector, then double-click it for full screen.');
  }

  // ---------------------------------------------------------------- the show
  // Replaces the show with a project file's contents. `media` sources whose entries didn't change are kept.
  load(json, { keepMedia = false, resetHistory = true } = {}) {
    const old = this.show;
    const show = readShow(json, this.files.resolve);
    const warn = w => show.warnings.push(w);
    if (keepMedia && old) {
      show.media = new Map();
      for (const m of show.mediaList) {
        const prev = old.media.get(m.id), prevEntry = old.mediaList.find(x => x.id === m.id);
        if (prev && prevEntry && JSON.stringify(prevEntry) === JSON.stringify(m)) { show.media.set(m.id, prev); if (prev.refresh) prev.params = m.params; }
        else if (m.id === 1 && prev) show.media.set(1, prev);
        else { try { const src = createSource(this.gl, m, this.files.resolve, warn); if (src) show.media.set(m.id, src); } catch (e) { warn(e.message); } }
      }
      for (const m of show.mediaList) { const src = show.media.get(m.id); if (src?.refresh) src.params = m.params; }
    } else show.media = buildMedia(this.gl, show.mediaList, this.files.resolve, warn);
    for (const a of show.audioAssets.values()) a.url = this.files.resolve(a.path, a.path);
    this.show = show;
    if (!this.stageRT || this.stageRT.w !== show.outW || this.stageRT.h !== show.outH) this.stageRT = makeTarget(this.gl, show.outW, show.outH);
    if (this.clock) { const buffers = this.clock.buffers, ctx = this.clock.ctx, t = this.clock.time, playing = this.clock.playing; this.clock.pause(); this.clock = new ShowClock(show); this.clock.ctx = ctx; this.clock.buffers = buffers; this.clock.pos = t; if (playing) this.clock.play(); }
    else this.clock = new ShowClock(show);
    const back = keepMedia && old ? show.surfaces.find(s => !old.surfaces.some(o => o.id === s.id)) : null;     // undo brought a surface back: select it
    if (back) this.sel.surface = back.id;
    else if (!show.surfaces.some(s => s.id === this.sel.surface) && !(keepMedia && !this.sel.surface)) this.sel.surface = show.surfaces[0]?.id || 0;
    if (!(this.selMask < (this.selected()?.masks.length ?? 0))) this.selMask = -1;
    this.sel.key = null; this.sel.keys = []; this.sel.clip = null; this.sel.track = null; this.sel.audio = null;
    this.multi = new Set([...this.multi].filter(id => show.surfaces.some(s => s.id === id)));
    if (this.sel.surface) this.multi.add(this.sel.surface);
    this.snapshot = JSON.stringify(writeShow(show));
    if (resetHistory) { this.undo = []; this.redo = []; this.savedSnapshot = this.snapshot; }
    this.panels.render();
    this.sendShow(true);
    if (show.warnings.length) this.status(show.warnings.join(' · '));
  }

  selected() { return this.show.surfaces.find(s => s.id === this.sel.surface) || null; }
  select(id) { const many = this.multi.size > 1; this.sel.track = null; this.sel.key = null; this.sel.keys = []; this.sel.clip = null; this.sel.audio = null; this.multi = new Set(id ? [id] : []); if (this.sel.surface !== id || this.selFolder || many) { this.sel.surface = id; this.selFolder = 0; this.selMask = -1; this.panels.tab = 'surfaces'; this.panels.render(); } }
  // Several surfaces at once; `primary` is the one the inspector and the stage edit.
  selectMany(ids, primary) { this.sel.key = null; this.sel.audio = null; this.multi = new Set(ids); this.sel.surface = primary; this.selFolder = 0; this.selMask = -1; this.panels.render(); }
  deselect() { this.multi = new Set(); this.sel.key = null; this.sel.keys = []; this.sel.clip = null; this.sel.audio = null; if (this.sel.surface || this.selFolder) { this.sel.surface = 0; this.selFolder = 0; this.selMask = -1; this.panels.render(); } }
  refreshPanels() { this.panels.render(); }
  selectKey(k) { this.sel.key = k; this.sel.keys = []; this.sel.clip = null; }
  defaultMedia() { return this.show.mediaList.some(m => m.id === this.selMedia) ? this.selMedia : 1; }

  // Something changed (while dragging): redraw, but don't make an undo step yet.
  touched() { this.dirty = true; this.sendShow(); }
  // A finished change: an undo step, and the autosave picks it up.
  commit() {
    const snap = JSON.stringify(writeShow(this.show));
    if (snap !== this.snapshot) { this.undo.push(this.snapshot); if (this.undo.length > 100) this.undo.shift(); this.redo = []; this.snapshot = snap; }
    this.panels.refreshValues();
    this.updateTitle();
    this.sendShow();
  }
  undoStep(back = true) {
    const from = back ? this.undo : this.redo, to = back ? this.redo : this.undo;
    if (!from.length) return;
    to.push(this.snapshot);
    const snap = from.pop();
    this.load(JSON.parse(snap), { keepMedia: true, resetHistory: false });
    this.snapshot = snap;
    this.updateTitle();
  }
  updateTitle() {
    const unsaved = this.snapshot !== this.savedSnapshot;
    document.title = `${unsaved ? '• ' : ''}MapHappy Editor`;
    $('unsaved').hidden = !unsaved;
  }
  status(msg) { const s = $('status'); s.textContent = msg; s.classList.add('show'); clearTimeout(this.statusTimer); this.statusTimer = setTimeout(() => s.classList.remove('show'), 6000); }

  duplicate(s) {
    return ops.pasteSurface(this.show, s);
  }
  copySurface() {
    const s = this.selected();
    if (!s) return false;
    this.clipboard = { kind: 'surface', data: { ...JSON.parse(JSON.stringify({ ...s, meshUrl: null })), meshUrl: s.meshUrl }, pastes: 0 };
    this.status(`Copied ${s.name}. Ctrl+V pastes it.`);
    return true;
  }
  pasteSurface() {
    if (this.clipboard?.kind !== 'surface') return false;
    const c = ops.pasteSurface(this.show, this.clipboard.data, 0.03 * ++this.clipboard.pastes);      // each paste a step further, so they don't hide each other
    this.select(c.id);
    this.commit();
    this.panels.render();
    return true;
  }
  // ---- the timeline's selection: keyframes (several, with Shift / Ctrl), or one clip
  // sel.keys = [{ track, clip, key }]; sel.key = the one clicked last, as { track, clip, index, key }; sel.clip = { track, clip }
  selectedKeys() { return this.sel.keys?.length ? this.sel.keys : this.sel.key ? [{ track: this.sel.key.track, clip: this.sel.key.clip, key: this.sel.key.key || this.sel.key.clip.keys[this.sel.key.index] }] : []; }
  setKeySelection(items, primary = items[items.length - 1]) {
    this.sel.keys = items; this.sel.clip = null; this.sel.audio = null;
    this.sel.key = primary ? { track: primary.track, clip: primary.clip, index: primary.clip.keys.indexOf(primary.key), key: primary.key } : null;
  }
  hasTimelineSelection() { return !!(this.sel.key || this.sel.keys?.length || this.sel.clip); }
  copyTimeline() {
    const how = 'Select another surface (or click its row) to paste there. Ctrl+V pastes at the playhead, Ctrl+Shift+V at the same time as the original.';
    if (this.sel.clip) { this.clipboard = ops.copyClip(this.sel.clip.track, this.sel.clip.clip); this.status(`Copied the clip. ${how}`); return true; }
    const keys = this.selectedKeys();
    if (!keys.length) return false;
    this.clipboard = ops.copyKeys(keys);
    this.status(`Copied ${keys.length} keyframe${keys.length > 1 ? 's' : ''}. ${how}`);
    return true;
  }
  // Pastes copied keyframes or a clip at the playhead (or at `at`; sameTime = where the original was). Where to:
  // the row clicked last (`targetTrack`), else the selected surface (the same parameters on it), else where they came from.
  pasteTimeline(at = this.clock.time, targetTrack = this.sel.track, sameTime = false) {
    const cb = this.clipboard;
    if (cb?.kind !== 'clip' && cb?.kind !== 'keys') return false;
    if (sameTime) at = cb.start;
    const target = targetTrack?.kind === 'param' && this.show.timeline.tracks.includes(targetTrack) ? targetTrack.path : null;
    const surface = !target && this.sel.surface ? this.sel.surface : 0;
    if (cb.kind === 'clip') {
      const r = ops.pasteClip(this.show, cb, at, { target, surface });
      if (!r) { this.status('Nothing to paste onto: the surface that clip was for is gone.'); return true; }
      this.sel.keys = []; this.sel.key = null; this.sel.clip = r.clip ? { track: r.track, clip: r.clip } : null; this.sel.track = r.track;
      if (Math.abs(r.clip.start - at) > 1e-6) this.status('There was already a clip there, so it went in the next free spot.');
    } else {
      const added = ops.pasteKeys(this.show, cb, at, target, surface);
      if (!added.length) { this.status('Nothing to paste onto: the surface those keyframes were for is gone.'); return true; }
      this.setKeySelection(added);
    }
    this.commit(); this.panels.refreshValues();
    return true;
  }

  // The Delete key: what was clicked last, i.e. a selected keyframe or song clip, else the mask being edited, else the surface.
  deleteSelected() {
    const s = this.selected();
    if (this.sel.key || this.sel.keys?.length) { ops.deleteKeys(this.show, this.selectedKeys()); this.sel.key = null; this.sel.keys = []; }
    else if (this.sel.clip) { ops.removeClip(this.show, this.sel.clip.track, this.sel.clip.clip); this.sel.clip = null; }
    else if (this.sel.audio) {
      deleteAudioClip(this.show, this.sel.audio.track, this.sel.audio.clip); this.sel.audio = null;
      if (this.clock.playing) { this.clock.pause(); this.clock.play(); }
    }
    else if (!s || this.selFolder) return false;
    else if (this.selMask >= 0 && s.masks[this.selMask]) { s.masks.splice(this.selMask, 1); this.selMask = -1; }
    else {
      const gone = this.show.surfaces.filter(x => x === s || this.multi.has(x.id));
      for (const g of gone) ops.removeSurface(this.show, g.id);
      this.deselect(); this.status(`Deleted ${gone.length > 1 ? `${gone.length} surfaces` : s.name}. Ctrl+Z brings ${gone.length > 1 ? 'them' : 'it'} back.`);
    }
    this.commit();
    this.panels.render();
    return true;
  }

  // ---------------------------------------------------------------- playback and sound
  // Browsers only allow sound after a click or key press: called from those.
  async ensureAudio() {
    if (!this.clock.ctx) this.clock.ctx = new AudioContext({ latencyHint: 'interactive' });
    if (this.clock.ctx.state === 'suspended') await this.clock.ctx.resume();
    const missing = [...this.show.audioAssets.values()].filter(a => !this.clock.buffers.has(a.id));
    if (!missing.length) return;
    this.status('Loading the music…');
    await Promise.all(missing.map(async a => {
      try {
        const r = await fetch(a.url || this.files.resolve(a.path, a.path));
        if (!r.ok) throw new Error(`${r.status}`);
        this.clock.buffers.set(a.id, await this.clock.ctx.decodeAudioData(await r.arrayBuffer()));
      } catch (e) { this.status(`Couldn't load the song "${a.name}" (${e.message}). Add it again from the Media tab.`); }
    }));
  }
  async togglePlay() {
    await this.ensureAudio();
    for (const m of this.show.media.values()) if (m.start && !m.started) { m.start(); m.started = true; }
    this.clock.toggle();
  }
  seek(t) { this.clock.seek(Math.max(0, t)); }

  // Splits the selected song clip (or the one under the playhead) at the playhead.
  splitSong() {
    const t = this.clock.time;
    let target = this.sel.audio;
    if (!target || t <= target.clip.start || t >= target.clip.start + target.clip.duration) {
      target = null;
      for (const tr of this.show.timeline.tracks) if (tr.kind === 'audio') for (const c of tr.audioClips) if (t > c.start && t < c.start + c.duration) target = { track: tr, clip: c };
    }
    if (!target) { this.status('Put the playhead inside a song clip to split it there.'); return; }
    const second = splitAudioClip(this.show, target.track, target.clip, t);
    if (second) { this.sel.audio = { track: target.track, clip: second }; this.clock.playing && (this.clock.pause(), this.clock.play()); this.commit(); }
  }

  async detectTempo(assetId) {
    await this.ensureAudio();
    const buf = this.clock.buffers.get(assetId), a = this.show.audioAssets.get(assetId);
    if (!buf || !a) return null;
    const ch = buf.numberOfChannels > 1 ? buf.getChannelData(0).map((v, i) => 0.5 * (v + buf.getChannelData(1)[i])) : buf.getChannelData(0);
    const r = detectBeats(ch, buf.sampleRate);
    if (!r.bpm) { this.status("Couldn't find a steady beat: type the tempo, or tap it."); return null; }
    a.bpm = r.bpm; a.beatOffset = r.offset;
    this.commit();
    this.status(`Tempo: ${r.bpm} BPM. The timeline shows the beats, and keyframes snap to them.`);
    return r;
  }

  // ---------------------------------------------------------------- files
  // Songs go on the timeline, videos into the media, OBJ models onto a 3D object surface.
  async addFiles(list, forSurface = null) {
    const taken = new Set([...this.files.blobs.keys(), ...this.show.mediaList.map(m => m.pathRel || m.path), ...[...this.show.audioAssets.values()].map(a => a.path)]);
    for (const f of list) {
      if (AUDIO_EXT.test(f.name)) {
        const path = await this.files.add(uniquePath('music', f.name, taken), f); taken.add(path);
        await this.ensureAudio();
        let buf;
        try { buf = await this.clock.ctx.decodeAudioData(await f.arrayBuffer()); } catch { this.status(`"${f.name}" isn't a sound file this browser can play (try MP3 or WAV).`); continue; }
        const id = ops.addSong(this.show, f.name, path, buf.duration);
        this.clock.buffers.set(id, buf);
        this.show.audioAssets.get(id).url = this.files.resolve(path, path);
        this.commit();
        this.status(`Added "${f.name}" to the timeline. Finding its tempo…`);
        setTimeout(() => this.detectTempo(id), 50);
      } else if (VIDEO_EXT.test(f.name)) {
        const path = await this.files.add(uniquePath('media', f.name, taken), f); taken.add(path);
        const id = Math.max(1, ...this.show.mediaList.map(m => m.id)) + 1;
        const entry = { id, name: f.name, kind: 'video', path, pathRel: path, playing: true, loop: true };
        this.show.mediaList.push(entry);
        const src = createSource(this.gl, entry, this.files.resolve, w => this.status(w));
        this.show.media.set(id, src); src.start(); src.started = true;
        const s = this.selected(); if (s && s.media === 1) s.media = id;
        this.selMedia = id;
        this.commit(); this.panels.render();
      } else if (OBJ_EXT.test(f.name)) {
        const path = await this.files.add(uniquePath('models', f.name, taken), f); taken.add(path);
        let s = forSurface && forSurface.shape === 'object3d' ? forSurface : null;
        if (!s) { s = ops.addSurface(this.show, 'object3d', this.defaultMedia()); this.sel.surface = s.id; }
        s.mesh = 'file'; s.meshPath = path; s.meshUrl = this.files.resolve(path, path);
        this.commit(); this.panels.render();
      } else if (/\.maphappy$/i.test(f.name)) {
        await this.openFiles(list); return;
      } else this.status(`"${f.name}": MapHappy takes songs (MP3, WAV), videos (MP4, WebM) and models (OBJ).`);
    }
    this.panels.render();
  }

  // Opens a .maphappy project picked from the computer, with the files it uses (picked along with it).
  async openFiles(list) {
    const proj = list.find(f => /\.maphappy$/i.test(f.name));
    if (!proj) { this.status('Choose a .maphappy project (and the song, videos and models it uses).'); return; }
    let json;
    try { json = JSON.parse(await proj.text()); } catch { this.status("That file isn't a MapHappy project."); return; }
    const byName = new Map(list.map(f => [f.name.toLowerCase(), f]));
    const want = [...(json.media || []).filter(m => m.kind === 'video').map(m => m.pathRel || m.path), ...(json.audioAssets || []).map(a => a.pathRel || a.path),
                  ...(json.surfaces || []).filter(s => s.mesh === 'file').map(s => s.meshPathRel || s.meshPath)].filter(Boolean);
    await this.files.clear();
    for (const p of want) { const f = byName.get(p.split(/[\\/]/).pop().toLowerCase()); if (f) await this.files.add(p.replace(/\\/g, '/'), f); }
    this.clock.buffers.clear(); this.clock.pause();
    this.load(json);
    this.status(`Opened ${proj.name}.`);
  }

  async openPublished() {
    try {
      const r = await fetch(PUBLISHED, { cache: 'no-cache' });
      if (!r.ok) throw new Error(`${r.status}`);
      await this.files.clear(); this.clock.buffers.clear(); this.clock.pause();
      this.load(await r.json());
      this.status('Opened the published show (web/show/show.maphappy).');
    } catch (e) { this.status(`Couldn't open the published show (${e.message}).`); }
  }

  newShow() {
    this.files.clear(); this.clock.buffers.clear(); this.clock.pause(); this.clock.pos = 0;
    const show = emptyShow();
    ops.addSurface(show, 'quad');
    this.load(writeShow(show));
  }

  saveProject() {
    const json = writeShow(this.show);
    download(new Blob([JSON.stringify(json, null, 2)], { type: 'application/json' }), 'show.maphappy');
    this.savedSnapshot = this.snapshot; this.updateTitle();
    this.status('Saved show.maphappy (to your Downloads). To publish the show with its music and media, use "Download for the web".');
  }

  // The show and every file it uses, as a .zip to unpack into the repository's web/ folder.
  async downloadForWeb() {
    this.status('Packing the show…');
    const json = writeShow(this.show), enc = new TextEncoder();
    const paths = new Set([...json.media.filter(m => m.kind === 'video').map(m => m.pathRel || m.path), ...json.audioAssets.map(a => a.path),
                           ...json.surfaces.filter(s => s.mesh === 'file').map(s => s.meshPath)].filter(Boolean));
    const files = [{ name: 'show/show.maphappy', data: enc.encode(JSON.stringify(json, null, 2)) }];
    for (const p of paths) {
      try { files.push({ name: `show/${p}`, data: await this.files.bytes(p) }); }
      catch (e) { this.status(e.message); return; }
    }
    download(makeZip(files), 'maphappy-show.zip');
    this.savedSnapshot = this.snapshot; this.updateTitle();
    this.status('Downloaded maphappy-show.zip: unzip it into your repository\'s web/ folder (replacing web/show), then commit and push to publish.');
  }

  // ---------------------------------------------------------------- live presets
  setLive(on) { this.live = on; $('live').classList.toggle('on', on); if (this.panels.tab === 'presets') this.panels.render(); }
  storePreset(slot) {
    const old = this.show.presets[slot];
    this.show.presets[slot] = capturePreset(this.show, old ? old.name : `Preset ${slot === 9 ? 0 : slot + 1}`, old ? old.fade : 0.5);
    this.commit(); this.status(`Stored the current look on key ${slot === 9 ? 0 : slot + 1}.`);
  }
  playPreset(slot) {
    const pr = this.show.presets[slot];
    if (!pr) { this.status(`Nothing on key ${slot === 9 ? 0 : slot + 1} yet: Shift + ${slot === 9 ? 0 : slot + 1} stores the current look.`); return; }
    this.player.start(this.show, pr, slot);
    if (this.panels.tab === 'presets') this.panels.render();
  }

  // ---------------------------------------------------------------- every frame
  frame(now, last) {
    const dt = Math.min(0.1, (now - last) / 1000);
    this.clock.tick();
    const t = Math.max(0, this.clock.time);
    this.player.update(dt);
    applyTimeline(this.show, t);
    for (const m of this.show.media.values()) m.update(dt);
    this.renderer.renderStage(this.stageRT, this.show);
    if (this.outputs) this.output.postMessage(frameMessage(this.show, t, this.blackout));   // the output window shows exactly this
    this.stageView.draw();
    this.timelineView.draw();
    $('time').textContent = `${Math.floor(t / 60)}:${(t % 60).toFixed(2).padStart(5, '0')}`;
    if (this.shownPlaying !== this.clock.playing) {           // the Play button shows what it will do
      this.shownPlaying = this.clock.playing;
      $('play').replaceChildren(icon(this.clock.playing ? 'pause' : 'play'), this.clock.playing ? 'Pause' : 'Play');
    }
    if (now - (this.lastRefresh || 0) > 120) { this.lastRefresh = now; this.panels.refreshValues(); }
    requestAnimationFrame(n => this.frame(n, now));
  }
}

// ---------------------------------------------------------------------------------------------- start-up
const ed = window.__editor = new Editor();
await ed.files.init();
let start = null;
try { start = JSON.parse(localStorage.getItem(AUTOSAVE) || 'null'); } catch { start = null; }
if (!start) { try { const r = await fetch(PUBLISHED, { cache: 'no-cache' }); if (r.ok) start = await r.json(); } catch { /* no published show */ } }
if (start) { try { ed.load(start); } catch (e) { ed.status(`Couldn't restore the last show (${e.message}); starting a new one.`); start = null; } }
if (!start) ed.newShow();
setInterval(() => { try { localStorage.setItem(AUTOSAVE, ed.snapshot); } catch { /* storage full or blocked */ } }, 2000);
requestAnimationFrame(t => ed.frame(t, t));

// ---- top bar
const on = (id, fn) => $(id).addEventListener('click', fn);
on('new', () => { if (confirmDiscard()) ed.newShow(); });
on('openPublished', () => { if (confirmDiscard()) ed.openPublished(); });
$('openFile').addEventListener('change', e => { const f = [...e.target.files]; e.target.value = ''; if (f.length && confirmDiscard()) ed.openFiles(f); });
on('save', () => ed.saveProject());
on('zip', () => ed.downloadForWeb());
on('play', () => ed.togglePlay());
on('undo', () => ed.undoStep(true));
on('redo', () => ed.undoStep(false));
on('live', () => ed.setLive(!ed.live));
on('present', () => present(true));
on('output', () => ed.openOutput());
on('viewInput', () => { ed.stageView.mode = ed.stageView.mode === 'input' ? 'output' : 'input'; $('viewInput').classList.toggle('on', ed.stageView.mode === 'input'); });
$('snap').addEventListener('change', e => { ed.snap = e.target.checked; });
on('marker', () => { ops.addMarker(ed.show, ed.clock.time); ed.commit(); });
on('split', () => ed.splitSong());
$('outsize').addEventListener('change', e => { const [w, h] = e.target.value.split('x').map(Number); ed.show.outW = w; ed.show.outH = h; ed.commit(); ed.load(JSON.parse(ed.snapshot), { keepMedia: true, resetHistory: false }); });
on('zoomIn', () => ed.stageView.zoomBy(1.25));
on('zoomOut', () => ed.stageView.zoomBy(1 / 1.25));
on('zoomFit', () => ed.stageView.fit());

// ---- the borders between the panels
const LAYOUT = 'maphappy-editor-layout', main = document.querySelector('main');
const layout = (() => { try { return JSON.parse(localStorage.getItem(LAYOUT)) || {}; } catch { return {}; } })();
const sizes = { panel: [340, () => [240, Math.max(260, main.clientWidth - 360)]], timeline: [260, () => [120, Math.max(140, main.clientHeight - 160)]] };
function setSize(which, v, save = true) {
  const [lo, hi] = sizes[which][1]();
  v = Math.round(Math.max(lo, Math.min(hi, v)));
  layout[which] = v;
  main.style.setProperty(which === 'panel' ? '--panel-w' : '--tl-h', `${v}px`);
  if (save) try { localStorage.setItem(LAYOUT, JSON.stringify(layout)); } catch { /* storage blocked */ }
}
for (const [id, which, horizontal] of [['splitV', 'panel', true], ['splitH', 'timeline', false]]) {
  const bar = $(id), cur = () => layout[which] ?? sizes[which][0];
  bar.addEventListener('pointerdown', e => {
    if (e.button !== 0) return;
    e.preventDefault(); try { bar.setPointerCapture(e.pointerId); } catch { /* not a real pointer */ } bar.classList.add('dragging'); document.body.classList.add('resizing');
    const start = horizontal ? e.clientX : e.clientY, from = cur();
    const move = ev => setSize(which, from + (horizontal ? ev.clientX - start : start - ev.clientY), false);
    const up = () => { bar.removeEventListener('pointermove', move); bar.classList.remove('dragging'); document.body.classList.remove('resizing'); setSize(which, cur()); };
    bar.addEventListener('pointermove', move); bar.addEventListener('pointerup', up, { once: true }); bar.addEventListener('lostpointercapture', up, { once: true });
  });
  bar.addEventListener('dblclick', () => setSize(which, sizes[which][0]));
  bar.addEventListener('keydown', e => {                       // the arrow keys move a focused border too
    const d = { ArrowLeft: -1, ArrowRight: 1, ArrowUp: 1, ArrowDown: -1 }[e.key];
    if (d === undefined || (horizontal !== (e.key === 'ArrowLeft' || e.key === 'ArrowRight'))) return;
    setSize(which, cur() + d * (e.shiftKey ? 50 : 10)); e.preventDefault(); e.stopPropagation();
  });
}
for (const which of Object.keys(sizes)) if (layout[which]) setSize(which, layout[which], false);
addEventListener('resize', () => { for (const which of Object.keys(sizes)) if (layout[which]) setSize(which, layout[which], false); });

$('help').addEventListener('click', () => { $('helpbox').hidden = !$('helpbox').hidden; });

// "Discard unsaved changes?" — a small in-page confirmation (no browser pop-ups).
let discardArmed = 0;
function confirmDiscard() {
  if (ed.snapshot === ed.savedSnapshot || performance.now() - discardArmed < 4000) { discardArmed = 0; return true; }
  discardArmed = performance.now();
  ed.status('This show has changes you haven\'t downloaded. Click again within 4 seconds to continue anyway (it is also autosaved in this browser).');
  return false;
}

function present(on) {
  document.body.classList.toggle('present', on);
  ed.stageView.hideHandles = on;
  if (on) document.documentElement.requestFullscreen?.().catch(() => {});
  else if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
}
document.addEventListener('fullscreenchange', () => { if (!document.fullscreenElement && document.body.classList.contains('present')) present(false); });

// ---- keys (never while typing in a box)
const TYPING = 'textarea, select, input:not([type=range]):not([type=checkbox]):not([type=color]):not([type=file])';
addEventListener('keydown', e => {
  if (e.target.closest?.(TYPING)) return;
  const k = e.key.toLowerCase(), del = e.code === 'Delete' || e.code === 'Backspace';
  // a slider or tick box that has the focus keeps its own keys (arrows, Space), but Delete, Undo, Copy... still work
  if (e.target.closest?.('input') && !del && !e.ctrlKey && !e.metaKey) return;
  if ((e.ctrlKey || e.metaKey) && !e.shiftKey && k === 'c') { if (!String(getSelection()) && (ed.hasTimelineSelection() ? ed.copyTimeline() : ed.copySurface())) e.preventDefault(); return; }   // keyframes / a clip if one is selected, else the surface
  if ((e.ctrlKey || e.metaKey) && k === 'v') { if ((e.shiftKey ? ed.pasteTimeline(undefined, undefined, true) : ed.pasteTimeline()) || (!e.shiftKey && ed.pasteSurface())) e.preventDefault(); return; }   // Shift: at the same time as the original
  if ((e.ctrlKey || e.metaKey) && k === 'z') { ed.undoStep(!e.shiftKey); e.preventDefault(); return; }
  if ((e.ctrlKey || e.metaKey) && k === 'y') { ed.undoStep(false); e.preventDefault(); return; }
  if ((e.ctrlKey || e.metaKey) && k === 's') { ed.saveProject(); e.preventDefault(); return; }
  if (e.ctrlKey || e.metaKey || e.altKey) return;
  if (e.code === 'Space') { ed.togglePlay(); e.preventDefault(); }
  else if (e.code === 'Escape') { present(false); }
  else if (e.code === 'Home') ed.seek(0);
  else if (e.code === 'ArrowLeft') ed.seek(ed.clock.time - (e.shiftKey ? 1 : 1 / 30));
  else if (e.code === 'ArrowRight') ed.seek(ed.clock.time + (e.shiftKey ? 1 : 1 / 30));
  else if (k === 'm') { ops.addMarker(ed.show, ed.clock.time); ed.commit(); }
  else if (k === 'b') ed.blackout = !ed.blackout;
  else if (e.key === '+' || e.key === '=') ed.stageView.zoomBy(1.25);
  else if (e.key === '-' || e.key === '_') ed.stageView.zoomBy(1 / 1.25);
  else if (k === 's') ed.splitSong();
  else if (del) { if (!ed.deleteSelected()) return; }
  else if (ed.live && /^(Digit|Numpad)\d$/.test(e.code)) {
    const d = Number(e.code.slice(-1)), slot = d === 0 ? 9 : d - 1;
    if (e.shiftKey) { ed.storePreset(slot); ed.panels.render(); } else ed.playPreset(slot);
  } else return;
  e.preventDefault();
});

// Drop files anywhere: songs, videos, models, or a project.
addEventListener('dragover', e => e.preventDefault());
addEventListener('drop', e => { e.preventDefault(); const f = [...e.dataTransfer.files]; if (f.some(x => /\.maphappy$/i.test(x.name))) { if (confirmDiscard()) ed.openFiles(f); } else ed.addFiles(f, ed.selected()); });
