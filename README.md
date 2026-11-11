# MapHappy

A free, open-source **projection mapping** tool written in C++ and OpenGL. Map video and generated
visuals onto real surfaces (walls, boxes, shapes), then choreograph them to a song on a keyframe
timeline: no live audio-reactivity needed, every change lands exactly where you put it.

It was built to make rhythmic, music-timed projection shows cheap and approachable. The workflow
(surfaces, a media pool, a conductor timeline) will feel familiar if you have used a commercial
projection mapper, but MapHappy is an **independent implementation**: it contains no code, assets or
artwork from any commercial product and is not affiliated with one.

> Status: working and in active development. Developed and tested on Windows. The code is written
> to be cross-platform (CMake, GLFW, OpenGL 3.3 core, miniaudio) but has not been built on macOS yet.

## Features

**Mapping**
- Any number of surfaces: quads, circles and triangles, each with an *input* region (what part of the
  media it shows) and an *output* quad (where it lands on the stage).
- Perspective-correct corner-pin warping (a homography solved per surface and evaluated per pixel on
  the GPU, so grids stay straight, unlike splitting a quad into two triangles).
- Anti-aliased edges, soft feathering, flips, opacity and **blend modes** (Over, Add, Multiply, Screen).
- A shared **media pool**: video files (FFmpeg) and a built-in test pattern; many surfaces can share
  one video, which is decoded once.
<img width="1277" height="554" alt="Screenshot 2026-11-10 150749" src="https://github.com/user-attachments/assets/8c3da450-5f45-4092-99c9-b2f6f4c96551" />

**Animation**
- A **Conductor** timeline with tracks, clips and keyframes (Linear / Smooth / Hold curves).
- Clips loop when stretched, and can be duplicated or **ghosted** (a ghost shares its keyframes with
  the original, so editing one edits all).
- 20 animatable parameters per surface: opacity, visibility, feather; position, rotation and scale
  (applied on top of the mapped corners so your alignment is never disturbed); content scroll, zoom
  and rotation (the picture inside the shape); tint, hue, saturation, contrast and brightness.
- Deterministic evaluation: the same time always produces the same picture, however you got there.

**Music**
- Drop in a WAV, MP3 or FLAC: waveform display, background decoding, and automatic **tempo and
  beat-grid detection** with a visible beat grid.
- The **audio clock is the master clock**, so picture stays locked to the sound. A sound/picture
  offset control compensates for output latency.
- Snapping to beats, markers, clip edges and the playhead. Tap **M** while the music plays to drop
  markers. Split and trim audio clips.
<img width="1035" height="378" alt="Screenshot 2026-10-27 213552" src="https://github.com/user-attachments/assets/2a9f3372-be73-44c3-8116-08ba42657b70" />

**Show control**
- A **projector output window**: borderless and sized exactly to a chosen display, sharing the
  editor's GPU textures (no copying). Fit or Stretch scaling, selectable resolution, a blackout
  panic key, and clean handling of a display being unplugged mid-show.
- **Projects** saved as readable JSON (git-friendly), with relative media paths so a project folder
  can be moved or shared, atomic saves, autosave and crash recovery, and an unsaved-changes prompt.
- Missing media never blocks opening a project: it becomes a placeholder you can relink.

## Quick start

1. Add a surface (**Surfaces** tab, "+ Quad"), then drag its corners in the **Stage** view to line it
   up with your real object. The **Input** view chooses which part of the picture it shows.
2. Drag a video or a song onto the window (or use **File > Add media**).
3. In the Conductor, click **+ Track** and pick a surface and a parameter, or right-click any
   slider in the inspector and choose **Animate in the Conductor**.
4. Right-click a lane to create a clip, double-click inside it to add keyframes, press **Space** to play.
5. **File > Save** (`Ctrl+S`). Press **F11** to open the projector output.

### Shortcuts

| Key | Action |
|---|---|
| Space | Play / pause |
| M | Drop a marker at the playhead |
| B | Blackout the projector output |
| F11 | Show / hide the projector output |
| Ctrl+N / O / S, Ctrl+Shift+S | New / Open / Save / Save As |
| Delete | Delete the selected keyframe or clip |
| Ctrl + wheel, Shift + wheel | Zoom / scroll the timeline |
| Esc (in the output window) | Close the output window |

## Building

