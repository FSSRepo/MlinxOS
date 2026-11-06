#!/bin/sh
# Init of the live initramfs. It finds the medium (CD, USB stick, any disk with a live ISO), mounts
# live/rootfs.squashfs read-only, puts a tmpfs overlay on top so writes stay in RAM, and switches to it.
export PATH=/bin
mount -t devtmpfs devtmpfs /dev
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mkdir -p /cdrom /ro /rw /new_root

found=
for dev in /dev/sr[0-9]* /dev/sd[a-z] /dev/sd[a-z][0-9]*; do
    [ -b "$dev" ] || continue
    mount -o ro "$dev" /cdrom 2>/dev/null || continue
    if [ -f /cdrom/live/rootfs.squashfs ]; then found=1; break; fi
    umount /cdrom
done
if [ -z "$found" ]; then
    echo "live medium not found: no live/rootfs.squashfs on any disk"
    exec sh
fi

mount -t squashfs -o ro,loop /cdrom/live/rootfs.squashfs /ro || { echo "cannot mount the root filesystem"; exec sh; }
mount -t tmpfs tmpfs /rw
mkdir -p /rw/upper /rw/work
mount -t overlay overlay -o lowerdir=/ro,upperdir=/rw/upper,workdir=/rw/work /new_root || { echo "cannot mount the overlay"; exec sh; }

# The medium and the RAM layer stay visible in the live system (the installer skips the mounted disk).
mkdir -p /new_root/cdrom /new_root/rw /new_root/dev /new_root/proc /new_root/sys
mount --move /dev /new_root/dev
mount --move /proc /new_root/proc
mount --move /sys /new_root/sys
mount --move /cdrom /new_root/cdrom
mount --move /rw /new_root/rw
exec switch_root /new_root /sbin/init
