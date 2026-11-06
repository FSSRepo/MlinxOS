#!/bin/sh

set -u
set -e

# Keep tty1 available for Xorg and provide a text console on tty2.
if [ -e "${TARGET_DIR}/etc/inittab" ]; then
	sed -i '/^tty1::/d' "${TARGET_DIR}/etc/inittab"
	grep -qE '^tty2::' "${TARGET_DIR}/etc/inittab" || \
	    sed -i '/GENERIC_SERIAL/a\
tty2::respawn:/sbin/getty -L tty2 0 vt100 # Xorg uses tty1' "${TARGET_DIR}/etc/inittab"
fi

# BR2_RELRO_FULL links the Xorg modules with BIND_NOW, so modesetting must resolve
# its libgbm calls (made through glamor) when it is loaded. It is not linked against
# libgbm, so add the dependency or Xorg ends up with "no screens found".
MODESETTING="${TARGET_DIR}/usr/lib/xorg/modules/drivers/modesetting_drv.so"
PATCHELF="${HOST_DIR}/bin/patchelf"
if [ -f "${MODESETTING}" ] && [ -x "${PATCHELF}" ]; then
	"${PATCHELF}" --print-needed "${MODESETTING}" | grep -qx 'libgbm.so.1' || \
	    "${PATCHELF}" --add-needed libgbm.so.1 "${MODESETTING}"
fi

# Live initramfs: finds the medium and mounts live/rootfs.squashfs with an overlay on top.
sh mlinxos_config/make-live-initrd.sh "${TARGET_DIR}" "${BINARIES_DIR}/live-initrd.gz"
