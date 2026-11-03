# wayland-os

Minimal system based on **Buildroot 2025.11**. It boots Weston in kiosk mode with Mesa3D,
using llvmpipe software rendering and the virtio GPU where available.

See the [main README](../README.md) for host requirements and dependencies.

## Layer contents

```text
wayland-os/
  configs/mlinxos_defconfig       Buildroot configuration
  mlinxos_config/                 Kernel config, post-build hook, patches
  init.sh                         Select the defconfig and optionally open menuconfig
  build.sh                        Build, install boot scripts, and prepare ISO files
  automate.sh                     Install an app and configure Weston autolaunch
  make-iso.sh                     Build the initramfs and ISO; optionally run QEMU
```

## Ordered build and ISO flow

Run these commands from the repository root. `init-buildroot.sh` installs the dependencies,
downloads Buildroot 2025.11 if needed, then creates `../wayland-os` with this project's layer.

```bash
cd /path/to/steward-os
SRC="$PWD"
bash ./init-buildroot.sh --project wayland-os --non-interactive
```

If the host dependencies are already installed, add `--skip-deps`. If `../wayland-os` already
exists and you want to refresh its configuration layer, add `--force`.

Then configure and build the system. The no-menu option applies the checked-in defaults;
use `--menuconfig` instead if you need to edit them.

```bash
cd ../wayland-os
bash init.sh --no-menuconfig
bash build.sh --jobs "$(nproc)" --no-iso
```

`build.sh` compiles Buildroot, creates `output/iso/boot/vmlinuz` and the GRUB config, and writes
the init and Weston service files into `output/target`. `--no-iso` stops before ISO creation so
a custom app can be installed first.

Cross-compile the Wayland example for this Buildroot tree, install `cube` into the target
rootfs, configure Weston to launch it, then create the ISO:

```bash
bash "$SRC/examples/wayland/build.sh" --buildroot "$PWD"
bash automate.sh --binary "$SRC/examples/wayland/bin/cube"
bash make-iso.sh --no-qemu --output hello-world.iso
```

The resulting ISO is `hello-world.iso` in the Buildroot tree. The cross-build also produces
`weston-green` in the example's `bin/` directory. `automate.sh` copies the selected executable
to `output/target/usr/bin` and writes `output/target/etc/xdg/weston/weston.ini`.

To add arguments or a working directory for another app, use `--args` and `--cwd`:

```bash
bash automate.sh --binary /path/to/my_app --args "--fullscreen" --cwd /root
```

To create the ISO and launch it on a graphical host, run `bash make-iso.sh --qemu`. On a host
without OpenGL support, use `bash make-iso.sh --qemu --no-virgl`. For a headless build server,
keep `--no-qemu` so ISO generation does not try to open a display.

## CLI options

- `init-buildroot.sh`: `--project`, `--projects`, `--skip-deps`, `--force`, `--non-interactive`,
  and `--with-qemu`.
- `init.sh`: `--no-menuconfig` or `--menuconfig`.
- `build.sh`: `--jobs N`, `--no-iso`, `--make-iso`, or `--make-and-run`.
- `examples/wayland/build.sh`: `--buildroot DIR` and `--triplet TARGET`.
- `automate.sh`: `--binary FILE|NAME`, `--args STRING`, and `--cwd DIRECTORY`.
- `make-iso.sh`: `--output FILE`, `--no-qemu` or `--qemu`, `--memory MB`, `--display BACKEND`,
  and `--no-virgl`.

## Usage

Launch a Wayland client inside the booted system with:

```bash
XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=wayland-1 <application>
```

## Troubleshooting

- The post-build hook must be executable. `init-buildroot.sh` sets its executable bit when it
  distributes the layer.
- If ISO creation reports that `xorriso` or `mformat` is missing, install `xorriso` and `mtools`.
- `build.sh` checks that `output/images/bzImage` and `output/images/rootfs.cpio.gz` exist before
  continuing. The defconfig produces `bzImage`, not `vmlinux`.
- If the client cannot connect, export `XDG_RUNTIME_DIR` and `WAYLAND_DISPLAY` in its session.
- If Weston does not start under QEMU, check that `CONFIG_DRM_VIRTIO_GPU` is enabled and inspect
  `/var/log/weston.log`. Try `--no-virgl` if the host lacks OpenGL support.
- For other build issues, see the [main README](../README.md#troubleshooting).