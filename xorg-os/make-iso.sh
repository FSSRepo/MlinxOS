#!/bin/bash

set -euo pipefail

RUN_QEMU=1
ISO_PATH=hello-world.iso
MEMORY=512
DISPLAY_BACKEND=gtk
VIRGL=1
DISK_FILE=
DISK_SIZE_MB=512
AUDIO_BACKEND=
MUSIC_SRC=
VIDEO_SRC=
KEYBOARD=
NET=1
usage() {
    cat <<'EOF'
Usage: bash make-iso.sh [options]

Options:
  --output FILE       ISO path (default: hello-world.iso).
  --no-qemu           Create the ISO without launching QEMU.
  --qemu              Launch QEMU after creating the ISO (default).
  --memory MB         QEMU memory (default: 512).
  --display BACKEND   QEMU display backend (default: gtk).
  --no-virgl          Disable virgl and OpenGL acceleration.
  --disk FILE         Attach FILE as a raw virtio disk in QEMU; it is created (sparse) when missing.
  --disk-size MB      Size used when --disk creates the file (default: 512).
  --audio BACKEND     Add an Intel HDA sound card to QEMU using the host audio backend
                      (pa, alsa, sdl, dsound, none, or "wav,path=out.wav"). Plays through aplay in the guest.
  --music PATH        Embed an audio file (or every file in a directory) in the image under
                      /mnt/music, where the linweb Music Player finds it without mounting a disk.
  --video PATH        Embed a video file (or every file in a directory) under /mnt/video, where the linweb
                      Video Player finds it. MP4 with H.264 and AAC; 640x360 keeps the image small.
  --keyboard LAYOUT   Keyboard layout, as LAYOUT or LAYOUT,VARIANT (for example us, es, latam,
                      es,dvorak). Passed on the kernel command line; the default is us.
  --no-net            Do not add a network card to QEMU (by default a virtio NIC with user-mode NAT,
                      which the guest configures by DHCP: 10.0.2.15, internet through the host).
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
        --disk) (($# >= 2)) || { echo "ERROR: --disk requires a path." >&2; exit 2; }; DISK_FILE=$2; shift 2 ;;
        --disk-size) (($# >= 2)) || { echo "ERROR: --disk-size requires a value." >&2; exit 2; }; DISK_SIZE_MB=$2; shift 2 ;;
        --audio) (($# >= 2)) || { echo "ERROR: --audio requires a backend." >&2; exit 2; }; AUDIO_BACKEND=$2; shift 2 ;;
        --music) (($# >= 2)) || { echo "ERROR: --music requires a path." >&2; exit 2; }; MUSIC_SRC=$2; shift 2 ;;
        --video) (($# >= 2)) || { echo "ERROR: --video requires a path." >&2; exit 2; }; VIDEO_SRC=$2; shift 2 ;;
        --keyboard) (($# >= 2)) || { echo "ERROR: --keyboard requires a layout." >&2; exit 2; }; KEYBOARD=$2; shift 2 ;;
        --no-net) NET=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "ERROR: unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

[[ "$MEMORY" =~ ^[1-9][0-9]*$ ]] || { echo "ERROR: invalid memory size: $MEMORY" >&2; exit 2; }
[[ "$DISK_SIZE_MB" =~ ^[1-9][0-9]*$ ]] || { echo "ERROR: invalid disk size: $DISK_SIZE_MB" >&2; exit 2; }
[[ -d output/target ]] || { echo "ERROR: output/target not found." >&2; exit 1; }
[[ -f output/iso/boot/vmlinuz ]] || { echo "ERROR: output/iso/boot/vmlinuz not found. Run build.sh first." >&2; exit 1; }
command -v cpio >/dev/null || { echo "ERROR: cpio is required." >&2; exit 1; }
command -v grub-mkrescue >/dev/null || { echo "ERROR: grub-mkrescue is required." >&2; exit 1; }
command -v xorriso >/dev/null || { echo "ERROR: xorriso is required by grub-mkrescue. Install grub-pc-bin and xorriso." >&2; exit 1; }

if [[ -n "$MUSIC_SRC" ]]; then
    [[ -e "$MUSIC_SRC" ]] || { echo "ERROR: --music path not found: $MUSIC_SRC" >&2; exit 2; }
    rm -rf output/target/mnt/music
    mkdir -p output/target/mnt/music
    if [[ -d "$MUSIC_SRC" ]]; then cp -a "$MUSIC_SRC"/. output/target/mnt/music/; else cp -a "$MUSIC_SRC" output/target/mnt/music/; fi
    echo "Embedded in /mnt/music: $(ls output/target/mnt/music | wc -l) file(s)"
fi
if [[ -n "$VIDEO_SRC" ]]; then
    [[ -e "$VIDEO_SRC" ]] || { echo "ERROR: --video path not found: $VIDEO_SRC" >&2; exit 2; }
    rm -rf output/target/mnt/video
    mkdir -p output/target/mnt/video
    if [[ -d "$VIDEO_SRC" ]]; then cp -a "$VIDEO_SRC"/. output/target/mnt/video/; else cp -a "$VIDEO_SRC" output/target/mnt/video/; fi
    echo "Embedded in /mnt/video: $(ls output/target/mnt/video | wc -l) file(s)"
fi

mkdir -p output/iso/boot/grub output/iso/live
cp output/images/live-initrd.gz output/iso/boot/initramfs.img
# The whole system is one compressed file on the medium, read at run time (not copied to RAM).
output/host/bin/mksquashfs output/target output/iso/live/rootfs.squashfs -comp xz -noappend -all-root -quiet

if [[ -n "$KEYBOARD" ]]; then
    [[ "$KEYBOARD" =~ ^[A-Za-z0-9_-]+(,[A-Za-z0-9_-]+)?$ ]] || { echo "ERROR: invalid --keyboard value: $KEYBOARD" >&2; exit 2; }
    KB_LAYOUT=${KEYBOARD%%,*}
    KB_VARIANT=
    [[ "$KEYBOARD" == *,* ]] && KB_VARIANT=${KEYBOARD#*,}
    sed -i -e 's/ xkb_layout=[^ ]*//g' -e 's/ xkb_variant=[^ ]*//g' output/iso/boot/grub/grub.cfg
    sed -i "s#rdinit=/init#rdinit=/init xkb_layout=$KB_LAYOUT${KB_VARIANT:+ xkb_variant=$KB_VARIANT}#" output/iso/boot/grub/grub.cfg
    echo "Keyboard layout: $KB_LAYOUT${KB_VARIANT:+ ($KB_VARIANT)}"
fi

# BIOS-only El Torito image (no EFI/GPT/APM/HFS+), like the reference ISO.
GRUB_PC_DIR=${GRUB_PC_DIR:-/usr/lib/grub/i386-pc}
[[ -d "$GRUB_PC_DIR" ]] || { echo "ERROR: $GRUB_PC_DIR not found. Install grub-pc-bin." >&2; exit 1; }
grub-mkrescue -d "$GRUB_PC_DIR" -o "$ISO_PATH" output/iso
echo "ISO created: $ISO_PATH"

if (( RUN_QEMU )); then
    QEMU_ARGS=(-cdrom "$ISO_PATH" -m "$MEMORY" -serial mon:stdio)
    if (( VIRGL )); then
        QEMU_ARGS+=(-device virtio-vga,virgl=on -display "$DISPLAY_BACKEND",gl=on)
    else
        QEMU_ARGS+=(-device virtio-vga -display "$DISPLAY_BACKEND")
    fi
    if [[ -n "$DISK_FILE" ]]; then
        if [[ ! -e "$DISK_FILE" ]]; then
            truncate -s "${DISK_SIZE_MB}M" "$DISK_FILE"
            echo "Created blank ${DISK_SIZE_MB} MB disk: $DISK_FILE"
        fi
        QEMU_ARGS+=(-drive "file=$DISK_FILE,format=raw,if=virtio")
    fi
    if [[ -n "$AUDIO_BACKEND" ]]; then
        # -audiodev + intel-hda/hda-duplex also works on old QEMU (4.x), which lacks -audio
        QEMU_ARGS+=(-audiodev "$AUDIO_BACKEND,id=snd0" -device intel-hda -device hda-duplex,audiodev=snd0)
    fi
    if (( NET )); then
        # the guest kernel has virtio-net (and 8139cp), not QEMU's default e1000
        QEMU_ARGS+=(-netdev user,id=net0 -device virtio-net-pci,netdev=net0)
    fi
    qemu-system-x86_64 "${QEMU_ARGS[@]}"
fi
