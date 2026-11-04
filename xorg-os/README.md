# xorg-os

Minimal system based on **Buildroot 2025.11**. It boots X.Org with Mesa3D, including
llvmpipe software rendering, GLX/EGL/OpenGL ES, and the virtio GPU where available. Weston is
not selected in this profile.

See the [main README](../README.md) for host requirements and dependencies.

## Layer contents

```text
xorg-os/
  configs/mlinxos_defconfig       Buildroot configuration for X.Org and Mesa
  mlinxos_config/                 Kernel config, post-build hook, patches
  init.sh                         Select the defconfig and optionally open menuconfig
  build.sh                        Build, install Xorg startup, and prepare ISO files
  automate.sh                     Install an app and configure Xorg autostart
  make-iso.sh                     Build the initramfs and ISO; optionally run QEMU
```

## Ordered build and ISO flow

The recommended path reuses the existing Wayland Buildroot tree on the build server. It keeps
`output/` and downloaded sources, overlays the X.Org configuration, then lets Buildroot rebuild
packages affected by the profile change. This avoids starting from a fresh Buildroot extraction;
X.Org, Mesa, or other packages whose options differ can still be rebuilt.

Run from the repository root, setting the path to the existing Buildroot tree. Here the tree is
`../wayland-os`; change that path if it lives elsewhere.

```bash
cd /path/to/steward-os
SRC="$PWD"
bash ./init-buildroot.sh --project xorg-os --reuse-tree ../wayland-os --skip-deps --non-interactive
```

Configure the X.Org defconfig and build. The init script applies checked-in defaults without
opening the interactive menu; use `--menuconfig` only when you intend to change them.
It also adjusts Buildroot's Mesa Kconfig so Iris keeps LLVM and Mesa's SPIR-V precompilation
tools without enabling OpenCL. This prevents target Clang and libclc from being included just
because Iris needs the precompilation path; the Iris OpenGL driver and LLVM remain enabled.

```bash
cd ../wayland-os
bash init.sh --no-menuconfig
bash build.sh --refresh-mesa --jobs "$(nproc)" --no-iso
```

`build.sh` compiles the selected Buildroot configuration, verifies the kernel and initramfs,
copies the kernel into the ISO tree, and relies on Buildroot's `S40xorg` service to start the
server. It removes the old Weston service/config from a reused target tree. Use
`--refresh-mesa` on the first switch from an already-built Wayland tree: it rebuilds Mesa with
the X.Org GLX settings in staging while preserving the Buildroot toolchain, LLVM and other
packages. The build script also removes stale Weston and Wayland runtime files left in the
reused target directory. Normal subsequent build runs can omit `--refresh-mesa`.

Cross-compile the X11/EGL examples for the Buildroot sysroot, install the cube, and generate
the ISO:

```bash
bash "$SRC/examples/xorg/build.sh" --buildroot "$PWD"
bash automate.sh --binary "$SRC/examples/xorg/bin/cube"
bash make-iso.sh --no-qemu --output hello-world.iso
```

The result is `hello-world.iso` in the Buildroot tree. `automate.sh` installs the app under
`output/target/usr/bin`, creates `S90app`, and starts it with `DISPLAY=:0` after the Xorg socket
appears. To use the triangle example, substitute `triangle` for `cube`.

To pass app arguments or select its working directory:

```bash
bash automate.sh --binary /path/to/my_app --args "--fullscreen" --cwd /root
```

On a graphical host, use `bash make-iso.sh --qemu`. For a host without OpenGL support, add
`--no-virgl`; use `--no-qemu` on a headless build server.

## CLI options

- `init-buildroot.sh`: `--project`, `--projects`, `--skip-deps`, `--force`, `--non-interactive`,
  `--buildroot-dir`, and `--reuse-tree`.
- `init.sh`: `--no-menuconfig` or `--menuconfig`.
- `build.sh`: `--jobs N`, `--refresh-mesa`, `--no-iso`, `--make-iso`, or `--make-and-run`.
- `examples/xorg/build.sh`: `--buildroot DIR` and `--triplet TARGET`.
- `automate.sh`: `--binary FILE|NAME`, `--args STRING`, and `--cwd DIRECTORY`.
- `make-iso.sh`: `--output FILE`, `--no-qemu` or `--qemu`, `--memory MB`, `--display BACKEND`,
  and `--no-virgl`.

## Usage

Launch an X11 application inside the booted system with:

```bash
DISPLAY=:0 <application>
```

## Troubleshooting

- The post-build hook must be executable. `init-buildroot.sh` sets its executable bit when it
  distributes the layer.
- If ISO creation reports that `xorriso` or `mformat` is missing, install `xorriso` and `mtools`.
- `build.sh` checks that `output/images/bzImage` and `output/images/rootfs.cpio.gz` exist before
  continuing. The defconfig produces `bzImage`.
- If Xorg does not start, inspect `/var/log/Xorg.0.log` and confirm `CONFIG_DRM_VIRTIO_GPU` is
  enabled. Try `--no-virgl` if the QEMU host lacks OpenGL support.
- If the app starts before Xorg, inspect `/var/log/Xorg.0.log` and `/tmp/.X11-unix/X0`.
- For other build issues, see the [main README](../README.md#troubleshooting).
