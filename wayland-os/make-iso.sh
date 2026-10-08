#!/bin/bash

pushd "output/target" >/dev/null
find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../iso/boot/initramfs.img
popd >/dev/null
grub-mkrescue -o hello-world.iso output/iso
qemu-system-x86_64 -cdrom hello-world.iso -m 512 -device virtio-vga,virgl=on -display gtk,gl=on  -serial mon:stdio
