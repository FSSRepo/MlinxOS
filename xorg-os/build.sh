#!/bin/sh

make -j$(nproc)

mkdir -p output/iso/boot/grub

cp output/images/bzImage output/iso/boot/vmlinuz

cat << 'GRUB_EOF' > output/iso/boot/grub/grub.cfg
set timeout=0
set default=0

menuentry "Hello World Linux" {
    linux /boot/vmlinuz console=ttyS0 root=/dev/ram0 rdinit=/init
    initrd /boot/initramfs.img
}
GRUB_EOF

cat << 'INIT_EOF' > output/target/init
#!/bin/sh
# devtmpfs does not get automounted for initramfs
/bin/mount -t devtmpfs devtmpfs /dev

# use the /dev/console device node from devtmpfs if possible to not
# confuse glibc's ttyname_r().
# This may fail (E.G. booted with console=), and errors from exec will
# terminate the shell, so use a subshell for the test
if (exec 0</dev/console) 2>/dev/null; then
    exec 0</dev/console
    exec 1>/dev/console
    exec 2>/dev/console
fi

exec /sbin/init "$@"
INIT_EOF

chmod +x output/target/init

echo "Create ISO and run in QEMU? (y/n):"
read answer

if [ "$answer" = "y" ] || [ "$answer" = "Y" ]; then
    bash make-iso.sh
else
    echo "ISO not generated. Run 'bash make-iso.sh' whenever you want to build and launch it."
fi
