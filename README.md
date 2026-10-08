# MLinxOS

Everything needed to build the project's **three minimalist Linux desktop systems**, plus the
graphical example applications in `examples/`.

Only the custom, non-regenerable layer is kept here (configs, patches, scripts, source);
regenerable artifacts (`output/`, `dl/`, ISOs, binaries) are omitted.

> Uses **Buildroot 2025.11**. The folders are configuration layers, not full Buildroot trees.

---

## Project index

| Project | Base | Graphical environment | Guide |
|---|---|---|---|
| **simple-os** | Host kernel + BusyBox | — | [simple-os/README.md](simple-os/README.md) |
| **wayland-os** | Buildroot 2025.11 | Weston on Wayland | [wayland-os/README.md](wayland-os/README.md) |
| **xorg-os** | Buildroot 2025.11 | X.Org server + Mesa3D | [xorg-os/README.md](xorg-os/README.md) |

All three share the hostname `MLinxOS` and the welcome message `Welcome to MLinxOS`.

---

## System requirements

- **Host must be Linux** (developed on Ubuntu 20.04+); not viable natively on Windows/macOS.
- **Hardware:** 4+ cores, 4 GiB RAM (8 recommended), 15 GiB free disk per system.
- A built `wayland-os`/`xorg-os` tree reaches ~19 GiB (`output/` + `dl/`); reserve 60+ GiB
  to build all three.

---

## Automated setup

From the repo root, `init-buildroot.sh` automates the manual steps below: it installs the
Buildroot dependencies, downloads and extracts Buildroot 2025.11, then asks which projects
to distribute its source to — creating one tree per selected project (`../<project>`, next
to the repo) with the project's configuration layer overlaid.

```bash
bash init-buildroot.sh
```

The sections below describe the same steps manually.

---

## Buildroot dependencies

Base packages (required for every Buildroot system):

```bash
sudo apt update
sudo apt install -y \
    build-essential git wget cpio unzip rsync bc \
    libncurses-dev file libelf-dev python3 bison flex
```

Key ones: `bison`/`flex` (kernel 6.12.47), `libelf-dev` (the defconfigs enable
`BR2_LINUX_KERNEL_NEEDS_HOST_LIBELF`), `cpio` (initramfs), `libncurses-dev` (`menuconfig`).

To boot the ISOs in QEMU:

```bash
sudo apt install -y qemu-system-x86 grub-pc-bin xorriso
```

Only for the didactic `simple-os` ISO (needs root for `busybox --install`):

```bash
sudo apt install -y busybox-static
```

---

## Get Buildroot 2025.11

```bash
cd ~
wget https://buildroot.org/downloads/buildroot-2025.11.tar.gz
tar -xzf buildroot-2025.11.tar.gz
cd buildroot-2025.11
grep BR2_VERSION Makefile      # must be 2025.11
```

Each system's README explains how to overlay its layer.

---

## Troubleshooting

- **`Unable to find host libelf`** → `sudo apt install -y libelf-dev`.
- **Hash errors** (`No hash found for ...`, checksum mismatch) → Buildroot must be exactly
  2025.11 and `BR2_GLOBAL_PATCH_DIR` must point to `mlinxos_config/patches`.
- **`No space left on device`** → `make clean` and/or `rm -rf dl`.
- **QEMU opens no window** (server/SSH) → use `-nographic` or `-vnc :0`; if the host lacks
  `gl=on`, drop `virgl=on`.
- **GRUB `file '/boot/vmlinuz' not found`** → ensure `output/iso/boot/` has `vmlinuz`,
  `initramfs.img` and `grub/grub.cfg`, and that the kernel was copied as `vmlinuz`.

---

## Notes

- No version control is used by project decision.
- The `linux-headers.hash -> ../linux/linux.hash` symlinks were materialized as real copies
  (Windows portability); if `linux.hash` changes, update the copies too.
- The folders on the development machine's desktop are the original trees, prior to the
  fixes; this repository is the source of truth.
