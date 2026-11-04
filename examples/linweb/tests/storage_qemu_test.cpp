// End-to-end check of src/system/storage.cpp on the target system, against a
// blank disk (QEMU: add `-drive file=disk.img,format=raw,if=virtio`, i.e. /dev/vda).
// Cross-build it with the Buildroot toolchain and run it as root inside the guest:
//   storage_qemu_test [disk-name]
// WARNING: it formats the given disk (default "vda"), so never point it at real data.
#include "system/storage.h"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/fs.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

using namespace linweb::storage;

static int g_failures = 0;
static int g_step = 0;

static void report(bool ok, const std::string& what, const std::string& detail = "") {
    ++g_step;
    if (!ok) ++g_failures;
    std::printf("%s %02d: %s%s%s\n", ok ? "PASS" : "FAIL", g_step, what.c_str(),
                detail.empty() ? "" : " -- ", detail.c_str());
    std::fflush(stdout);
}

static bool check(const Result& r, const std::string& what) {
    report(r.ok, what, r.error);
    return r.ok;
}

static const Disk* find_disk(const std::vector<Disk>& disks, const std::string& name) {
    for (const Disk& d : disks) if (d.name == name) return &d;
    return nullptr;
}

// Write a one-partition MBR (type 0x83) so the kernel exposes <disk>1.
static bool write_mbr_partition(const std::string& dev) {
    unsigned char mbr[512];
    std::memset(mbr, 0, sizeof(mbr));
    unsigned char entry[16] = {0x00, 0x00, 0x00, 0x00, 0x83, 0x00, 0x00, 0x00,
                               0x00, 0x08, 0x00, 0x00, 0x20, 0x00, 0x01, 0x00};   // LBA 2048, 65568 sectors
    std::memcpy(mbr + 446, entry, sizeof(entry));
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    int fd = open(dev.c_str(), O_RDWR);
    if (fd < 0) return false;
    bool ok = pwrite(fd, mbr, sizeof(mbr), 0) == static_cast<ssize_t>(sizeof(mbr)) && fsync(fd) == 0;
    ioctl(fd, BLKRRPART);
    close(fd);
    return ok;
}

int main(int argc, char** argv) {
    const std::string name = argc > 1 ? argv[1] : "vda";
    const std::string dev = "/dev/" + name;
    const std::string mnt = mount_root() + "/" + name;

    std::vector<Disk> disks = list_disks();
    std::printf("disks:");
    for (const Disk& d : disks) std::printf(" %s(%llu bytes, fs='%s')", d.name.c_str(), (unsigned long long)d.size_bytes, d.fstype.c_str());
    std::printf("\n");
    const Disk* disk = find_disk(disks, name);
    report(disk != nullptr, "list_disks finds " + name);
    if (!disk) { std::printf("E2E DONE failures=%d\n", g_failures); return 1; }
    report(disk->size_bytes > 0 && !disk->optical, "disk has a size and is not optical");
    report(disk->fstype.empty(), "blank disk has no filesystem", disk->fstype);

    // A disk that already has a partition table: formatting it must clear the partitions.
    report(write_mbr_partition(dev), "write a test MBR with one partition");
    disks = list_disks();
    disk = find_disk(disks, name);
    report(disk && disk->partitions.size() == 1, "kernel exposes the test partition", disk ? disk->partitions.empty() ? "none" : disk->partitions[0].name : "no disk");

    // ext4: format, mount, write, remount.
    report(!format_device(dev, "ext4", "E2ETEST", "wrong").ok, "format with a wrong confirmation is refused");
    if (!check(format_device(dev, "ext4", "E2ETEST", name), "format ext4")) { std::printf("E2E DONE failures=%d\n", g_failures); return 1; }
    FsInfo info = detect_fs(dev);
    report(info.type == "ext4" && info.label == "E2ETEST", "detect_fs sees ext4/E2ETEST", info.type + "/" + info.label);
    disks = list_disks();
    disk = find_disk(disks, name);
    report(disk && disk->partitions.empty() && disk->fstype == "ext4", "formatting the disk removed the old partition");

    std::string mountpoint;
    if (!check(mount_device(dev, mountpoint), "mount ext4")) { std::printf("E2E DONE failures=%d\n", g_failures); return 1; }
    report(mountpoint == mnt, "mounted under the mount root", mountpoint);
    disk = nullptr;
    disks = list_disks();
    disk = find_disk(disks, name);
    report(disk && disk->mountpoint == mnt && disk->fstype == "ext4", "list_disks reports the mount");
    report(!format_device(dev, "vfat", "X", name).ok, "formatting a mounted disk is refused");

    check(write_file(mnt + "/hello.txt", "hola desde linweb\n"), "write_file");
    check(make_dir(mnt + "/docs"), "make_dir");
    std::string data;
    check(read_file(mnt + "/hello.txt", 0, data), "read_file");
    report(data == "hola desde linweb\n", "file content matches");
    std::vector<DirEntry> entries;
    bool truncated = false;
    check(list_dir(mnt, entries, truncated), "list_dir");
    bool saw_docs = false, saw_hello = false;
    for (const DirEntry& e : entries) {
        if (e.name == "docs" && e.is_dir) saw_docs = true;
        if (e.name == "hello.txt" && !e.is_dir && e.size == 18) saw_hello = true;
    }
    report(saw_docs && saw_hello, "list_dir shows docs/ and hello.txt (18 bytes)");
    report(!read_file("/etc/passwd", 0, data).ok, "files outside the mount root are refused");

    check(unmount_path(mnt), "unmount");
    report(!read_file(mnt + "/hello.txt", 0, data).ok, "file is gone while unmounted");
    check(mount_device(dev, mountpoint), "remount ext4");
    check(read_file(mnt + "/hello.txt", 0, data), "read after remount");
    report(data == "hola desde linweb\n", "data persisted across unmount/mount");
    check(remove_path(mnt + "/hello.txt"), "remove file");
    check(remove_path(mnt + "/docs"), "remove directory");
    check(unmount_path(mnt), "unmount again");

    // FAT: reformat the same disk.
    if (check(format_device(dev, "vfat", "E2EFAT", name), "format vfat")) {
        info = detect_fs(dev);
        report(info.type == "vfat" && info.label == "E2EFAT", "detect_fs sees vfat/E2EFAT", info.type + "/" + info.label);
        disks = list_disks();
        disk = find_disk(disks, name);
        report(disk && disk->partitions.empty() && disk->fstype == "vfat", "no phantom partition after FAT format",
               disk && !disk->partitions.empty() ? disk->partitions[0].name : "");
        if (check(mount_device(dev, mountpoint), "mount vfat")) {
            check(write_file(mnt + "/Fat File.txt", "fat ok\n"), "write on vfat");
            check(read_file(mnt + "/Fat File.txt", 0, data), "read on vfat");
            report(data == "fat ok\n", "vfat content matches");
            report(read_file(mnt + "/FAT FILE.TXT", 0, data).ok && data == "fat ok\n", "vfat lookups are case-insensitive");
            check(write_file(mnt + "/ñandú.txt", "acentos\n"), "write a UTF-8 file name on vfat");
            entries.clear();
            list_dir(mnt, entries, truncated);
            bool saw_utf8 = false;
            for (const DirEntry& e : entries) if (e.name == "ñandú.txt") saw_utf8 = true;
            report(saw_utf8, "UTF-8 file name is listed back unchanged");
            check(unmount_path(mnt), "unmount vfat");
        }
    }

    std::printf("E2E DONE failures=%d\n", g_failures);
    return g_failures ? 1 : 0;
}
