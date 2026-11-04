#!/bin/bash
# Install Buildroot prerequisites, fetch Buildroot 2025.11, then distribute the
# selected project layer into an independent tree beside this repository.

set -euo pipefail

BR_VERSION="2025.11"
BR_TARBALL="buildroot-${BR_VERSION}.tar.gz"
BR_URL="https://buildroot.org/downloads/${BR_TARBALL}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PARENT_DIR="$(dirname "$SCRIPT_DIR")"
BR_BASE="${PARENT_DIR}/buildroot-${BR_VERSION}"
PROJECTS=(wayland-os xorg-os)
SELECTED=()
INSTALL_DEPS=1
INSTALL_QEMU=0
FORCE=0
NON_INTERACTIVE=0
REUSE_TREE=

usage() {
    cat <<EOF
Usage: bash init-buildroot.sh [options]

Options:
  -p, --project NAME       Select one project (repeatable).
      --projects LIST      Select comma/space-separated project names or 'all'.
      --all                Select all Buildroot projects.
      --skip-deps          Do not install host packages with apt.
      --with-qemu          Also install qemu-system-x86 for local ISO boot tests.
      --force              Overlay configuration when the target exists.
      --non-interactive    Fail instead of prompting for missing choices.
      --buildroot-dir DIR  Use an existing Buildroot 2025.11 tree.
      --reuse-tree DIR     Overlay one project onto an existing Buildroot tree,
                           preserving its output/ and downloads for incremental builds.
  -h, --help               Show this help.

Example: bash init-buildroot.sh --project wayland-os --non-interactive
EOF
}

