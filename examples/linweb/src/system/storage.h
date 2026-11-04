#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace linweb {
namespace storage {

struct Result {
    bool ok = false;
    std::string error;
};

struct FsInfo {
    std::string type;   // "ext4", "ext3", "ext2", "vfat" or "" when unknown/unformatted
    std::string label;
};

struct Partition {
    std::string name;       // e.g. "vda1"
    std::string path;       // e.g. "/dev/vda1"
    std::string fstype;
    std::string label;
    std::string mountpoint; // empty when not mounted
    uint64_t size_bytes = 0;
};

struct Disk {
    std::string name;       // e.g. "vda"
    std::string path;       // e.g. "/dev/vda"
    std::string model;
    std::string fstype;     // filesystem written directly on the disk, if any
    std::string label;
    std::string mountpoint;
    uint64_t size_bytes = 0;
    bool removable = false;
    bool read_only = false;
    bool optical = false;
    std::vector<Partition> partitions;
};

struct DirEntry {
    std::string name;
    bool is_dir = false;
    uint64_t size = 0;
};

// Directory every file operation is confined to, and where devices are mounted.
const std::string& mount_root();
void set_mount_root(const std::string& root);   // used by tests

// Inspect the superblock of a block device (or an image file).
FsInfo detect_fs(const std::string& path);

// Enumerate block devices from /sys/block, the filesystems found on them and
// their current mount points from /proc/mounts.
std::vector<Disk> list_disks(const std::string& sys_block = "/sys/block",
                             const std::string& dev_dir = "/dev",
                             const std::string& proc_mounts = "/proc/mounts");

Result mount_device(const std::string& dev_path, std::string& mountpoint_out);
Result unmount_path(const std::string& mountpoint);

// Run mkfs on dev_path. `confirm` must equal the device's base name (for example
// "vda1"); the device must be a whole disk or partition reported by list_disks(),
// must not be mounted, optical or read-only.
Result format_device(const std::string& dev_path, const std::string& fs,
                     const std::string& label, const std::string& confirm);

// Zero the first and last MiB of a whole disk (MBR, GPT and its backup, old filesystem
// signatures) and ask the kernel to re-read the partition table. Works on image files too.
Result wipe_partition_tables(const std::string& target);

// mkfs without the device checks. Exposed so the tool invocation can be tested on an image file.
Result run_mkfs(const std::string& target, const std::string& fs, const std::string& label);

// File operations. Every path must resolve inside mount_root().
Result list_dir(const std::string& path, std::vector<DirEntry>& out, bool& truncated);
Result read_file(const std::string& path, size_t max_bytes, std::string& out);
Result write_file(const std::string& path, const std::string& data);
Result make_dir(const std::string& path);
Result remove_path(const std::string& path);

} // namespace storage
} // namespace linweb
