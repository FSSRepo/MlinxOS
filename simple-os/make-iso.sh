#!/bin/bash
ISODIR="iso"
INITRAMFS_DIR="initramfs"

chmod +x $INITRAMFS_DIR/init

echo "[*] Generating initramfs..."
pushd $INITRAMFS_DIR >/dev/null
find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../$ISODIR/boot/initramfs.img
popd >/dev/null

grub-mkrescue -o hello-world.iso "$ISODIR"
qemu-system-x86_64 -cdrom hello-world.iso -m 128 -device virtio-vga -display gtk

