# Linweb - Lightweight Web Rendering Engine

A lightweight HTML/CSS/JS rendering engine built from scratch with C++17 and OpenGL. This project also includes additional OpenGL demos.

## Projects

### Linweb (Main)
A browser-style engine that parses HTML, CSS, and JavaScript to render web pages using OpenGL.

**Pipeline:**
1. **HTML Parsing** - Tokenizer, element parser, text parser
2. **CSS Parsing** - Tokenizer, selector parser, declaration parser, keyframe parser
3. **Style Engine** - Selector matching, property resolution, transitions, animations
4. **Layout Engine** - Block, inline, flex, grid, positioned layout modes
5. **OpenGL Rendering** - Primitives, text (stb_truetype), images (stb_image), SVG (nanosvg), shaders, textures

**JavaScript Integration (QuickJS):**
- DOM bindings (createElement, appendChild, textContent, innerHTML, classList)
- Style bindings (get/set CSS properties by element or ID)
- Event bindings (addEventListener, dispatchEvent)
- Timer bindings (setTimeout, requestAnimationFrame)
- Console API (console.log, .error, .warn, .info)
- Browser-like globals: `document`, `window`, `console`
- Linweb diagnostics: `linweb.renderer.info()` reports the active OpenGL vendor, renderer, GL/GLSL versions, and a software-rendering flag. `linweb.debug.renderer()` logs that report and `linweb.debug.glErrors()` drains and logs pending GL errors.
- Storage: `linweb.storage` works with block devices on Linux. `list()` returns the disks, their partitions, filesystem (ext2/3/4 or FAT, read from the superblock), label, size and mount point. `mount(dev)` / `unmount(path)` mount under `/mnt/<name>`, and `format(dev, 'ext4' | 'vfat', label, confirmName)` runs `mkfs`; `confirmName` must equal the device name, and mounted, read-only or optical devices are refused. `readDir`, `readFile(path, maxBytes)`, `writeFile`, `makeDir` and `remove` are confined to `/mnt` (symlinks and `..` cannot escape). Every call returns `{ ok, error, ... }`. Formatting blocks the UI until `mkfs` ends.
- Audio: `linweb.audio` plays MP3 files from `/mnt` and drives the mixer through ALSA (`aplay`, `amixer`; Buildroot `alsa-utils`). `cards()` lists sound cards (`index`, `id`, `name`, `playback`); `getVolume(card)` / `setVolume(card, percent)` / `setMute(card, bool)` use the Master or PCM control. `play(path, card, marginMs)` returns `{ ok, error }` right away: a helper process (this executable run with `--play-mp3`) decodes the file frame by frame with OpenMP3 (`external/openmp3`, `src/media/mp3`) and writes it to `aplay` through a pipe that holds only `marginMs` of audio (default 1000), so decoding stays about that far ahead of the sound card instead of loading the whole song. `pause()` / `resume()` / `stop()` act on the helper's process group; `status()` returns `{ playing, paused, path, position, duration, sampleRate, channels, error }` (`duration` is estimated from file size and bitrate; `error` is reported once when the helper dies, for example without a sound device). MPEG-1 Layer III only. Pass `card = -1` for ALSA's default device. The PS3 menu's Multimedia > Music Player browses `/mnt` and plays the MP3 files in a folder one after another.

**Input Handling:**
- Keyboard (keydown/keyup events)
- Mouse (click, hover, cursor tracking)
- Scroll (wheel events)

**Debug Tools:**
- Node tree overlay (`--debug` flag)
- Debug visualizer for inspecting elements

