#!/bin/bash
# Switch the active compositor (weston or xorg) without rebuilding.
#   output/                  shared packages (compositor/common.txt) + host/ + the active compositor's own parts
#   compositor/<name>/       the inactive compositor's own parts: build/ (its packages), target, images, iso
#                            (output/staging is only a link to the shared sysroot of host/: it stays where it is)
#   compositor/<name>/files/ build.sh, mlinxos_config/post-build.sh and .config of each compositor
# usage: switch-compositor.sh weston|xorg
set -euo pipefail
ROOT="$HOME/Desktop/system-base"
WANT="${1:?usage: switch-compositor.sh weston|xorg}"
case "$WANT" in weston|xorg) ;; *) echo "unknown compositor: $WANT" >&2; exit 2 ;; esac
cd "$ROOT"
CUR=$(cat compositor/active 2>/dev/null || echo weston)
if [ "$CUR" = "$WANT" ]; then echo "already on $WANT"; exit 0; fi
PARTS="target images iso"
mkdir -p "compositor/$CUR/build" "compositor/$WANT/build" output/build

# 1. park the current compositor's own packages and parts
for p in $(ls -A output/build); do
    grep -qxF "$p" compositor/common.txt && continue
    [ -e "compositor/$CUR/build/$p" ] && { echo "compositor/$CUR/build/$p already exists" >&2; exit 1; }
    mv "output/build/$p" "compositor/$CUR/build/"
done
for d in $PARTS; do
    [ -e "output/$d" ] || continue
    [ -e "compositor/$CUR/$d" ] && { echo "compositor/$CUR/$d already exists" >&2; exit 1; }
    mv "output/$d" "compositor/$CUR/$d"
done

# 2. bring the other compositor's own packages and parts into output/
for p in $(ls -A "compositor/$WANT/build"); do
    [ -e "output/build/$p" ] && { echo "output/build/$p already exists" >&2; exit 1; }
    mv "compositor/$WANT/build/$p" output/build/
done
for d in $PARTS; do
    [ -e "compositor/$WANT/$d" ] || continue
    mv "compositor/$WANT/$d" "output/$d"
done

# 2b. the install stamps of the shared packages belong to the target/images that are parked, so they travel with them
STAMPS=".stamp_target_installed .stamp_images_installed"
for d in output/build/*/; do
    p=${d%/}; p=${p##*/}
    grep -qxF "$p" compositor/common.txt || continue
    for s in $STAMPS; do
        [ -f "$d$s" ] || continue
        mkdir -p "compositor/$CUR/stamps/$s"
        mv "$d$s" "compositor/$CUR/stamps/$s/$p"
    done
done
for s in $STAMPS; do
    [ -d "compositor/$WANT/stamps/$s" ] || continue
    for f in compositor/$WANT/stamps/$s/*; do
        p=${f##*/}
        [ -d "output/build/$p" ] && mv "$f" "output/build/$p/$s"
    done
done

# 3. the files that differ between the compositors: build.sh, post-build.sh and the Buildroot configuration
for f in build.sh mlinxos_config/post-build.sh .config; do
    mkdir -p "compositor/$CUR/files/$(dirname "$f")"
    cp "$f" "compositor/$CUR/files/$f"
    [ -f "compositor/$WANT/files/$f" ] && cp "compositor/$WANT/files/$f" "$f"
done

echo "$WANT" > compositor/active
echo "active compositor: $WANT (previous: $CUR)"
echo "output/build: $(ls -A output/build | wc -l) packages"