add_projects() {
    local item candidate found
    local list=${1//,/ }
    for item in $list; do
        item=${item,,}
        if [[ "$item" == all || "$item" == '*' ]]; then
            SELECTED=("${PROJECTS[@]}")
            return
        fi
        found=0
        for candidate in "${PROJECTS[@]}"; do
            if [[ "$item" == "$candidate" ]]; then
                found=1
                if [[ ! " ${SELECTED[*]} " == *" $candidate "* ]]; then
                    SELECTED+=("$candidate")
                fi
            fi
        done
        if (( ! found )); then
            echo "ERROR: unknown project '$item'. Choose: ${PROJECTS[*]}" >&2
            return 1
        fi
    done
}

while (($#)); do
    case "$1" in
        -p|--project)
            (($# >= 2)) || { echo "ERROR: $1 requires a project name." >&2; exit 2; }
            add_projects "$2"
            shift 2
            ;;
        --projects)
            (($# >= 2)) || { echo "ERROR: --projects requires a list." >&2; exit 2; }
            add_projects "$2"
            shift 2
            ;;
        --all) SELECTED=("${PROJECTS[@]}"); shift ;;
        --skip-deps) INSTALL_DEPS=0; shift ;;
        --with-qemu) INSTALL_QEMU=1; shift ;;
        --force) FORCE=1; shift ;;
        --non-interactive) NON_INTERACTIVE=1; shift ;;
        --buildroot-dir)
            (($# >= 2)) || { echo "ERROR: --buildroot-dir requires a directory." >&2; exit 2; }
            BR_BASE=$(cd "$2" && pwd)
            shift 2
            ;;
        --reuse-tree)
            (($# >= 2)) || { echo "ERROR: --reuse-tree requires an existing Buildroot project tree." >&2; exit 2; }
            REUSE_TREE=$(cd "$2" && pwd)
            shift 2
            ;;
        -h|--help) usage; exit 0 ;;
        *) echo "ERROR: unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

install_deps() {
    local packages=(build-essential git wget cpio unzip rsync bc libncurses-dev file libelf-dev python3 bison flex grub-pc-bin xorriso mtools)
    echo ">> Installing Buildroot dependencies..."
    if ! command -v apt-get >/dev/null 2>&1; then
        echo "ERROR: apt-get not found. Install the packages listed in README.md manually." >&2
        return 1
    fi
    if (( INSTALL_QEMU )); then
        packages+=(qemu-system-x86)
    fi
    sudo apt-get update
    sudo apt-get install -y "${packages[@]}"
}

download_buildroot() {
    if [[ -n "$REUSE_TREE" ]]; then
        [[ -f "$REUSE_TREE/Makefile" ]] || { echo "ERROR: not a Buildroot tree: $REUSE_TREE" >&2; return 1; }
        if grep -Eq "^export BR2_VERSION[[:space:]]*(:=|=)[[:space:]]*${BR_VERSION}([[:space:]]|$)" "$REUSE_TREE/Makefile"; then
            echo ">> Reusing Buildroot ${BR_VERSION} tree at $REUSE_TREE."
            return 0
        fi
        echo "ERROR: $REUSE_TREE is not Buildroot ${BR_VERSION}." >&2
        return 1
    fi
    if [[ -d "$BR_BASE" ]]; then
        echo ">> Reusing Buildroot tree at $BR_BASE."
    else
        cd "$PARENT_DIR"
        if [[ ! -f "$BR_TARBALL" ]]; then
            echo ">> Downloading ${BR_TARBALL}..."
            wget "$BR_URL"
        fi
        echo ">> Extracting ${BR_TARBALL}..."
        tar -xzf "$BR_TARBALL"
    fi

    if grep -Eq "^export BR2_VERSION[[:space:]]*(:=|=)[[:space:]]*${BR_VERSION}([[:space:]]|$)" "$BR_BASE/Makefile"; then
        echo ">> Buildroot version verified: ${BR_VERSION}."
    else
        echo "ERROR: $BR_BASE is not Buildroot ${BR_VERSION}." >&2
        return 1
    fi
}

select_projects() {
    if ((${#SELECTED[@]} == 0)); then
        if (( NON_INTERACTIVE )); then
            echo "ERROR: select a project with --project or --projects." >&2
            return 2
        fi
        echo "Projects that use Buildroot: ${PROJECTS[*]}"
        read -r -p "Selection: " selection </dev/tty
        add_projects "$selection"
    fi
    printf '>> Selected projects: %s\n' "${SELECTED[*]}"
}

distribute() {
    local project src target answer
    if [[ -n "$REUSE_TREE" && ${#SELECTED[@]} -ne 1 ]]; then
        echo "ERROR: --reuse-tree requires exactly one selected project." >&2
        return 2
    fi
    for project in "${SELECTED[@]}"; do
        src="${SCRIPT_DIR}/${project}"
        target="${PARENT_DIR}/${project}"
        [[ -z "$REUSE_TREE" ]] || target=$REUSE_TREE
        [[ -d "$src" ]] || { echo "ERROR: missing layer $src" >&2; return 1; }

        echo ">> Preparing ${project} at ${target}"
        if [[ -n "$REUSE_TREE" ]]; then
            [[ -f "$target/Makefile" ]] || { echo "ERROR: not a Buildroot tree: $target" >&2; return 1; }
            if ! grep -Eq "^export BR2_VERSION[[:space:]]*(:=|=)[[:space:]]*${BR_VERSION}([[:space:]]|$)" "$target/Makefile"; then
                echo "ERROR: $target is not Buildroot ${BR_VERSION}." >&2
                return 1
            fi
            answer=y
        elif [[ -e "$target" ]]; then
            if (( FORCE )); then
                answer=y
            elif (( NON_INTERACTIVE )); then
                echo "ERROR: target exists; pass --force to overlay it: $target" >&2
                return 1
            else
                read -r -p "Target exists. Overlay its configuration? (y/n): " answer </dev/tty
            fi
            if [[ "$answer" != y && "$answer" != Y ]]; then
                echo "Skipped $project."
                continue
            fi
        else
            cp -a "$BR_BASE" "$target"
        fi

        mkdir -p "$target/configs"
        cp "$src/configs/mlinxos_defconfig" "$target/configs/"
        rm -rf "$target/mlinxos_config"
        cp -a "$src/mlinxos_config" "$target/"
        cp "$src"/*.sh "$target/"
        chmod +x "$target"/*.sh
        # Windows checkouts may not preserve the executable bit on this hook.
        [[ ! -f "$target/mlinxos_config/post-build.sh" ]] || chmod +x "$target/mlinxos_config/post-build.sh"
        echo "   Ready. Next: cd '$target' && bash init.sh"
    done
}

echo "Buildroot ${BR_VERSION} - MLinxOS"
(( INSTALL_DEPS == 0 )) || install_deps
download_buildroot
select_projects
distribute
echo ">> Distribution complete."
