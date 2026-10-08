# wayland-os

Minimalist system based on **Buildroot 2025.11** that boots **Weston** (Wayland) in kiosk
mode, with Mesa3D (`llvmpipe` software + `virgl` virtual GPU).

Requirements and dependencies: see the [main README](../README.md).

## Layer contents

```
wayland-os/
├── configs/mlinxos_defconfig      # Buildroot defconfig
├── mlinxos_config/                # linux.config, post-build.sh, patches/*.hash
├── init.sh                        # make mlinxos_defconfig && make menuconfig
├── build.sh                       # make + init + S50weston + grub.cfg + ISO
├── automate.sh                    # autolaunch a custom binary with Weston
└── make-iso.sh                    # cpio + grub-mkrescue + QEMU
```

## Prepare the Buildroot tree

```bash
cd ~/buildroot-2025.11
SRC=/path/to/steward-os/wayland-os
cp $SRC/configs/mlinxos_defconfig configs/
cp -r $SRC/mlinxos_config .
cp $SRC/*.sh .
```

## Build

```bash
bash init.sh       # select the defconfig (opens menuconfig)
bash build.sh      # build + write init/S50weston + ISO; asks to run QEMU
```

Output: `hello-world.iso`. QEMU runs with 512 MB and a virtual GPU
(`-device virtio-vga,virgl=on`); the serial console is multiplexed with the monitor
(`Ctrl-A C` toggles it). Regenerate/run the ISO alone with `bash make-iso.sh`.

Optional autolaunch of your own app:

```bash
bash automate.sh   # asks binary/args/cwd, writes /etc/xdg/weston/weston.ini
```

## Usage

Launch a Wayland client inside the booted system:

```bash
XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=wayland-1 <application>
```

## Troubleshooting

- **`build.sh` now copies `bzImage`** (the defconfig produces `bzImage`, not `vmlinux`) and
  fails early if the kernel/rootfs are missing. If you reuse an old tree, delete
  `output/iso/boot/vmlinuz` before rebuilding.
- **Client can't connect to the display** → export `XDG_RUNTIME_DIR` and `WAYLAND_DISPLAY`
  in your shell (the service only exports them for its own process).
- **Weston won't start in QEMU** → check the DRM driver (`CONFIG_DRM_VIRTIO_GPU`), the RAM
  size, and `/var/log/weston.log`; without host `gl=on`, boot without `virgl=on`.
- General build issues → [main README](../README.md#troubleshooting-general).
