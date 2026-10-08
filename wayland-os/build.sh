#!/bin/sh

make -j$(nproc)

mkdir -p output/iso/boot/grub

if [ ! -f output/images/bzImage ]; then
    echo "ERROR: output/images/bzImage not found. Kernel compilation failed." >&2
    exit 1
fi

if [ ! -f output/images/rootfs.cpio.gz ]; then
    echo "ERROR: output/images/rootfs.cpio.gz not found. Initramfs generation failed." >&2
    exit 1
fi

cp output/images/bzImage output/iso/boot/vmlinuz

cat << 'EOF' > output/iso/boot/grub/grub.cfg
set timeout=0
set default=0

menuentry "Hello World Linux" {
    linux /boot/vmlinuz console=ttyS0 root=/dev/ram0 rdinit=/init
    initrd /boot/initramfs.img
}
EOF

if [ ! -d output/target ]; then
    echo "ERROR: output/target not found. Rootfs was not generated." >&2
    exit 1
fi

cat << 'EOF' > output/target/init
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
EOF

cat << 'EOF' > output/target/etc/init.d/S50weston
#!/bin/sh
#
# Starts Weston (Wayland compositor)
#

BIN=/usr/bin/weston
PIDFILE=/var/run/weston.pid

# UID 0 = root (normal for minimal systems)
USER_ID=0
XDG_RUNTIME_DIR=/run/user/$USER_ID

WESTON_ARGS="--backend=drm --shell=kiosk --log=/var/log/weston.log"

start() {
    printf "Starting Weston: "

    # Create XDG_RUNTIME_DIR correctly
    mkdir -p $XDG_RUNTIME_DIR
    chmod 700 $XDG_RUNTIME_DIR
    export XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR
    export WAYLAND_DISPLAY=wayland-1

    start-stop-daemon -S -q -b -m -p $PIDFILE \
        --exec $BIN -- $WESTON_ARGS

    [ $? = 0 ] && sleep 2 && echo "OK" || echo "FAIL"
}

stop() {
    printf "Stopping Weston: "
    start-stop-daemon -K -q -p $PIDFILE
    [ $? = 0 ] && echo "OK" || echo "FAIL"
}

restart() {
    stop
    sleep 2
    start
}

case "$1" in
    start)
        start
    ;;
    stop)
        stop
    ;;
    restart|reload)
        restart
    ;;
    *)
        echo "Usage: $0 {start|stop|restart}"
        exit 1
esac
EOF

chmod +x output/target/etc/init.d/S50weston
chmod +x output/target/init

echo "Create ISO and run in QEMU? (y/n):"
read answer

if [ "$answer" = "y" ] || [ "$answer" = "Y" ]; then
    bash make-iso.sh
else
    echo "ISO not generated. Run 'bash make-iso.sh' whenever you want to build and launch it."
fi
