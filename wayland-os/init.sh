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

# Iris needs Mesa's precompilation tools and LLVM, but the Buildroot version
# used here also selects the OpenCL runtime through this internal symbol.
# Decouple those options so Iris/OpenGL does not pull target Clang and libclc.
MESA3D_CONFIG_IN=package/mesa3d/Config.in
if [[ -f "$MESA3D_CONFIG_IN" ]]; then
    python3 - "$MESA3D_CONFIG_IN" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
old = "config BR2_PACKAGE_MESA3D_NEEDS_PRECOMP_COMPILER\n\tbool\n\tselect BR2_PACKAGE_MESA3D_OPENCL\n\tselect BR2_PACKAGE_SPIRV_LLVM_TRANSLATOR\n\tselect BR2_PACKAGE_SPIRV_TOOLS"
new = "config BR2_PACKAGE_MESA3D_NEEDS_PRECOMP_COMPILER\n\tbool\n\tselect BR2_PACKAGE_SPIRV_LLVM_TRANSLATOR\n\tselect BR2_PACKAGE_SPIRV_TOOLS"
if old in text:
    path.write_text(text.replace(old, new, 1))
elif "config BR2_PACKAGE_MESA3D_NEEDS_PRECOMP_COMPILER\n\tbool\n\tselect BR2_PACKAGE_SPIRV_LLVM_TRANSLATOR\n\tselect BR2_PACKAGE_SPIRV_TOOLS" not in text:
    raise SystemExit(f"ERROR: unexpected Mesa Kconfig block in {path}; refusing to patch")
PY
else
    echo "ERROR: Mesa Kconfig not found at $MESA3D_CONFIG_IN" >&2
    exit 1
fi

make mlinxos_defconfig

# Keep the Intel OpenGL path while ensuring Clang is not pulled in solely by
# the precompilation option. LLVM and Mesa's SPIR-V tools remain selected.
grep -qx 'BR2_PACKAGE_MESA3D_GALLIUM_DRIVER_IRIS=y' .config || {
    echo "ERROR: Iris is not enabled in the Wayland profile." >&2
    exit 1
}
grep -qx 'BR2_PACKAGE_MESA3D_LLVM=y' .config || {
    echo "ERROR: LLVM support is not enabled in the Wayland profile." >&2
    exit 1
}
if grep -Eq '^BR2_PACKAGE_(MESA3D_OPENCL|CLANG|LIBCLC)=y$' .config; then
    echo "ERROR: OpenCL, Clang, or libclc was enabled unexpectedly." >&2
    exit 1
fi

# Iris needs Mesa's SPIR-V compiler support. Limit target libclc to Mesa's
# generic SPIR-V builtins instead of compiling unrelated AMD/NVIDIA targets.
LIBCLC_MK=package/llvm-project/libclc/libclc.mk
LIBCLC_TARGET_OPTION='LIBCLC_CONF_OPTS += -DLIBCLC_TARGETS_TO_BUILD=spirv64-mesa3d-'
if [[ -f "$LIBCLC_MK" ]] && ! grep -Fq "$LIBCLC_TARGET_OPTION" "$LIBCLC_MK"; then
    printf '%s\n' "$LIBCLC_TARGET_OPTION" >> "$LIBCLC_MK"
fi

if (( MENUCONFIG )); then
    make menuconfig
fi
