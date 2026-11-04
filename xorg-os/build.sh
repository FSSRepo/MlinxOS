#!/bin/bash

set -euo pipefail

JOBS=$(nproc)
ISO_MODE=prompt
REFRESH_MESA=0
while (($#)); do
    case "$1" in
        -j|--jobs)
            (($# >= 2)) || { echo "ERROR: --jobs requires a positive integer." >&2; exit 2; }
            JOBS=$2; shift 2 ;;
        --no-iso) ISO_MODE=skip; shift ;;
        --make-iso) ISO_MODE=iso; shift ;;
        --make-and-run) ISO_MODE=run; shift ;;
        --refresh-mesa) REFRESH_MESA=1; shift ;;
        -h|--help)
            echo "Usage: bash build.sh [-j|--jobs N] [--refresh-mesa] [--no-iso|--make-iso|--make-and-run]"
            echo "  --refresh-mesa  Rebuild Mesa after switching an existing Wayland tree to X.Org."
            exit 0 ;;
        *) echo "ERROR: unknown option: $1" >&2; exit 2 ;;
    esac
done

[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || { echo "ERROR: invalid job count: $JOBS" >&2; exit 2; }
if (( REFRESH_MESA )); then
    make mesa3d-dirclean
fi
make -j"$JOBS"

[[ -f output/images/bzImage ]] || { echo "ERROR: output/images/bzImage not found. Kernel compilation failed." >&2; exit 1; }
[[ -f output/images/rootfs.cpio.gz ]] || { echo "ERROR: output/images/rootfs.cpio.gz not found. Initramfs generation failed." >&2; exit 1; }
[[ -d output/target ]] || { echo "ERROR: output/target not found. Rootfs was not generated." >&2; exit 1; }

mkdir -p output/iso/boot/grub output/target/etc/init.d
cp output/images/bzImage output/iso/boot/vmlinuz
[[ -x output/target/etc/init.d/S40xorg ]] || { echo "ERROR: Buildroot did not install its S40xorg service." >&2; exit 1; }
rm -f output/target/etc/init.d/S50weston output/target/etc/init.d/S50xorg output/target/etc/xdg/weston/weston.ini
rm -f output/target/usr/bin/weston output/target/usr/bin/weston-*
rm -rf output/target/usr/lib/libweston-*
rm -f output/target/usr/lib/libwayland-*.so*
rm -f output/target/usr/lib/pkgconfig/weston*.pc output/target/usr/lib/pkgconfig/libweston*.pc
rm -f output/target/usr/lib/pkgconfig/wayland*.pc
rm -rf output/target/usr/lib/weston output/target/usr/share/weston
rm -rf output/target/usr/share/libweston-* output/target/usr/share/wayland-sessions
rm -rf output/target/usr/share/wayland
cat <<'EOF' > output/iso/boot/grub/grub.cfg
set timeout=0
set default=0

menuentry "Hello World Linux (Xorg)" {
    linux /boot/vmlinuz console=ttyS0 root=/dev/ram0 rdinit=/init
    initrd /boot/initramfs.img
}
EOF

cat <<'EOF' > output/target/init
#!/bin/sh
/bin/mount -t devtmpfs devtmpfs /dev
if (exec 0</dev/console) 2>/dev/null; then
    exec 0</dev/console
    exec 1>/dev/console
    exec 2>/dev/console
fi
exec /sbin/init "$@"
EOF

chmod 0755 output/target/init

case "$ISO_MODE" in
    skip) echo "Build complete. ISO creation skipped (--no-iso)." ;;
    iso) bash make-iso.sh --no-qemu ;;
    run) bash make-iso.sh --qemu ;;
    prompt)
        read -r -p "Create ISO and run in QEMU? (y/n): " answer
        if [[ "$answer" == y || "$answer" == Y ]]; then
            bash make-iso.sh --qemu
        else
            echo "Build complete. Run 'bash make-iso.sh --no-qemu' to create the ISO."
        fi
        ;;
esac
