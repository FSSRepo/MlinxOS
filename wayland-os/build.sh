#!/bin/bash

set -euo pipefail

JOBS=$(nproc)
ISO_MODE=prompt
# The installer (mlinx-install and the GRUB files it writes to disk) is in the image unless --no-installer.
export MLINX_INSTALLER=1
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
        --no-installer) MLINX_INSTALLER=0; shift ;;
        -h|--help)
            echo "Usage: bash build.sh [-j|--jobs N] [--no-iso|--make-iso|--make-and-run] [--no-installer]"
            exit 0
            ;;
        *) echo "ERROR: unknown option: $1" >&2; exit 2 ;;
    esac
done

[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || { echo "ERROR: invalid job count: $JOBS" >&2; exit 2; }
make -j"$JOBS"

mkdir -p output/iso/boot/grub
[[ -f output/images/bzImage ]] || { echo "ERROR: output/images/bzImage not found. Kernel compilation failed." >&2; exit 1; }
[[ -f output/images/live-initrd.gz ]] || { echo "ERROR: output/images/live-initrd.gz not found. Live initramfs generation failed." >&2; exit 1; }
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
    # Keyboard layout from the kernel command line: xkb_layout=es [xkb_variant=...]
    LAYOUT=; VARIANT=
    for arg in $(cat /proc/cmdline); do
        case "$arg" in
            xkb_layout=*) LAYOUT=${arg#xkb_layout=} ;;
            xkb_variant=*) VARIANT=${arg#xkb_variant=} ;;
        esac
    done
    if [ -n "$LAYOUT" ] && [ -f /etc/xdg/weston/weston.ini ]; then
        sed -i '/^\[keyboard\]/,/^$/d' /etc/xdg/weston/weston.ini
        {
            echo
            echo "[keyboard]"
            echo "keymap_layout=$LAYOUT"
            [ -n "$VARIANT" ] && echo "keymap_variant=$VARIANT"
        } >> /etc/xdg/weston/weston.ini
    fi
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
cat <<'EOF' > output/target/etc/init.d/S41ethernet
#!/bin/sh
# Loopback and DHCP on wired interfaces (QEMU's virtio NIC, or a cable); Wi-Fi is handled by linweb.
case "$1" in
    start)
        ip link set lo up 2>/dev/null
        for dev in /sys/class/net/*; do
            name=${dev##*/}
            [ "$name" = lo ] && continue
            if [ -d "$dev/wireless" ] || [ -d "$dev/phy80211" ]; then continue; fi
            ip link set "$name" up 2>/dev/null
            udhcpc -b -i "$name" -p "/var/run/udhcpc.$name.pid" -t 5 -T 2 >/dev/null 2>&1 &
        done
        ;;
    stop)
        for pidfile in /var/run/udhcpc.*.pid; do
            [ -f "$pidfile" ] && kill "$(cat "$pidfile")" 2>/dev/null
        done
        ;;
    *) echo "Usage: $0 {start|stop}"; exit 1 ;;
esac
EOF
if [[ "$MLINX_INSTALLER" == 1 ]]; then
cat <<'EOF' > output/target/usr/sbin/mlinx-install
#!/bin/sh
# Installs MLinxOS on a whole disk (BIOS): one ext4 partition that fills the disk, GRUB in the MBR.
# DESTRUCTIVE: everything on DISK is erased. Progress goes to stdout; the last line is DONE or FAILED.
DISK="$1"
TARGET=/mnt/mlinx-install
fail() { echo "FAILED: $*"; exit 1; }
[ -n "$DISK" ] || fail "usage: mlinx-install /dev/DISK"
[ -b "$DISK" ] || fail "$DISK is not a block device"
case "$DISK" in
    *[0-9]) PART="${DISK}p1" ;;   # nvme0n1, mmcblk0
    *) PART="${DISK}1" ;;         # sda, vda
esac
trap 'umount "$TARGET" 2>/dev/null' EXIT

echo "Partitioning $DISK"
# Start at sector 2048: GRUB's core image lives in the sectors between the MBR and the partition.
printf 'o\nn\np\n1\n2048\n\nw\n' | fdisk "$DISK" >/dev/null 2>&1 || fail "could not partition $DISK"
sleep 2   # fdisk asks the kernel to reread the table; udev then creates the partition node
[ -b "$PART" ] || fail "partition $PART did not appear"

echo "Formatting $PART (ext4)"
mkfs.ext4 -q -F -L MLINX "$PART" || fail "mkfs.ext4 failed"
mkdir -p "$TARGET"
mount -t ext4 "$PART" "$TARGET" || fail "could not mount $PART"

