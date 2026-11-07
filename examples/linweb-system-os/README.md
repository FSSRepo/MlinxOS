# linweb-system-os

The OS module of MLinxOS on top of the generic Linweb engine. The engine renders HTML, CSS and JavaScript; this module
adds everything the operating system needs: storage, network and Wi-Fi, node-lite, the installer, power, system
information, the gamepad, the `linweb.*` JavaScript object and the shader backgrounds of the PS3 demo.

## Folder layout

```
steward-os/examples/linweb-system-os/
  CMakeLists.txt  build.sh  sync-engine.sh  README.md
  src/main.cpp                    registers SystemOsModule, then linweb::run(argc, argv)
  src/system_os_module.h/.cpp     SystemOsModule : linweb::Module (the engine's hooks)
  src/natives.h                   register_system_natives(linweb::JSEngine&)
  src/gamepad_hook.h              poll_gamepad(linweb::KeyboardHandler&)
  src/ps3_backgrounds.h           register_ps3_backgrounds()
  src/system/*.cpp,h              storage, network, nodelite, install, power, process, sysinfo, input, gamepad
  src/scripting/*_bindings.cpp,h  the JavaScript bindings of those objects (plus perf and native)
  src/gamepad_input.cpp,h         the gamepad reader
  ps3/                            the PS3-style demo (HTML/CSS/JS)
  ../linweb-engine/               a copy of the generic engine, made by sync-engine.sh (never edited here)
```

## Build on the Buildroot VM

```bash
bash sync-engine.sh
PLATFORM=weston bash build.sh ~/Desktop/system-base
```

`build.sh` installs the executable as `/usr/bin/linweb`, builds `steward-httpd` and copies the demo and its fonts to
`output/target/root/ps3`. `PLATFORM` is `xorg` or `weston`; the build directory is `build-<platform>`.

## Updating the engine

Run `sync-engine.sh` again (it copies `../../../Linweb-web-engine`, or the path given as its first argument) and rebuild.
The copied commit is recorded in `../linweb-engine/ENGINE_VERSION`; a dirty engine repository is marked `dirty`.

## How the module hooks in

`SystemOsModule` derives from `linweb::Module` and the engine calls its hooks:

- `install_js`: adds the `linweb.*` natives and registers the PS3 shader backgrounds.
- `on_page_loaded`: lets media elements read the files next to the page.
- `on_frame`: polls the gamepad into key events and passes the music levels to the ring background.
- `wants_frame`: keeps drawing while a stick moves or an overlay animates.
- `on_shutdown`: stops the web server and the node-lite jobs.

The frozen interface is documented in `../../docs/linweb-modules/tasks/_api.md`.

## The engine is read-only here

The engine folder (`../linweb-engine`) is a copy made by `sync-engine.sh`. Do not edit it: change the engine in its own
repository (`Linweb-web-engine`) and run the script again.
