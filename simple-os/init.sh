#!/bin/bash
set -e

# ============================
# Hello World Linux ISO Builder
# ============================

KERNEL_VERSION="$(uname -r)"
ISODIR="iso"
INITRAMFS_DIR="initramfs"

echo "[*] Creating directory structure..."
mkdir -p $ISODIR/boot/grub

mkdir -p $INITRAMFS_DIR/{bin,dev,proc,sys,sbin}

echo "[*] Copying kernel..."
cp /boot/vmlinuz-$KERNEL_VERSION $ISODIR/boot/vmlinuz

echo "[*] Copying busybox..."
cp /bin/busybox $INITRAMFS_DIR/bin/

echo "[*] Installing busybox links..."
pushd $INITRAMFS_DIR/bin >/dev/null
sudo ./busybox --install .
popd >/dev/null

echo "[*] Creating init..."
cat << 'EOF' > $INITRAMFS_DIR/init
#!/bin/sh

mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev

echo
echo "=================="
echo "   Hello world"
echo "=================="
echo

# Prevent kernel panic
while true; do
    sleep 1
done
EOF

chmod +x $INITRAMFS_DIR/init

echo "[*] Generating initramfs..."
pushd $INITRAMFS_DIR >/dev/null
find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../$ISODIR/boot/initramfs.img
popd >/dev/null

echo "[*] Creating GRUB configuration..."
cat << 'EOF' > $ISODIR/boot/grub/grub.cfg
set timeout=0
set default=0

menuentry "Hello World Linux" {
    linux /boot/vmlinuz quiet
    initrd /boot/initramfs.img
}
EOF

echo "[*] Installing dependencies..."
sudo apt install -y grub-pc-bin xorriso

echo "[*] Generating ISO..."
grub-mkrescue -o hello-world.iso $ISODIR

echo
echo "[✓] ISO created successfully: hello-world.iso"
echo "    You can boot it with QEMU or in a VM:"
echo "qemu-system-x86_64 -cdrom hello-world.iso -m 128 -device virtio-vga -display gtk "
echo
