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
