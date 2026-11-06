#!/bin/bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILDROOT_DIR=${1:-${BUILDROOT_DIR:-"$SCRIPT_DIR/../../../wayland-os"}}
PLATFORM=${PLATFORM:-xorg}
case "$PLATFORM" in
    xorg) XORG_ON=ON; WESTON_ON=OFF ;;
    weston) XORG_ON=OFF; WESTON_ON=ON ;;
    *) echo "ERROR: PLATFORM must be xorg or weston, got: $PLATFORM" >&2; exit 2 ;;
esac
BUILD_DIR=${BUILD_DIR:-"$SCRIPT_DIR/build-$PLATFORM"}

if [[ ! -f "$BUILDROOT_DIR/output/host/share/buildroot/toolchainfile.cmake" ]]; then
    echo "ERROR: Buildroot toolchain not found at $BUILDROOT_DIR" >&2
    echo "Usage: [PLATFORM=xorg|weston] bash build.sh [BUILDROOT_DIR]" >&2
    exit 1
fi

BUILDROOT_DIR=$(cd "$BUILDROOT_DIR" && pwd)
if [[ -x "$BUILDROOT_DIR/output/host/bin/cmake" ]]; then
    CMAKE="$BUILDROOT_DIR/output/host/bin/cmake"
elif command -v cmake >/dev/null 2>&1; then
    CMAKE=$(command -v cmake)
else
    echo "ERROR: CMake was not found in Buildroot or on the host." >&2
    exit 1
fi

"$CMAKE" \
    -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$BUILDROOT_DIR/output/host/share/buildroot/toolchainfile.cmake" \
    -DCMAKE_FIND_ROOT_PATH="$BUILDROOT_DIR/output/staging" \
    -DCMAKE_BUILD_TYPE=Release -DXORG_PLATFORM=$XORG_ON -DWESTON_PLATFORM=$WESTON_ON
"$CMAKE" --build "$BUILD_DIR" --parallel "$(nproc)"

install -D -m 0755 "$BUILD_DIR/linweb" "$BUILDROOT_DIR/output/target/usr/bin/linweb"

# Web server that linweb switches on and off (System > Web Server), installed as /usr/sbin/steward-httpd.
HTTPD_SRC="$SCRIPT_DIR/../http-server/server.c"
TARGET_CC=$(ls "$BUILDROOT_DIR"/output/host/bin/*-linux-gnu-gcc 2>/dev/null | head -1 || true)
if [[ -f "$HTTPD_SRC" && -n "$TARGET_CC" ]]; then
    install -d "$BUILDROOT_DIR/output/target/usr/sbin"
    "$TARGET_CC" -std=c11 -O2 -Wall -Wextra "$HTTPD_SRC" -o "$BUILDROOT_DIR/output/target/usr/sbin/steward-httpd"
    echo "Installed /usr/sbin/steward-httpd."
else
    echo "WARNING: steward-httpd not built (missing $HTTPD_SRC or the Buildroot compiler); the web server switch will report it." >&2
fi
FONT_DIR="$SCRIPT_DIR/assets/fonts"
PS3_TARGET="$BUILDROOT_DIR/output/target/root/ps3"
rm -rf "$PS3_TARGET"
install -d "$PS3_TARGET"
cp -a "$SCRIPT_DIR/ps3/." "$PS3_TARGET/"
for font in arial.ttf arialbd.ttf monospace.ttf; do
    [[ -f "$FONT_DIR/$font" ]] || { echo "ERROR: Linweb font not found: $FONT_DIR/$font" >&2; exit 1; }
    install -D -m 0644 "$FONT_DIR/$font" "$PS3_TARGET/$font"
done
rm -f "$BUILDROOT_DIR/output/target/usr/bin/arial.ttf" \
      "$BUILDROOT_DIR/output/target/usr/bin/arialbd.ttf" \
      "$BUILDROOT_DIR/output/target/usr/bin/monospace.ttf"
echo "Installed Linweb and PS3 demo with its bundled fonts under output/target/root/ps3."
