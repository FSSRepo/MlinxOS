# simple-os

Didactic ISO **without Buildroot**: uses the host kernel + a BusyBox binary to build a
minimal bootable ISO. The kernel is the host's, not 6.12.47.

Requirements: see the [main README](../README.md). Also install (and run on Ubuntu, with
`sudo`):

```bash
sudo apt install -y busybox-static grub-pc-bin xorriso qemu-system-x86
```

## Build and run

```bash
cd simple-os
bash init.sh        # prepares everything and creates hello-world.iso
bash make-iso.sh    # rebuilds the initramfs and boots QEMU (128 MB, virtio-vga)
```

## Notes / troubleshooting

- `init.sh` **overwrites** `initramfs/init` with a "Hello world" loop variant; the stored
  version instead chains into BusyBox `init` (reads `inittab`). They are two didactic
  variants; restore the stored one if you want it back.
- Needs `/bin/busybox` (from `busybox-static`) and root for `busybox --install`.
- The `grub.cfg` boots `/boot/vmlinuz` + `/boot/initramfs.img`, so both must exist in
  `iso/boot/` before creating the ISO (`ls -l iso/boot/`).
