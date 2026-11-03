#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SCANNER=${WAYLAND_SCANNER:-wayland-scanner}
PROTO=${WAYLAND_XDG_SHELL_XML:-/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml}

usage() {
    echo "Usage: bash generate.sh [--scanner PATH] [--protocols-dir DIR]"
}
while (($#)); do
    case "$1" in
        --scanner) (($# >= 2)) || { echo "ERROR: --scanner requires a path." >&2; exit 2; }; SCANNER=$2; shift 2 ;;
        --protocols-dir)
            (($# >= 2)) || { echo "ERROR: --protocols-dir requires a directory." >&2; exit 2; }
            PROTO="$2/stable/xdg-shell/xdg-shell.xml"
            shift 2
            ;;
        -h|--help) usage; exit 0 ;;
        *) echo "ERROR: unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

[[ -f "$PROTO" ]] || { echo "ERROR: xdg-shell.xml not found: $PROTO" >&2; exit 1; }
command -v "$SCANNER" >/dev/null 2>&1 || { echo "ERROR: wayland-scanner not found: $SCANNER" >&2; exit 1; }

"$SCANNER" client-header "$PROTO" "$SCRIPT_DIR/libs/xdg-shell-client-protocol.h"
"$SCANNER" private-code "$PROTO" "$SCRIPT_DIR/libs/xdg-shell-client-protocol.c"