**Tests:**
- `g++ -std=c++17 -Isrc tests/storage_test.cpp src/system/storage.cpp -o /tmp/storage_test && /tmp/storage_test` checks filesystem detection, `mkfs`, device enumeration, and the path confinement (needs `mkfs.ext4` and `mkfs.vfat`).
- `node tests/ps3_storage_test.js` drives the PS3 menu's Storage category with a fake DOM and an in-memory `linweb.storage`.
- `g++ -std=c++17 -Isrc -Isrc/media/mp3 -Iexternal/openmp3 tests/audio_test.cpp src/system/audio.cpp src/system/storage.cpp src/media/mp3/*.cpp external/openmp3/*.cpp -o /tmp/audio_test && /tmp/audio_test [song.mp3]` checks card and mixer parsing and the `/mnt` confinement; with an MP3 it also plays through a fake `aplay` that drains one second of audio per second, to check the player keeps decoding in step with playback, pauses, resumes and stops.
- `g++ -std=c++17 -Isrc/media/mp3 -Iexternal/openmp3 tests/mp3_stream_test.cpp src/media/mp3/mp3_stream.cpp src/media/mp3/decode.cpp external/openmp3/*.cpp -o /tmp/mp3_stream_test && /tmp/mp3_stream_test song.mp3 [out.raw]` decodes a file frame by frame and optionally writes raw S16_LE PCM.
- `node tests/ps3_music_test.js` drives the Music Player with a fake DOM and in-memory `linweb.audio`.

### ogl-3d
A 3D OpenGL demo with:
- Free camera movement (WASD + mouse look)
- Phong lighting (directional + point lights)
- Multiple 3D objects with rotation and color

### sci-fi_hud
A sci-fi HUD shader effect rendering animated rings and hotspots with configurable parameters.

## Build Instructions

### Prerequisites
- CMake 3.10+
- C++17 compatible compiler (MSVC)
- GLFW 3.x
- OpenGL

### Build
```bash
cmake -B build
cmake --build build --config Release
```

### Run
```bash
build\Release\linweb.exe assets/pages/index.html
```

### Debug Mode
```bash
build\Release\linweb.exe --debug assets/pages/index.html
build\Release\linweb.exe --debug=element assets/pages/index.html
build\Release\linweb.exe --debug=text assets/pages/index.html
```

## Project Structure

```
├── assets/
│   ├── fonts/            # TrueType font files
│   └── pages/            # HTML/CSS/JS test pages
├── external/
│   ├── glad/             # OpenGL loader
│   ├── nanosvg/          # SVG parser
│   ├── quickjs/          # JavaScript engine
│   └── stb/              # stb_image, stb_truetype
├── ps3/                  # PS3 WebOS-style demo app
├── src/
│   ├── core/             # DOM tree, navigator, helpers
│   ├── debug/            # Node tree view, debug visualizer
│   ├── layout/           # Block, inline, flex, grid, positioned
│   ├── parser/
│   │   ├── css/          # CSS tokenizer & parsers
│   │   └── html/         # HTML tokenizer & parsers
│   ├── platform/         # Window, input handlers
│   ├── renderer/         # Shaders, textures, fonts, SVG, primitives
│   ├── scripting/        # JS engine, DOM/style/event/timer bindings
│   ├── style/            # Style engine, transitions, animations
│   └── utils/            # File I/O, traversal, scroll manager
├── CMakeLists.txt
└── run.bat               # Quick test runner
```

## Supported CSS Features

- Selectors: element, class, ID, descendant, child, pseudo-classes (:hover, :focus, :active)
- Box model: margin, padding, border (width, color, style, radius)
- Display: block, inline, inline-block, flex, grid, none
- Flexbox: direction, wrap, justify-content, align-items, gap
- Grid: template columns/rows, gap, column/row spanning
- Positioning: static, relative, absolute, fixed (with top/left/right/bottom)
- Typography: font-family, font-size, font-weight, text-align, color
- Background: color, image
- Transitions: property, duration, timing-function, delay
- Animations: keyframes, name, duration, iteration-count, direction
- Transforms: translate, rotate, scale (2D)
- CSS filters: blur, invert, drop-shadow
- Overflow: visible, hidden, scroll, auto
- Z-index stacking
- Margin collapse
- Opacity
- Gradients (linear, radial)
- Inline SVG rendering
