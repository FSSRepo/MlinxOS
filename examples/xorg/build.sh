#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILDROOT_DIR=
TRIPLET=x86_64-buildroot-linux-gnu
usage() {
    cat <<'EOF'
Usage: bash build.sh [--buildroot DIR] [--triplet TARGET]

Without --buildroot, build with the host compiler and native X11/EGL packages.
With --buildroot, cross-compile for that Buildroot tree's output/staging sysroot.
EOF
}
while (($#)); do
    case "$1" in
        --buildroot) (($# >= 2)) || { echo "ERROR: --buildroot requires a path." >&2; exit 2; }; BUILDROOT_DIR=$2; shift 2 ;;
        --triplet) (($# >= 2)) || { echo "ERROR: --triplet requires a target triplet." >&2; exit 2; }; TRIPLET=$2; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "ERROR: unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ -n "$BUILDROOT_DIR" ]]; then
    BUILDROOT_DIR=$(cd "$BUILDROOT_DIR" && pwd)
    HOST_DIR="$BUILDROOT_DIR/output/host"
    SYSROOT="$BUILDROOT_DIR/output/staging"
    CC="$HOST_DIR/bin/$TRIPLET-gcc"
    CXX="$HOST_DIR/bin/$TRIPLET-g++"
    PKG_CONFIG="$HOST_DIR/bin/pkg-config"
    [[ -x "$CC" && -x "$CXX" ]] || { echo "ERROR: Buildroot cross compiler not found under $HOST_DIR/bin." >&2; exit 1; }
    [[ -x "$PKG_CONFIG" && -d "$SYSROOT/usr/lib/pkgconfig" ]] || { echo "ERROR: Buildroot pkg-config or staging sysroot is missing." >&2; exit 1; }
    export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
    export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig:$SYSROOT/usr/lib/$TRIPLET/pkgconfig"
    export PKG_CONFIG_PATH=
else
    CC=${CC:-gcc}
    CXX=${CXX:-g++}
    PKG_CONFIG=${PKG_CONFIG:-pkg-config}
fi

cd "$SCRIPT_DIR"
read -r -a PKG_CFLAGS <<< "$("$PKG_CONFIG" --cflags x11 egl glesv2)"
read -r -a PKG_LIBS <<< "$("$PKG_CONFIG" --libs x11 egl glesv2)"
mkdir -p bin
"$CXX" "${PKG_CFLAGS[@]}" -I"$SCRIPT_DIR/libs" "$SCRIPT_DIR/cube.cpp" \
    -o "$SCRIPT_DIR/bin/cube" "${PKG_LIBS[@]}" -lm
"$CC" "${PKG_CFLAGS[@]}" "$SCRIPT_DIR/triangle.c" \
    -o "$SCRIPT_DIR/bin/triangle" "${PKG_LIBS[@]}" -lm
echo "Built $SCRIPT_DIR/bin/cube and $SCRIPT_DIR/bin/triangle"
