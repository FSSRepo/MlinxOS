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
- Keyboard: `keydown`/`keyup` events carry the same fields as DOM `KeyboardEvent`: `key` (`"a"`, `"A"`, `"@"`, `"�"`, `"Enter"`, `"ArrowLeft"`, `"F5"`, `"Shift"`...), `code` (`"KeyA"`, `"Digit1"`, `"ShiftLeft"`), `keyCode`, `shiftKey`, `ctrlKey`, `altKey`, `metaKey`, `capsLock` and `repeat`. The whole keyboard works on Weston (xkbcommon, using the layout the compositor sends; key repeat is done by linweb) and on Xorg (`XLookupString`), with shift, caps lock, AltGr, the keypad, function and media keys, and dead keys (acute, grave, circumflex, diaeresis and tilde combine with the next letter). `Escape` no longer closes the application (pages use it for "back" and to cancel dialogs): `Ctrl+Q` does. The layout comes from the kernel command line, `xkb_layout=es` and optionally `xkb_variant=...`, which `make-iso.sh --keyboard es` writes into GRUB (default `us`).
- Wi-Fi: `linweb.wifi` drives `wpa_supplicant` through its control socket and gets the address with `udhcpc` (Buildroot: `wpa_supplicant`, the Intel `iwlwifi` firmware, `wireless-regdb`). `status()` returns `{ available, iface, state, ssid, ip, error }` with `state` one of `OFF`, `DISCONNECTED`, `CONNECTING`, `OBTAINING_IP`, `CONNECTED`, `FAILED`; calling it also drives DHCP, so poll it. `scan()` asks for a scan and `networks()` returns the results (`ssid`, `signal` 0-100, `dbm`, `frequency`, `secure`, `security`, `connected`), strongest first and one per SSID. `connect(ssid, password)` returns `{ ok, error }` at once (an empty password means an open network; 8-63 characters or 64 hex digits otherwise) and the attempt is followed with `status()`; a wrong password or a 45 s timeout ends in `FAILED`. `disconnect()` forgets the network. `enable()` unblocks rfkill, brings the link up and starts `wpa_supplicant`. `linweb.network.addresses()` lists the IPv4 addresses of the machine. WPA-Enterprise is not supported. Passwords are never written to disk or to the command line.
- Web server: `linweb.webserver` starts and stops `steward-httpd` (`examples/http-server`, installed in `/usr/sbin` by `build.sh`): `start(port)` (default 8080), `stop()` and `status()` -> `{ running, port, addresses: [{ iface, ip }] }`. It is stopped when linweb exits.
- PS3 menu: System > Network Settings lists the networks, asks for the password in a dialog (type it on the keyboard; Tab shows it) and shows the network name, `Connected` and the IP in the top-right corner; System > Web Server turns the server on and off and shows its URLs.
- Node Lite: `linweb.node` runs [node-lite](../node-lite) scripts in the background: `available()`, `start(script, args, timeoutMs)` and `eval(code, timeoutMs)` return `{ ok, id, error }`, `poll(id)` returns `{ found, running, timedOut, exitCode, output }` (stdout and stderr together, the last 64 KiB) and `stop(id)` kills the job and forgets it. Script files must live in `/root/node-lite`, `/root/ps3`, `/mnt` or `/tmp`; a job is killed after `timeoutMs` (default one minute); at most four run at once. System > Node Lite runs `fetch` against google.com, fetches a URL you type, or starts the example web server on port 8090, and shows the output as it comes.
- Video: `linweb.video` plays MP4 files (H.264 video, AAC audio) with the decoders copied from `media-reader` (see `external/VIDEO-CODECS.md`): `play(path, { card, loop })`, `pause()`, `resume()`, `stop()`, `seek(seconds)`, `setLoop(loop)` and `status()` -> `{ loaded, playing, paused, ended, loop, audio, position, duration, width, height, fps, dropped, path, error }`. The file must be inside `/mnt`, like the music; one video plays at a time and playing one stops the MP3 player. The sound goes to `aplay` and is the clock the picture follows (the picture follows the system clock when there is no audio track, no `aplay` or no sound card, and `status().audio` says which); a frame that arrives more than 150 ms late is dropped (`dropped`). Seeking starts from the nearest IDR picture, so encode with a keyframe every few seconds (`-g 60`). Output of the hardware mixer is `linweb.audio`'s job (`setVolume`, `setMute`). Set `LINWEB_VIDEO_DEBUG=1` for a log on stderr.
- `<video>` element: a replaced element like `<img>`. It shows the picture of the video being played (`object-fit: contain` inside its box, the box's background fills the bars), takes its size from `width`/`height` (attributes or CSS) or from the picture, and keeps the aspect ratio when only one side is set. `<video src="/mnt/video/a.mp4" autoplay loop>` starts that file when the page loads; otherwise control it with `linweb.video`. There is no `controls` UI, `currentTime` or events: the page draws its own controls.
- PS3 menu: Multimedia > Video Player browses `/mnt` (starting in `/mnt/video`, where `make-iso.sh --video` puts the embedded files) and plays MP4 files full screen with controls that hide after four seconds: Enter pause/resume (or play again at the end), Left/Right seek 10 s (Shift: 60 s), Up/Down volume, M mute, L loop, Esc back.

**Input Handling:**
- Keyboard (keydown/keyup events)
- Mouse (click, hover, cursor tracking)
- Scroll (wheel events)

**Debug Tools:**
- Node tree overlay (`--debug` flag)
- Debug visualizer for inspecting elements

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
│   (QuickJS lives in ../external/quickjs, shared with node-lite)
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
