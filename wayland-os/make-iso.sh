#!/bin/bash

set -euo pipefail

RUN_QEMU=1
ISO_PATH=hello-world.iso
MEMORY=512
DISPLAY_BACKEND=gtk
VIRGL=1
usage() {
    cat <<'EOF'
Usage: bash make-iso.sh [options]

Options:
  --output FILE       ISO path (default: hello-world.iso).
  --no-qemu           Create the ISO without launching QEMU (for VPS/headless builds).
  --qemu              Launch QEMU after creating the ISO (default).
  --memory MB         QEMU memory (default: 512).
  --display BACKEND   QEMU display backend (default: gtk).
  --no-virgl           Disable virgl and OpenGL acceleration in QEMU.
  -h, --help          Show this help.
EOF
}
while (($#)); do
    case "$1" in
        --output|-o) (($# >= 2)) || { echo "ERROR: --output requires a path." >&2; exit 2; }; ISO_PATH=$2; shift 2 ;;
        --no-qemu) RUN_QEMU=0; shift ;;
        --qemu) RUN_QEMU=1; shift ;;
        --memory) (($# >= 2)) || { echo "ERROR: --memory requires a value." >&2; exit 2; }; MEMORY=$2; shift 2 ;;
        --display) (($# >= 2)) || { echo "ERROR: --display requires a value." >&2; exit 2; }; DISPLAY_BACKEND=$2; shift 2 ;;
        --no-virgl) VIRGL=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "ERROR: unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

[[ "$MEMORY" =~ ^[1-9][0-9]*$ ]] || { echo "ERROR: invalid memory size: $MEMORY" >&2; exit 2; }
[[ -d output/target ]] || { echo "ERROR: output/target not found." >&2; exit 1; }
[[ -f output/iso/boot/vmlinuz ]] || { echo "ERROR: output/iso/boot/vmlinuz not found. Run build.sh first." >&2; exit 1; }
command -v cpio >/dev/null || { echo "ERROR: cpio is required." >&2; exit 1; }
command -v grub-mkrescue >/dev/null || { echo "ERROR: grub-mkrescue is required." >&2; exit 1; }
command -v xorriso >/dev/null || { echo "ERROR: xorriso is required by grub-mkrescue. Install grub-pc-bin and xorriso." >&2; exit 1; }

mkdir -p output/iso/boot/grub
(
    cd output/target
    find . -print0 | cpio --quiet --null -o --format=newc | gzip -9
) > output/iso/boot/initramfs.img

grub-mkrescue -o "$ISO_PATH" output/iso
echo "ISO created: $ISO_PATH"

if (( RUN_QEMU )); then
    QEMU_ARGS=(-cdrom "$ISO_PATH" -m "$MEMORY" -serial mon:stdio)
    if (( VIRGL )); then
        QEMU_ARGS+=(-device virtio-vga,virgl=on -display "$DISPLAY_BACKEND",gl=on)
    else
        QEMU_ARGS+=(-device virtio-vga -display "$DISPLAY_BACKEND")
    fi
    qemu-system-x86_64 "${QEMU_ARGS[@]}"
fi
