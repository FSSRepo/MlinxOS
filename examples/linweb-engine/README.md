# Linweb - Lightweight Web Rendering Engine

A lightweight HTML/CSS/JS rendering engine built from scratch with C++17 and OpenGL.

## Pipeline

1. **HTML Parsing** - Tokenizer, element parser, text parser
2. **CSS Parsing** - Tokenizer, selector parser, declaration parser, keyframe parser
3. **Style Engine** - Selector matching, property resolution, transitions, animations
4. **Layout Engine** - Block, inline, flex, grid, positioned layout modes
5. **OpenGL Rendering** - Primitives, text (stb_truetype), images (stb_image), SVG (nanosvg), shaders, textures

## JavaScript Integration (QuickJS)

### DOM API
- `document.getElementById(id)`
- `document.getElementsByClassName(name)`
- `document.querySelector(selector)`
- `document.querySelectorAll(selector)`
- `document.createElement(tagName)`
- `document.body`
- `document.documentElement`
- `element.textContent` / `element.innerText`
- `element.innerHTML`
- `element.className`
- `element.classList.add(...)` / `.remove(...)` / `.toggle(cls, force)`
- `element.getAttribute(name)`
- `element.appendChild(child)`
- `element.querySelector(selector)` / `element.querySelectorAll(selector)`

### Style API
- `element.style.backgroundColor`
- `element.style.color`
- `element.style.display`
- `element.style.opacity`
- `element.style.transform`

### Event API
- `element.addEventListener(type, callback)`
- `document.addEventListener(type, callback)`
- `window.addEventListener(type, callback)`
- `window.dispatchEvent(targetId, type, data)`

### Timer API
- `setTimeout(callback, delay)`
- `requestAnimationFrame(callback)`

### Console API
- `console.log(...args)`
- `console.error(...args)`
- `console.warn(...args)`
- `console.info(...args)`

### Browser-like Globals
- `document`
- `window`
- `console`

## Input Handling

- **Keyboard** - keydown/keyup events
- **Mouse** - click, hover, cursor tracking
- **Scroll** - wheel events, scrollable containers

## Debug Tools

- Node tree overlay (`--debug` or `--debug=element`)
- Text rendering debug (`--debug=text`)
- Dimension debug (`--debug=dimens`)
- Animation debug (`--debug=anim`)

## Build Instructions

### Prerequisites
- CMake 3.10+
- C++17 compatible compiler (MSVC, GCC, Clang)
- OpenGL
- One of the following backends:
  - **GLFW** (default) - `glfw3`
  - **Wayland/Weston** - `wayland-client`, `wayland-egl`, `EGL`, `GLESv2`
  - **X11/EGL** - `X11`, `EGL`, `OpenGL`

### Build
```bash
cmake -B build
cmake --build build --config Release
```

### Platform Options
```bash
# Wayland/Weston
cmake -B build -DWESTON_PLATFORM=ON

# X11/EGL
cmake -B build -DDXORG_PLATFORM=ON
```

### Run
```bash
# Default page
build\Release\linweb.exe

# Specific HTML file
build\Release\linweb.exe examples/index.html

# With debug overlay
build\Release\linweb.exe --debug examples/index.html
build\Release\linweb.exe --debug=dimens examples/index.html
build\Release\linweb.exe --debug=anim examples/index.html
```

### Pages from a URL

`linweb.exe https://example.com/` loads a page from the network. The HTML and its
static resources (stylesheets, images, CSS images and fonts, `srcset` candidates and
CSS imports) are downloaded into memory, not into a folder: nothing is written to disk.
Resources that are streamed when they play (`<audio>`, `<video>`, media files) keep their
address. JavaScript is not inspected for resources, so pictures a script creates later
are fetched when they are used (`new Image().src = "https://..."` works).

HTTPS uses the bundled **mbedTLS 3.6.4** and the Mozilla CA bundle in
`external/ca/cacert.pem`. Certificates and hostnames are verified; no system TLS
installation is needed. The CA bundle is embedded during CMake configuration:
replace `external/ca/cacert.pem` and rebuild to update the trusted roots. Keep the
clock correct for certificate expiration checks. The mbedTLS license is preserved in
`external/mbedtls/LICENSE`; the CA provenance is in the PEM header. To test a host
that is not in DNS, set `LINWEB_RESOLVE=host=ip`; to trust another certificate,
set `LINWEB_CA_FILE=<file>`.

Relative URLs, `<base href>`, HTTP redirects, binary bodies, `Content-Length` and
chunked transfer encoding are supported. It does not follow page links or iframes,
resolve JavaScript module imports or maintain cookies. The client requests
uncompressed responses (`Accept-Encoding: identity`); servers that insist on
gzip/Brotli return an explicit error.

