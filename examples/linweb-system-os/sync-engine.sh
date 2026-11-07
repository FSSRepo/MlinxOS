#!/bin/bash
# Copies the generic Linweb engine into ../linweb-engine, the folder the module builds against.
# The engine is never edited here: change it in its own repository and run this script again.
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
ENGINE_REPO=${1:-"$SCRIPT_DIR/../../../Linweb-web-engine"}
DEST=${DEST:-"$SCRIPT_DIR/../linweb-engine"}

if [[ ! -f "$ENGINE_REPO/CMakeLists.txt" ]]; then
    echo "ERROR: Linweb engine not found at $ENGINE_REPO (missing CMakeLists.txt)." >&2
    echo "Usage: bash sync-engine.sh [ENGINE_REPO]" >&2
    exit 1
fi
ENGINE_REPO=$(cd "$ENGINE_REPO" && pwd)
if [[ -z "$DEST" || "$DEST" == "/" ]]; then
    echo "ERROR: refusing to synchronize into '$DEST'." >&2
    exit 1
fi

# Every exclude is anchored to the top of the engine repository. An unanchored build* would also drop
# external/mbedtls/include/mbedtls/build_info.h and the module would not compile.
if command -v rsync >/dev/null 2>&1; then
    if [[ -n "${SYNC_DRY_RUN:-}" ]]; then
        echo "rsync -a --delete --exclude '/.git' --exclude '/build*' --exclude '/downloads' --exclude '/.playwright-mcp' --exclude '/examples' --exclude '/tools' \"$ENGINE_REPO/\" \"$DEST/\""
        exit 0
    fi
    rsync -a --delete \
        --exclude '/.git' --exclude '/build*' --exclude '/downloads' \
        --exclude '/.playwright-mcp' --exclude '/examples' --exclude '/tools' \
        "$ENGINE_REPO/" "$DEST/"
else
    # tar patterns starting with ./ are anchored to the top too.
    if [[ -n "${SYNC_DRY_RUN:-}" ]]; then
        echo "tar -C \"$ENGINE_REPO\" --exclude=./.git --exclude='./build*' --exclude=./downloads --exclude=./.playwright-mcp --exclude=./examples --exclude=./tools -cf - . | tar -C \"$DEST\" -xf -"
        exit 0
    fi
    rm -rf "$DEST"
    mkdir -p "$DEST"
    tar -C "$ENGINE_REPO" --exclude=./.git --exclude='./build*' --exclude=./downloads \
        --exclude=./.playwright-mcp --exclude=./examples --exclude=./tools \
        -cf - . | tar -C "$DEST" -xf -
fi

if [[ ! -f "$DEST/external/mbedtls/include/mbedtls/build_info.h" ]]; then
    echo "ERROR: the copy at $DEST is incomplete: external/mbedtls/include/mbedtls/build_info.h is missing." >&2
    echo "Check the exclude patterns: they must be anchored to the top of the engine repository." >&2
    exit 1
fi

# The engine commit this copy came from, so the module can say what it was built against.
VERSION="unknown"
if git -C "$ENGINE_REPO" rev-parse --short HEAD >/dev/null 2>&1; then
    VERSION=$(git -C "$ENGINE_REPO" rev-parse --short HEAD)
    DIRTY=$(git -C "$ENGINE_REPO" status --porcelain 2>/dev/null || true)
    if [[ -n "$DIRTY" ]]; then
        VERSION="$VERSION dirty"
    fi
fi
printf '%s\n' "$VERSION" > "$DEST/ENGINE_VERSION"
echo "Synchronized the engine into $DEST ($VERSION)."
