# xorg-os

Minimalist system based on **Buildroot 2025.11** that boots the **X.Org server** with
**Mesa3D** (`llvmpipe` + `virgl`, GLX/EGL/OpenGL ES).

Requirements and dependencies: see the [main README](../README.md).

## Layer contents

```
xorg-os/
├── configs/mlinxos_defconfig      # Buildroot defconfig
├── mlinxos_config/                # linux.config, post-build.sh, patches/*.hash
├── init.sh                        # make mlinxos_defconfig && make menuconfig
├── build.sh                       # make + init + grub.cfg + ISO
├── automate.sh                    # autolaunch a custom binary with X.Org
└── make-iso.sh                    # cpio + grub-mkrescue + QEMU
```

## Prepare the Buildroot tree

```bash
cd ~/buildroot-2025.11
SRC=/path/to/steward-os/xorg-os
cp $SRC/configs/mlinxos_defconfig configs/
cp -r $SRC/mlinxos_config .
cp $SRC/*.sh .
```

## Build

```bash
bash init.sh       # select the defconfig (opens menuconfig)
bash build.sh      # build + write init + ISO; asks to run QEMU
```

Output: `hello-world.iso`. QEMU runs with 512 MB and a virtual GPU
(`-device virtio-vga,virgl=on`); the serial console is multiplexed with the monitor
(`Ctrl-A C` toggles it). Regenerate/run the ISO alone with `bash make-iso.sh`.

Optional autolaunch of your own app:

```bash
bash automate.sh   # asks binary/args/cwd, writes /etc/init.d/S90app
```

The service waits for `/tmp/.X11-unix/X0` before starting the application.

## Usage

Inside the booted system:

```bash
DISPLAY=:0 <application>
```

## Troubleshooting

- **`build.sh` now runs the full cycle** (`make` + kernel copy + ISO `grub.cfg`); previously
  it only wrote `output/target/init`. The ISO `grub.cfg` must point to `/boot/vmlinuz` and
  `/boot/initramfs.img`.
- **Xorg won't start in QEMU** → check the DRM driver (`CONFIG_DRM_VIRTIO_GPU`), the RAM
  size, and the X server log; without host `gl=on`, boot without `virgl=on`.
- General build issues → [main README](../README.md#troubleshooting-general).
