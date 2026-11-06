#!/bin/sh

set -u
set -e

# Add a console on tty1
if [ -e "${TARGET_DIR}/etc/inittab" ]; then
    grep -qE '^tty1::' "${TARGET_DIR}/etc/inittab" || \
	sed -i '/GENERIC_SERIAL/a\
tty1::respawn:/sbin/getty -L  tty1 0 vt100 # QEMU graphical window' "${TARGET_DIR}/etc/inittab"
fi

# Kernel image for mlinx-install: it copies /boot/bzImage to the installed disk.
mkdir -p "${TARGET_DIR}/boot"
cp "${BINARIES_DIR}/bzImage" "${TARGET_DIR}/boot/bzImage"

# GRUB for BIOS, for mlinx-install. Buildroot has no grub-install for the target, so the boot sector
# and the core image are made here (on the host) and the installer writes them to the disk with dd.
GRUB_OUT="${TARGET_DIR}/usr/share/mlinx/grub"
if [ "${MLINX_INSTALLER:-1}" = 1 ]; then
    GRUB_CORE=$(ls -d "${BUILD_DIR}"/grub2-*/build-i386-pc/grub-core | head -n 1)
    mkdir -p "${GRUB_OUT}"
    cp "${GRUB_CORE}/boot.img" "${GRUB_OUT}/boot.img"
    "${HOST_DIR}/bin/grub-mkimage" -O i386-pc -d "${GRUB_CORE}" -p '(hd0,msdos1)/boot/grub' \
        -o "${GRUB_OUT}/core.img" biosdisk part_msdos ext2 normal linux configfile
else
    rm -rf "${GRUB_OUT}"
fi

# Live initramfs: finds the medium and mounts live/rootfs.squashfs with an overlay on top.
sh mlinxos_config/make-live-initrd.sh "${TARGET_DIR}" "${BINARIES_DIR}/live-initrd.gz"