Requirements: a C++17 compiler, CMake 3.20+, Git, and [vcpkg](https://github.com/microsoft/vcpkg)
for FFmpeg. Everything else is fetched by CMake.

**Windows (Visual Studio, "Desktop development with C++")**

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat

git clone https://github.com/evjapont-hash/MapHappy.git
cd MapHappy
cmake -S . -B build "-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
.\build\Release\MapHappy.exe
```

The first configure builds FFmpeg through vcpkg (10 to 20 minutes, once).

**macOS / Linux** use the same CMake commands with your vcpkg path. This is untested so far, so expect
to fix small things.

### Dependencies

| Library | Used for | License |
|---|---|---|
| [GLFW](https://www.glfw.org/) | windows, input | zlib |
| [GLAD](https://github.com/Dav1dde/glad) | OpenGL loader | MIT |
| [Dear ImGui](https://github.com/ocornut/imgui) | user interface | MIT |
| [FFmpeg](https://ffmpeg.org/) (via vcpkg) | video decoding | LGPL (dynamically linked) |
| [miniaudio](https://miniaud.io/) | audio decoding and output | public domain / MIT-0 |
| [nlohmann/json](https://github.com/nlohmann/json) | project files | MIT |

## How it works

A few of the more interesting pieces, for anyone reading the code:

- **Warping** (`Homography.h`, `Renderer.h`): each surface stores two quads. A closed-form
  homography maps the unit square to a quad. The fragment shader works backwards per pixel: stage
  pixel, then (inverse output homography) surface coordinates, then (input homography) picture
  coordinates, so perspective is exact. Edge coverage is derived from the distance to the shape's
  edge for anti-aliasing and feathering.
- **Transforms** (`Surface.h`): animation moves the *effective* quads, never the mapped corners.
  Rotation is done in pixel proportions, so a square stays square on a 16:9 stage.
- **One parameter table** (`Params.h`): every animatable value is a row (key, label, range, default,
  format). The timeline, the inspector, the project file and the Add Track menu all read it.
- **Timeline engine** (`Timeline.h`): no UI or OpenGL dependencies. Tracks hold clips; clips hold
  shared keyframe data (which is how ghost clips work). Values hold between clips.
- **Audio** (`AudioMixer.h`, `AudioSync.h`): the mixer runs on the audio thread and its frame
  counter *is* the show's clock; the UI reads it. Clip lists are handed over as immutable snapshots.
- **Beat detection** (`BeatDetect.h`): multi-band spectral flux, then a tempo estimate combining
  autocorrelation with the Fourier magnitude of the onset curve, then a comb-filter search for phase.
- **Video** (`VideoDecoder.cpp`): FFmpeg decoding on a worker thread into a small frame queue,
  converted to RGBA and flipped for OpenGL, looping with monotonic timestamps.
- **Output window** (`OutputWindow.h`, `OutputController.h`): a second GLFW window created with a
  *shared* OpenGL context, so it samples the stage texture directly.
- **Safety** (`ProjectIO.cpp`): a project is fully read and validated before it replaces the open
  one; damaged values are repaired with warnings instead of trusted.

### Source layout

```
src/
  main.cpp                 app loop
  Editor.h                 menus, panels, dialogs, inspector
  ConductorPanel.h         timeline UI
  Timeline.h  Params.h     timeline engine, animatable parameter table
  Surface.h  Project.h     data model
  Renderer.h  GLUtil.h     stage rendering
  MediaPool.h  VideoDecoder.*  VideoSource.h  PatternSource.h  SolidSource.h
  Audio*.h  Audio.cpp  BeatDetect.h
  ProjectIO.*  FileBrowser.h  PathUtil.h  PlatformFiles.cpp
  OutputState.h  OutputWindow.h  OutputController.h
```

## Roadmap

- Cues: short triggerable timelines and a Timeline Player track to fire them from the Conductor
- Mesh warp, masks and edge blending for multi-projector setups
- Undo / redo
- Audio-reactive parameters as an extra track type
- Automated tests and continuous integration in this repository; macOS build verification

## Known limitations

- One projector output window showing the whole stage (no per-output splitting yet).
- Beat detection can pick half or double the true tempo on some music; the inspector has x2 and /2.
- Video only (no live inputs such as NDI, Syphon or Spout yet).
- No undo yet.