Limits: 10 redirects per request, 64 MiB per response, 256 MiB per page, 512 static
resources and 16 dependency levels. Reads have a 15-second inactivity timeout;
DNS/connect use OS timeouts.

### Static rendering and visual checks

Render a page without running its JavaScript:

```powershell
.\build\Release\linweb.exe --static examples/index.html
```

Fonts are copied beside `linweb.exe` during the build. Text rendering accepts
UTF-8 and legacy Latin-1 bytes and includes Latin-1 glyphs (Spanish accents,
`ñ`, and `©`); characters outside this atlas currently use a fallback glyph.
The static rendering path supports basic HTML tables, input button labels,
local CSS imports and image backgrounds, attribute selectors, CSS variable
fallbacks, specificity/`!important`, and screen media queries for min/max viewport
width/height. Unknown CSS at-rules are skipped without blocking the parser.
Web fonts, full table layout (including cell spans), and complete browser CSS
compatibility remain outside this rendering subset.

Capture one frame as a BMP and exit. On Windows/GLFW this uses a hidden window
and automatically enables static mode. The optional layout dump records box
positions and computed dimensions as TSV:

```powershell
.\build\Release\linweb.exe --screenshot=render.bmp --layout-dump=layout.tsv examples/index.html
```

The full build includes parser and pixel/geometry checks in addition to the
download tests. `render-static` requires a working OpenGL context:

```powershell
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

## Project Structure

```
├── assets/
│   └── fonts/            # TrueType font files (Arial, Monospace)
├── examples/
│   ├── ps3/              # PS3 WebOS-style demo app
│   ├── tests/            # HTML/CSS feature tests
│   ├── index.html        # CERN-inspired demo page
│   ├── new_features.html # Typography, scroll, margin collapse demo
│   └── ...               # Various feature demos
├── external/
│   ├── glad/             # OpenGL loader
│   ├── nanosvg/          # SVG parser
│   ├── quickjs/          # JavaScript engine
│   └── stb/              # stb_image, stb_truetype
├── src/
│   ├── core/             # DOM tree, navigator, helpers
│   ├── debug/            # Node tree view, debug visualizer
│   ├── layout/           # Block, inline, flex, grid, positioned
│   ├── parser/
│   │   ├── css/          # CSS tokenizer & parsers
│   │   └── html/         # HTML tokenizer & parsers
│   ├── platform/         # Window abstraction (GLFW, Weston, X11)
│   ├── renderer/         # Shaders, textures, fonts, SVG, primitives
│   ├── scripting/        # JS engine, DOM/style/event/timer bindings
│   ├── style/            # Style engine, transitions, animations
│   └── utils/            # File I/O, traversal, scroll manager
├── CMakeLists.txt
└── README.md
```

## Supported HTML Features

- Elements: `div`, `span`, `p`, `h1`-`h3`, `a`, `img`, `button`, `input`, `header`, `footer`, `nav`, `section`, `main`, `aside`, `article`, `dl`, `dt`, `dd`, `ul`, `ol`, `li`, `strong`, `em`, `code`, `br`, `style`, `script`, `link`
- Inline elements with proper whitespace handling
- Default browser-like styles for tags
- External and inline CSS
- External and inline JavaScript
- Resource path resolution for images and scripts

## Supported CSS Features

- **Selectors**: element, class, ID, descendant, child, pseudo-classes (`:hover`, `:focus`, `:active`)
- **Box model**: margin, padding, border (width, color, style, radius)
- **Display**: `block`, `inline`, `inline-block`, `flex`, `grid`, `none`
- **Flexbox**: direction, wrap, justify-content, align-items, gap
- **Grid**: template columns/rows, gap, column/row spanning
- **Positioning**: `static`, `relative`, `absolute`, `fixed` (with top/left/right/bottom)
- **Typography**: font-family, font-size, font-weight, text-align, color, line-height
- **Background**: color, image
- **Transitions**: property, duration, timing-function, delay
- **Animations**: keyframes, name, duration, iteration-count, direction
- **Transforms**: translate, rotate, scale (2D)
- **CSS Filters**: blur, invert, drop-shadow
- **Overflow**: visible, hidden, scroll, auto
- **Z-index** stacking
- **Margin collapse**
- **Opacity**
- **Gradients**: linear, radial
- **Inline SVG** rendering
- **Box shadow**

## Performance

On startup, the engine prints a performance breakdown of each pipeline stage:
- HTML Parsing (ms + node count)
- Resources Loading (ms + style count)
- CSS Parsing (ms + rule count)
- Style Tree (ms + styled node count)
- Layout Tree (ms + box count)
