#!/bin/bash
#
# init-buildroot.sh
#
# 1) Installs the Buildroot dependencies.
# 2) Downloads and extracts Buildroot 2025.11 (once).
# 3) Asks which projects to distribute its source to and, for each selected
#    project, creates an independent Buildroot tree next to the repo, overlaying
#    the project's configuration layer (configs/, mlinxos_config/ and *.sh).
#
# Usage:  bash init-buildroot.sh
#

set -u

BR_VERSION="2025.11"
BR_TARBALL="buildroot-${BR_VERSION}.tar.gz"
BR_URL="https://buildroot.org/downloads/${BR_TARBALL}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PARENT_DIR="$(dirname "$SCRIPT_DIR")"
BR_BASE="${PARENT_DIR}/buildroot-${BR_VERSION}"

# Projects that use Buildroot (simple-os does NOT: it uses the host kernel + BusyBox).
PROJECTS="wayland-os xorg-os"

echo "==================================================="
echo " Buildroot ${BR_VERSION} - MLinxOS"
echo "==================================================="
echo

# ---------------------------------------------------------------------------
# 1) Buildroot dependencies
# ---------------------------------------------------------------------------
install_deps() {
    echo ">> Installing Buildroot dependencies..."

    if ! command -v apt-get >/dev/null 2>&1; then
        echo "WARNING: apt-get not found. Install manually:" >&2
        echo "  build-essential git wget cpio unzip rsync bc libncurses-dev file libelf-dev python3 bison flex" >&2
        return
    fi

    sudo apt-get update
    sudo apt-get install -y \
        build-essential git wget cpio unzip rsync bc \
        libncurses-dev file libelf-dev python3 bison flex

    echo ">> Dependencies installed."
}

# ---------------------------------------------------------------------------
# 2) Download Buildroot
# ---------------------------------------------------------------------------
download_buildroot() {
    if [ -d "$BR_BASE" ]; then
        echo ">> Buildroot already extracted at $BR_BASE (reusing it)."
        return
    fi

    cd "$PARENT_DIR" || exit 1

    if [ ! -f "$BR_TARBALL" ]; then
        echo ">> Downloading ${BR_TARBALL}..."
        wget "$BR_URL" || { echo "ERROR: failed to download ${BR_TARBALL}." >&2; exit 1; }
    fi

    echo ">> Extracting ${BR_TARBALL}..."
    tar -xzf "$BR_TARBALL" || { echo "ERROR: failed to extract ${BR_TARBALL}." >&2; exit 1; }

    if grep -q "BR2_VERSION = ${BR_VERSION}" "$BR_BASE/Makefile" 2>/dev/null; then
        echo ">> Version verified: ${BR_VERSION}."
    else
        echo "WARNING: could not verify BR2_VERSION in $BR_BASE/Makefile." >&2
    fi
}

# ---------------------------------------------------------------------------
# 3) Project selection
# ---------------------------------------------------------------------------
select_projects() {
    echo
    echo "Projects that use Buildroot: ${PROJECTS}"
    echo "Which projects should the Buildroot source be distributed to?"
    echo "  Type 'all' or the names separated by spaces/commas."
    read -r -p "Selection: " selection

    selection="$(echo "$selection" | tr ',' ' ' | tr '[:upper:]' '[:lower:]')"

    SELECTED=""
    if [ "$selection" = "all" ] || [ "$selection" = "*" ]; then
        SELECTED="${PROJECTS}"
    else
        for name in $selection; do
            for p in $PROJECTS; do
                if [ "$name" = "$p" ]; then
                    case " $SELECTED " in
                        *" $p "*) ;;                      # already included
                        *) SELECTED="$SELECTED $p" ;;
                    esac
                fi
            done
        done
        SELECTED="${SELECTED# }"                          # drop leading space
    fi

    if [ -z "$SELECTED" ]; then
        echo "ERROR: no valid project selected." >&2
        exit 1
    fi

    echo ">> Selected projects: ${SELECTED}"
}

# ---------------------------------------------------------------------------
# 4) Distribution: one Buildroot tree per project
# ---------------------------------------------------------------------------
distribute() {
    local project src target answer

    for project in ${SELECTED}; do
        src="${SCRIPT_DIR}/${project}"
        target="${PARENT_DIR}/${project}"

        echo
        echo ">> Preparing Buildroot tree for '${project}' at ${target}"

        if [ ! -d "$src" ]; then
            echo "   WARNING: ${src} does not exist. Skipping." >&2
            continue
        fi

        if [ -e "$target" ]; then
            read -r -p "   Target already exists. Overwrite the configuration layer? (y/n): " answer
            if [ "$answer" != "y" ] && [ "$answer" != "Y" ]; then
                echo "   Skipped."
                continue
            fi
        else
            echo "   Copying the base Buildroot tree..."
            cp -r "$BR_BASE" "$target" || { echo "   ERROR: could not copy the base tree." >&2; continue; }
        fi

        # Overlay the project layer (see each project's README).
        mkdir -p "$target/configs"
        cp "$src/configs/mlinxos_defconfig" "$target/configs/" || { echo "   ERROR: missing configs/mlinxos_defconfig." >&2; continue; }

        rm -rf "$target/mlinxos_config"
        cp -r "$src/mlinxos_config" "$target/" || { echo "   ERROR: missing mlinxos_config/." >&2; continue; }

        cp "$src"/*.sh "$target/" || { echo "   ERROR: could not copy the scripts." >&2; continue; }
        chmod +x "$target"/*.sh

        echo "   Done. Next step:"
        echo "     cd ${target} && bash init.sh"
    done
}

install_deps
download_buildroot
select_projects
distribute

echo
echo "==================================================="
echo " Distribution complete."
echo "==================================================="
