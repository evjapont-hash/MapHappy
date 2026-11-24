"""Writes the demo show in web/show/: a MapHappy project (show.maphappy) and a synthesized beat (music/demo-beat.wav).

The project is an ordinary MapHappy file: open it in the desktop app to change it, save it, and the web player
plays the new version. Run:  python web/tools/make-demo-show.py
"""
import json, math, os, random, struct, wave

HERE = os.path.dirname(os.path.abspath(__file__))
SHOW = os.path.join(HERE, '..', 'show')
BPM, BEATS = 120, 64
BEAT = 60.0 / BPM
LENGTH = BEATS * BEAT                       # 32 seconds


def write_beat(path):
    """A simple 120 BPM groove: kick on every beat, clap on 2 and 4, off-beat hats, a bass note per bar."""
    sr, n = 22050, int(LENGTH * 22050) + 1
    x = [0.0] * n
    rnd = random.Random(7)

    def add(t, samples, g):
        i = int(t * sr)
        for k, s in enumerate(samples):
            if 0 <= i + k < n: x[i + k] += g * s
    kick = [math.sin(2 * math.pi * (50 + 90 * math.exp(-k / sr * 35)) * k / sr) * math.exp(-k / sr * 12) for k in range(sr // 4)]
    clap = [rnd.uniform(-1, 1) * math.exp(-k / sr * 30) for k in range(sr // 6)]
    hat = [rnd.uniform(-1, 1) * math.exp(-k / sr * 90) * 0.4 for k in range(sr // 20)]
    for b in range(BEATS):
        t = b * BEAT
        add(t, kick, 0.9)
        if b % 2 == 1: add(t, clap, 0.35)
        add(t + BEAT / 2, hat, 1.0)
        if b % 4 == 0:
            f = [55.0, 65.4, 49.0, 73.4][(b // 4) % 4]
            add(t, [0.18 * math.sin(2 * math.pi * f * k / sr) * min(1, k / 200) * math.exp(-k / sr * 0.8) for k in range(int(sr * BEAT * 4))], 1.0)
    peak = max(1e-9, max(abs(v) for v in x))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(b''.join(struct.pack('<h', int(v / peak * 0.85 * 32767)) for v in x))


def quad(x0, y0, x1, y1): return [[x0, y0], [x1, y0], [x1, y1], [x0, y1]]


def track(tid, path, clips):
    """clips: list of (start, duration, keys); keys: list of (t, v, curve)."""
    datas, cl = [], []
    for i, (start, dur, keys) in enumerate(clips):
        datas.append({'keys': [{'t': t, 'v': v, 'curve': c} for t, v, c in keys]})
        cl.append({'id': tid * 100 + i + 1, 'name': f'Clip-{tid}-{i + 1}', 'start': start, 'duration': dur, 'ghost': False, 'data': i})
    return {'id': tid, 'kind': 'param', 'name': '', 'path': path, 'muted': False, 'clipData': datas, 'clips': cl}


def main():
    write_beat(os.path.join(SHOW, 'music', 'demo-beat.wav'))
    cube, back, text = 1, 2, 3
    media = [
        {'id': 1, 'name': 'Test Pattern', 'playing': True, 'loop': True, 'kind': 'generator', 'type': 'pattern'},
        {'id': 2, 'name': 'Gradient', 'playing': True, 'loop': True, 'kind': 'generator', 'type': 'gradient', 'width': 1920, 'height': 1080,
         'params': {'colorA': [0.02, 0.03, 0.15, 1], 'colorB': [0.35, 0.05, 0.4, 1], 'style': 'radial', 'angle': 0, 'spin': 0, 'repeat': 2, 'scroll': 0.15}},
        {'id': 3, 'name': 'Lines', 'playing': True, 'loop': True, 'kind': 'generator', 'type': 'lines', 'width': 1024, 'height': 1024,
         'params': {'count': 10, 'thickness': 0.35, 'softness': 0.2, 'angle': 45, 'speed': 1.0, 'color': [1, 1, 1, 1], 'background': [0.1, 0.45, 0.6, 1]}},
        {'id': 4, 'name': 'Title', 'playing': True, 'loop': True, 'kind': 'generator', 'type': 'text', 'width': 1920, 'height': 200,
         'params': {'text': 'GRAPHICS  BLINK', 'font': 'sans', 'align': 'center', 'size': 0.6, 'posY': 0.5, 'color': [1, 1, 1, 1], 'background': [0, 0, 0, 0], 'scroll': 0}},
    ]
    base = {'flipH': False, 'flipV': False, 'blend': 'over', 'folder': 0}
    surfaces = [
        {**base, 'id': cube, 'name': 'Cube', 'shape': 'object3d', 'media': 3, 'in': quad(0, 0, 1, 1), 'out': quad(0.3, 0.12, 0.7, 0.83),
         'mesh': 'cube', 'rotX': -20, 'rotY': 30, 'objScale': 1, 'fov': 40, 'shading': 1, 'lightModel': 2, 'ambient': 0.12, 'kd': 1, 'ks': 0.8, 'shininess': 48,
         'dirIntensity': 0.7, 'dirAzimuth': -40, 'dirElevation': 35, 'pointIntensity': 1.6, 'pointX': 2, 'pointY': 0.5, 'pointZ': 2, 'pointRange': 3,
         'pointR': 1, 'pointG': 0.35, 'pointB': 0.2, 'spotIntensity': 0, 'spotX': -2, 'spotY': 2.5, 'spotZ': 3, 'spotAngle': 22, 'spotSoftness': 0.4,
         'spotR': 0.3, 'spotG': 0.8, 'spotB': 1},
        {**base, 'id': back, 'name': 'Backdrop', 'shape': 'quad', 'media': 2, 'in': quad(0, 0, 1, 1), 'out': quad(0, 0, 1, 1), 'opacity': 1},
        {**base, 'id': text, 'name': 'Title', 'shape': 'quad', 'media': 4, 'in': quad(0, 0, 1, 1), 'out': quad(0.1, 0.86, 0.9, 0.97), 'blend': 'add', 'opacity': 0.0},
    ]
    surfaces.sort(key=lambda s: [cube, text, back].index(s['id']))       # front-most first: cube, title, backdrop
    B = BEAT
    tracks = [
        track(1, f'surface/{cube}/rotY', [(0, LENGTH, [(0, 30, 'linear'), (8 * B, 390, 'linear')])]),            # a full turn every 8 beats
        track(2, f'surface/{cube}/pointX', [(0, LENGTH, [(0, 2.5, 'smooth'), (4 * B, -2.5, 'smooth'), (8 * B, 2.5, 'smooth')])]),   # the point light swings
        track(3, f'surface/{cube}/lightModel', [(0, LENGTH, [(0, 2, 'hold'), (16 * B, 1, 'hold'), (32 * B, 3, 'hold'), (48 * B, 2, 'hold'), (LENGTH, 2, 'hold')])]),
        track(4, f'surface/{cube}/spotIntensity', [(16 * B, 32 * B, [(0, 0, 'linear'), (2 * B, 3, 'linear'), (30 * B, 3, 'linear'), (32 * B, 0, 'linear')])]),
        track(5, f'surface/{cube}/dirIntensity', [(0, LENGTH, [(0, 1.4, 'linear'), (0.5 * B, 0.4, 'linear'), (B, 1.4, 'linear')])]),     # pulses on every beat
        track(6, f'surface/{text}/opacity', [(0, LENGTH, [(0, 0, 'smooth'), (4 * B, 0, 'smooth'), (6 * B, 1, 'smooth'), (60 * B, 1, 'smooth'), (64 * B, 0, 'smooth')])]),
        {'id': 7, 'kind': 'audio', 'name': 'Audio-1', 'path': 'audio/1', 'muted': False,
         'audioClips': [{'id': 900, 'name': 'demo-beat', 'asset': 1, 'start': 0, 'duration': LENGTH, 'offset': 0, 'gain': 1}]},
    ]

    def look(slot, name, fade, values):
        return {'slot': slot, 'key': (slot + 1) % 10, 'name': name, 'fade': fade,
                'surfaces': [{'id': cube, 'media': 3, 'values': values}], 'folders': []}
    presets = [
        look(0, 'Warm', 0.5, {'pointR': 1, 'pointG': 0.35, 'pointB': 0.2, 'ambient': 0.12, 'hue': 0, 'ks': 0.8}),
        look(1, 'Ice', 0.8, {'pointR': 0.2, 'pointG': 0.6, 'pointB': 1, 'ambient': 0.25, 'hue': 160, 'ks': 1.5}),
        look(2, 'Spotlight', 0.3, {'spotIntensity': 4, 'dirIntensity': 0, 'pointIntensity': 0, 'ambient': 0.02}),
        look(9, 'Dark', 1.5, {'opacity': 0}),
    ]
    project = {
        'format': 'maphappy', 'version': 1, 'output': {'width': 1920, 'height': 1080},
        'media': media, 'surfaces': surfaces, 'folders': [], 'presets': presets,
        'audioAssets': [{'id': 1, 'name': 'demo-beat.wav', 'bpm': BPM, 'beatOffset': 0, 'path': 'music/demo-beat.wav', 'pathRel': 'music/demo-beat.wav'}],
        'timeline': {'length': LENGTH, 'tracks': tracks, 'markers': [], 'counters': {'track': 8, 'clip': 1000, 'marker': 1, 'audioTrack': 1}},
        'transport': {'avOffset': 0, 'loop': True},
        'counters': {'surface': 4, 'asset': 2, 'folder': 1, 'shape': [1, 0, 0, 0, 0, 1]},
    }
    with open(os.path.join(SHOW, 'show.maphappy'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(project, f, indent=2)
    print('wrote', os.path.normpath(SHOW))


if __name__ == '__main__':
    main()
