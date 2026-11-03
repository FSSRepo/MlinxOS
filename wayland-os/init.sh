#!/bin/bash

set -euo pipefail

MENUCONFIG=1
while (($#)); do
    case "$1" in
        --no-menuconfig|--defaults) MENUCONFIG=0; shift ;;
        --menuconfig) MENUCONFIG=1; shift ;;
        -h|--help)
            echo "Usage: bash init.sh [--no-menuconfig|--menuconfig]"
            echo "  --no-menuconfig  Apply mlinxos_defconfig without opening the menu."
            exit 0
            ;;
        *) echo "ERROR: unknown option: $1" >&2; exit 2 ;;
    esac
done

make mlinxos_defconfig
if (( MENUCONFIG )); then
    make menuconfig
fi