echo "Copying the system"
(cd / && tar -cf - --exclude=./proc --exclude=./sys --exclude=./dev --exclude=./mnt --exclude=./tmp --exclude=./run --exclude=./cdrom --exclude=./rw .) \
    | (cd "$TARGET" && tar -xf -) || fail "copy failed"
mkdir -p "$TARGET/proc" "$TARGET/sys" "$TARGET/dev" "$TARGET/mnt" "$TARGET/tmp" "$TARGET/run"
[ -f "$TARGET/boot/bzImage" ] || fail "the kernel image is missing from the system (boot/bzImage)"

echo "Installing GRUB"
mkdir -p "$TARGET/boot/grub"
cat > "$TARGET/boot/grub/grub.cfg" <<'CFG'
set timeout=3
set default=0

menuentry "MLinxOS" {
    linux /boot/bzImage root=PARTUUID=PLACEHOLDER rw init=/sbin/init
}
CFG
# The kernel resolves PARTUUID= by itself (LABEL= needs an initramfs), so the disk gets a random
# signature at MBR offset 440 and the partition's PARTUUID is that signature plus "-01".
dd if=/dev/urandom of="$DISK" bs=1 seek=440 count=4 conv=notrunc 2>/dev/null || fail "could not write the disk signature"
SIG=$(od -An -tu4 -j440 -N4 "$DISK" | tr -d ' ')
PARTUUID=$(printf '%08x' "$SIG")-01
sed -i "s|PLACEHOLDER|$PARTUUID|" "$TARGET/boot/grub/grub.cfg"
GRUB=/usr/share/mlinx/grub
cp "$GRUB/boot.img" /tmp/mlinx-boot.img || fail "boot.img is missing"
cp "$GRUB/core.img" /tmp/mlinx-core.img || fail "core.img is missing"
# boot.img: the core starts at sector 1 (kernel sector, offset 0x5c); the BIOS boot drive is set at run time (0x64)
printf '\001\000\000\000\000\000\000\000' | dd of=/tmp/mlinx-boot.img bs=1 seek=92 conv=notrunc 2>/dev/null
printf '\377' | dd of=/tmp/mlinx-boot.img bs=1 seek=100 conv=notrunc 2>/dev/null
# core.img: whole sectors, then one block list entry at offset 500 (start 2, length, segment 0x820) and
# a terminator (zeros) at offset 488
SIZE=$(wc -c < /tmp/mlinx-core.img)
NSEC=$(( (SIZE + 511) / 512 ))
[ "$NSEC" -lt 2047 ] || fail "core.img does not fit between the MBR and the partition"
dd if=/dev/zero of=/tmp/mlinx-core.img bs=1 seek="$SIZE" count=$(( NSEC * 512 - SIZE )) conv=notrunc 2>/dev/null
dd if=/dev/zero of=/tmp/mlinx-core.img bs=1 seek=488 count=24 conv=notrunc 2>/dev/null
LEN_LO=$(( (NSEC - 1) & 255 )); LEN_HI=$(( ((NSEC - 1) >> 8) & 255 ))
printf "\\002\\000\\000\\000\\000\\000\\000\\000\\$(printf %03o $LEN_LO)\\$(printf %03o $LEN_HI)\\040\\010" \
    | dd of=/tmp/mlinx-core.img bs=1 seek=500 conv=notrunc 2>/dev/null
# MBR: the first 440 bytes of boot.img and its signature; the partition table is left alone
dd if=/tmp/mlinx-boot.img of="$DISK" bs=1 count=440 conv=notrunc 2>/dev/null || fail "could not write the MBR"
dd if=/tmp/mlinx-boot.img of="$DISK" bs=1 skip=510 seek=510 count=2 conv=notrunc 2>/dev/null || fail "could not write the MBR"
dd if=/tmp/mlinx-core.img of="$DISK" bs=512 seek=1 conv=notrunc 2>/dev/null || fail "could not write GRUB's core image"
rm -f /tmp/mlinx-boot.img /tmp/mlinx-core.img

sync
umount "$TARGET" || fail "could not unmount $TARGET"
echo "DONE: MLinxOS installed on $DISK"
EOF
chmod +x output/target/usr/sbin/mlinx-install
else
    rm -f output/target/usr/sbin/mlinx-install
fi
chmod +x output/target/init output/target/etc/init.d/S50weston output/target/etc/init.d/S41ethernet

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
