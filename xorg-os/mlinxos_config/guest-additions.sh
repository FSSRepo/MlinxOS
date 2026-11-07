#!/bin/bash
# Puts the VirtualBox Guest Additions into output/target: the kernel modules vboxguest and vboxsf (built here, against this
# tree's kernel, with the Buildroot toolchain), the service and clients from the Additions ISO, and an init script.
# With them VirtualBox can resize the display of the guest, share its clipboard and mount shared folders.
#
#   bash mlinxos_config/guest-additions.sh [VBoxGuestAdditions.iso]     (from the Buildroot tree, after build.sh)
#
# The ISO ships with VirtualBox (its install folder) and must be the version of the host. Run it again after build.sh has
# regenerated output/target, then make-iso.sh.
set -euo pipefail

ROOT=$(pwd)
[[ -d output/target && -d output/host ]] || { echo "ERROR: run this from the Buildroot tree (output/target not found)." >&2; exit 1; }
ISO=${1:-"$HOME/VBoxGuestAdditions.iso"}
[[ -f "$ISO" ]] || { echo "ERROR: Guest Additions ISO not found: $ISO" >&2; exit 1; }

KDIR=$(ls -d "$ROOT"/output/build/linux-[0-9]* | head -n 1)
KVER=$(basename "$KDIR"); KVER=${KVER#linux-}
CROSS=$(ls "$ROOT"/output/host/bin/*-linux-gnu-gcc | head -n 1); CROSS=${CROSS%gcc}
TARGET="$ROOT/output/target"
WORK="$ROOT/output/guest-additions"
rm -rf "$WORK"; mkdir -p "$WORK"

echo "== extracting $ISO"
xorriso -osirrox on -indev "$ISO" -extract /VBoxLinuxAdditions.run "$WORK/VBoxLinuxAdditions.run" >/dev/null 2>&1
(cd "$WORK" && sh VBoxLinuxAdditions.run --noexec --keep --nox11 --target ex >/dev/null 2>&1)
mkdir -p "$WORK/amd64" && tar -xjf "$WORK/ex/VBoxGuestAdditions-amd64.tar.bz2" -C "$WORK/amd64"
SRC=$(ls -d "$WORK"/amd64/src/vboxguest-*)
echo "   Additions $(basename "$SRC")  for kernel $KVER"

echo "== building the kernel modules"
kmake() { make -C "$KDIR" ARCH=x86 CROSS_COMPILE="$CROSS" "$@"; }
kmake M="$SRC/vboxguest" modules >"$WORK/build-vboxguest.log" 2>&1 || { tail -20 "$WORK/build-vboxguest.log"; exit 1; }
kmake M="$SRC/vboxsf" KBUILD_EXTRA_SYMBOLS="$SRC/vboxguest/Module.symvers" modules >"$WORK/build-vboxsf.log" 2>&1 || { tail -20 "$WORK/build-vboxsf.log"; exit 1; }
MODDIR="$TARGET/lib/modules/$KVER/extra"
mkdir -p "$MODDIR"
cp "$SRC/vboxguest/vboxguest.ko" "$SRC/vboxsf/vboxsf.ko" "$MODDIR/"
"$ROOT/output/host/sbin/depmod" -a -b "$TARGET" "$KVER"
echo "   $(ls "$MODDIR" | tr '\n' ' ')"

echo "== installing the service and the clients"
install -D -m 0755 "$WORK/amd64/sbin/VBoxService"    "$TARGET/usr/sbin/VBoxService"
install -D -m 0755 "$WORK/amd64/bin/VBoxControl"     "$TARGET/usr/bin/VBoxControl"
install -D -m 0755 "$WORK/amd64/bin/VBoxClient"      "$TARGET/usr/bin/VBoxClient"
install -D -m 0755 "$WORK/amd64/bin/VBoxDRMClient"   "$TARGET/usr/bin/VBoxDRMClient"
install -D -m 0755 "$WORK/amd64/other/mount.vboxsf"  "$TARGET/sbin/mount.vboxsf"

cat > "$TARGET/etc/init.d/S55vboxguest" <<'EOF'
#!/bin/sh
# VirtualBox Guest Additions: udev loads vboxguest from its PCI id; this starts the service and the display client that
# lets the host resize the guest screen. Nothing happens when the system does not run on VirtualBox.
SERVICE=/usr/sbin/VBoxService
DRMCLIENT=/usr/bin/VBoxDRMClient
CLIENT=/usr/bin/VBoxClient

start() {
    printf "Starting VirtualBox Guest Additions: "
    modprobe vboxguest 2>/dev/null
    i=0
    while [ ! -e /dev/vboxguest ] && [ "$i" -lt 20 ]; do sleep 0.5; i=$((i + 1)); done
    if [ ! -e /dev/vboxguest ]; then echo "not running on VirtualBox"; return 0; fi
    modprobe vboxsf 2>/dev/null
    # what they print goes to the console (the serial port when the kernel has console=ttyS0)
    start-stop-daemon -S -q -b -m -p /var/run/vboxservice.pid --exec /bin/sh -- -c "exec $SERVICE -f >/dev/console 2>&1"
    start-stop-daemon -S -q -b -m -p /var/run/vboxdrmclient.pid --exec /bin/sh -- -c "exec $DRMCLIENT >/dev/console 2>&1"
    # The session client reads the display changes the host asks for and hands them to VBoxDRMClient. It works through
    # X11 (it needs libX11 and a running server), so it is only started on an X.Org system.
    if [ -e /usr/lib/libX11.so.6 ]; then
        i=0
        while [ ! -S /tmp/.X11-unix/X0 ] && [ "$i" -lt 40 ]; do sleep 0.5; i=$((i + 1)); done
        start-stop-daemon -S -q -b -m -p /var/run/vboxclient.pid --exec /bin/sh -- -c "export DISPLAY=:0 HOME=/root; exec $CLIENT --vmsvga-session --nodaemon >/dev/console 2>&1"
        echo "OK (display resizing through X11)"
    else
        echo "OK (no X11: the display cannot follow the host window)"
    fi
}
stop() {
    printf "Stopping VirtualBox Guest Additions: "
    start-stop-daemon -K -q -p /var/run/vboxclient.pid 2>/dev/null
    start-stop-daemon -K -q -p /var/run/vboxdrmclient.pid 2>/dev/null
    start-stop-daemon -K -q -p /var/run/vboxservice.pid 2>/dev/null
    echo "OK"
}
case "$1" in
    start) start ;;
    stop) stop ;;
    restart|reload) stop; sleep 1; start ;;
    *) echo "Usage: $0 {start|stop|restart}"; exit 1 ;;
esac
EOF
chmod 0755 "$TARGET/etc/init.d/S55vboxguest"
# On X.Org nothing applies the mode that the host asks for (a desktop environment would): when the DRM reports a change,
# switch the outputs to their preferred mode. VBoxDRMClient has already made the size of the host window the preferred mode.
if [[ -x "$TARGET/usr/bin/xrandr" ]]; then
    cat > "$TARGET/usr/bin/vbox-apply-mode" <<'EOF'
#!/bin/sh
# Called by udev when the DRM changes (the host resized the window): switch each connected output to its preferred mode.
# (xrandr --auto does nothing for an output that is already on; the mode has to be asked for.) X must own the display:
# while another virtual terminal is shown the server is not DRM master and the kernel refuses the change.
export DISPLAY=:0
[ -S /tmp/.X11-unix/X0 ] || exit 0
xrandr | awk '/^[^ ]/ { out = $1 } /^ / && /\+/ && out { print out, $1; out = "" }' | while read -r output mode; do
    xrandr --output "$output" --mode "$mode"
done
EOF
    chmod 0755 "$TARGET/usr/bin/vbox-apply-mode"
    install -d "$TARGET/etc/udev/rules.d"
    echo 'ACTION=="change", SUBSYSTEM=="drm", ENV{HOTPLUG}=="1", RUN+="/usr/bin/vbox-apply-mode"' > "$TARGET/etc/udev/rules.d/90-vbox-display.rules"
    echo "   display resizing on X.Org: udev rule and /usr/bin/vbox-apply-mode"
fi

echo "Done: Guest Additions in output/target (run make-iso.sh to put them in the ISO)."
