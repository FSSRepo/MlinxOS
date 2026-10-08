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
