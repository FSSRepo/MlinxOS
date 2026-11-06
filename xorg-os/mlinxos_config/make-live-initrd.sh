#!/bin/sh
# Builds the small initramfs of the live system: busybox with the libraries it needs, and live-init.sh
# as /init. Everything else of the system is in live/rootfs.squashfs, read from the medium.
# usage: make-live-initrd.sh TARGET_DIR OUT.gz
set -eu
TARGET_DIR="$1"
OUT="$2"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/bin" "$WORK/lib" "$WORK/lib64" "$WORK/dev" "$WORK/proc" "$WORK/sys" \
         "$WORK/cdrom" "$WORK/ro" "$WORK/rw" "$WORK/new_root"
cp "$TARGET_DIR/bin/busybox" "$WORK/bin/busybox"
for applet in sh mount umount mkdir sleep echo cat ls switch_root; do
    ln -s busybox "$WORK/bin/$applet"
done
cp "$TARGET_DIR/lib/ld-linux-x86-64.so.2" "$WORK/lib64/"
cp "$TARGET_DIR/lib/libc.so.6" "$TARGET_DIR/lib/libresolv.so.2" "$WORK/lib/"
cp "$TARGET_DIR/lib/libc.so.6" "$TARGET_DIR/lib/libresolv.so.2" "$WORK/lib64/"
cp "$(dirname "$0")/live-init.sh" "$WORK/init"
chmod 755 "$WORK/init"

(cd "$WORK" && find . | cpio -o -H newc --quiet | gzip -9) > "$OUT"
