#!/bin/bash

set -euo pipefail

JOBS=$(nproc)
ISO_MODE=prompt
while (($#)); do
    case "$1" in
        -j|--jobs)
            (($# >= 2)) || { echo "ERROR: --jobs requires a positive integer." >&2; exit 2; }
            JOBS=$2
            shift 2
            ;;
        --no-iso) ISO_MODE=skip; shift ;;
        --make-iso) ISO_MODE=iso; shift ;;
        --make-and-run) ISO_MODE=run; shift ;;
        -h|--help)
            echo "Usage: bash build.sh [-j|--jobs N] [--no-iso|--make-iso|--make-and-run]"
            exit 0
            ;;
        *) echo "ERROR: unknown option: $1" >&2; exit 2 ;;
    esac
done

[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || { echo "ERROR: invalid job count: $JOBS" >&2; exit 2; }
make -j"$JOBS"

mkdir -p output/iso/boot/grub
[[ -f output/images/bzImage ]] || { echo "ERROR: output/images/bzImage not found. Kernel compilation failed." >&2; exit 1; }
[[ -f output/images/rootfs.cpio.gz ]] || { echo "ERROR: output/images/rootfs.cpio.gz not found. Initramfs generation failed." >&2; exit 1; }
[[ -d output/target ]] || { echo "ERROR: output/target not found. Rootfs was not generated." >&2; exit 1; }

cp output/images/bzImage output/iso/boot/vmlinuz
cat <<'EOF' > output/iso/boot/grub/grub.cfg
set timeout=0
set default=0

menuentry "Hello World Linux" {
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

cat <<'EOF' > output/target/etc/init.d/S50weston
#!/bin/sh
BIN=/usr/bin/weston
PIDFILE=/var/run/weston.pid
USER_ID=0
XDG_RUNTIME_DIR=/run/user/$USER_ID
WESTON_ARGS="--backend=drm --shell=kiosk --log=/var/log/weston.log"

start() {
    printf "Starting Weston: "
    mkdir -p "$XDG_RUNTIME_DIR"
    chmod 700 "$XDG_RUNTIME_DIR"
    export XDG_RUNTIME_DIR
    export WAYLAND_DISPLAY=wayland-1
    start-stop-daemon -S -q -b -m -p "$PIDFILE" --exec "$BIN" -- $WESTON_ARGS
    [ $? = 0 ] && sleep 2 && echo "OK" || echo "FAIL"
}
stop() {
    printf "Stopping Weston: "
    start-stop-daemon -K -q -p "$PIDFILE"
    [ $? = 0 ] && echo "OK" || echo "FAIL"
}
case "$1" in
    start) start ;;
    stop) stop ;;
    restart|reload) stop; sleep 2; start ;;
    *) echo "Usage: $0 {start|stop|restart}"; exit 1 ;;
esac
EOF
chmod +x output/target/init output/target/etc/init.d/S50weston

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
