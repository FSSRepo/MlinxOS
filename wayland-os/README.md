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

Audio: `bash make-iso.sh --qemu --audio pa` (or `alsa`, `sdl`, `dsound`...) adds an Intel HDA card (`-audiodev pa,id=snd0 -device intel-hda -device hda-duplex,audiodev=snd0`, which also works on QEMU 4.x). The guest kernel has `SND_HDA_INTEL`/`SND_HDA_GENERIC` (plus `SND_INTEL8X0` for `-device AC97` and `SND_VIRTIO` for `virtio-sound`), and the rootfs ships `alsa-utils`. Check it inside the guest with `cat /proc/asound/cards`, `amixer sset Master 80% unmute` and `speaker-test -c2 -t sine -l1`; `--audio "wav,path=out.wav"` records what the guest plays when the host has no sound. `--music FILE_OR_DIR` embeds audio files in the image under `/mnt/music` so the Music Player finds them without mounting a disk (they make the initramfs, and the ISO, larger by their size).

### Disks in QEMU

The kernel supports virtio, SCSI and IDE disks, MBR/GPT partitions, ext4 and FAT, and the image
includes `mkfs.ext4` and `mkfs.vfat`. Attach a raw disk image to try it; `--disk` creates the
file (512 MB unless `--disk-size MB` is given) when it does not exist and exposes it as `/dev/vda`:

```bash
bash make-iso.sh --qemu --no-virgl --disk disk.img
```

In the PS3 menu, the **Storage** category lists the disks. Select one to mount or unmount it,
browse its files, create a test file, or format it as ext4 or FAT (the confirmation defaults to
Cancel). This is limited to what QEMU emulates for now; AHCI/NVMe drivers for real hardware are
not enabled yet.

## CLI options

- `init-buildroot.sh`: `--project`, `--projects`, `--skip-deps`, `--force`, `--non-interactive`,
  and `--with-qemu`.
- `init.sh`: `--no-menuconfig` or `--menuconfig`.
- `build.sh`: `--jobs N`, `--no-iso`, `--make-iso`, or `--make-and-run`.
- `examples/wayland/build.sh`: `--buildroot DIR` and `--triplet TARGET`.
- `automate.sh`: `--binary FILE|NAME`, `--args STRING`, and `--cwd DIRECTORY`.
- `make-iso.sh`: `--output FILE`, `--no-qemu` or `--qemu`, `--memory MB`, `--display BACKEND`,
  `--no-virgl`, `--disk FILE`, and `--disk-size MB`.

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