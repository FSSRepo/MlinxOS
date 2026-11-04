#include "system/storage.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#ifdef __linux__
#include <dirent.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#endif

namespace linweb {
namespace storage {

static std::string g_mount_root = "/mnt";

const std::string& mount_root() { return g_mount_root; }
void set_mount_root(const std::string& root) { g_mount_root = root; }

#ifdef __linux__

namespace {

constexpr size_t kMaxFileBytes = 1024 * 1024;
constexpr size_t kMaxDirEntries = 512;

Result fail(const std::string& message) {
    Result r;
    r.ok = false;
    r.error = message;
    return r;
}

Result success() {
    Result r;
    r.ok = true;
    return r;
}

Result errno_fail(const std::string& what) {
    return fail(what + ": " + std::strerror(errno));
}

std::string trim_text(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\0')) s.pop_back();
    size_t start = 0;
    while (start < s.size() && s[start] == ' ') ++start;
    return s.substr(start);
}

// Keep labels printable ASCII so they are always valid UTF-8 for the script layer.
std::string printable_label(const unsigned char* data, size_t len) {
    std::string out;
    for (size_t i = 0; i < len && data[i] != 0; ++i) {
        if (data[i] >= 0x20 && data[i] < 0x7f) out.push_back(static_cast<char>(data[i]));
    }
    return trim_text(out);
}

std::string read_text(const std::string& path) {
    std::ifstream in(path);
    std::string line;
    if (in) std::getline(in, line);
    return trim_text(line);
}

uint64_t read_u64(const std::string& path) {
    std::string text = read_text(path);
    if (text.empty()) return 0;
    return std::strtoull(text.c_str(), nullptr, 10);
}

uint32_t le32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

std::string base_name(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool valid_device_name(const std::string& name) {
    if (name.empty() || name.size() > 32) return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

// "/dev/<name>" with a plain alphanumeric name.
bool valid_device_path(const std::string& path) {
    static const std::string prefix = "/dev/";
    return path.compare(0, prefix.size(), prefix) == 0 && valid_device_name(path.substr(prefix.size()));
}

std::string decode_mount_field(const std::string& field) {
    std::string out;
    for (size_t i = 0; i < field.size(); ++i) {
        if (field[i] == '\\' && i + 3 < field.size() && std::isdigit(static_cast<unsigned char>(field[i + 1]))) {
            out.push_back(static_cast<char>(std::strtol(field.substr(i + 1, 3).c_str(), nullptr, 8)));
            i += 3;
        } else {
            out.push_back(field[i]);
        }
    }
    return out;
}

// device path -> first mount point
std::map<std::string, std::string> read_mounts(const std::string& proc_mounts) {
    std::map<std::string, std::string> mounts;
    std::ifstream in(proc_mounts);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string device, mountpoint;
        if (!(fields >> device >> mountpoint)) continue;
        device = decode_mount_field(device);
        mountpoint = decode_mount_field(mountpoint);
        if (device.empty() || device[0] != '/') continue; // skip proc, tmpfs, devtmpfs, ...
        mounts.emplace(device, mountpoint);
    }
    return mounts;
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.compare(0, prefix.size(), prefix) == 0;
}

std::string mount_point_for(const std::map<std::string, std::string>& mounts, const std::string& dev) {
    auto it = mounts.find(dev);
    return it == mounts.end() ? std::string() : it->second;
}

// Resolve `path` and make sure it stays inside the mount root. When `must_exist`
// is false the last component may be missing (used to create files/directories).
Result resolve_allowed(const std::string& path, bool must_exist, std::string& resolved) {
    if (path.empty() || path[0] != '/') return fail("path must be absolute");
    if (path.find('\0') != std::string::npos) return fail("invalid path");

    char root_buf[PATH_MAX];
    if (!realpath(mount_root().c_str(), root_buf)) return errno_fail("mount root " + mount_root());
    std::string root = root_buf;

    auto inside = [&](const std::string& p) {
        return p == root || starts_with(p, root == "/" ? root : root + "/");
    };

    char buf[PATH_MAX];
    if (realpath(path.c_str(), buf)) {
        if (!inside(buf)) return fail("path is outside " + mount_root());
        resolved = buf;
        return success();
    }
    if (must_exist || errno != ENOENT) return errno_fail(path);

    // The leaf does not exist: validate its parent and append the leaf name.
    std::string clean = path;
    while (clean.size() > 1 && clean.back() == '/') clean.pop_back();
    size_t slash = clean.find_last_of('/');
    std::string parent = slash == 0 ? "/" : clean.substr(0, slash);
    std::string leaf = clean.substr(slash + 1);
    if (leaf.empty() || leaf == "." || leaf == "..") return fail("invalid file name");
    if (!realpath(parent.c_str(), buf)) return errno_fail(parent);
    if (!inside(buf)) return fail("path is outside " + mount_root());
    resolved = std::string(buf) + (std::strcmp(buf, "/") == 0 ? "" : "/") + leaf;
    return success();
}

void ensure_dir(const std::string& path) {
    if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) {
        // reported by the caller through the following mount/open error
    }
}

std::string sanitize_fs_label(const std::string& label, size_t max_len, bool upper) {
    std::string out;
    for (char c : label) {
        if (out.size() >= max_len) break;
        unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '_' || c == '-') out.push_back(upper ? static_cast<char>(std::toupper(u)) : c);
        else if (c == ' ') out.push_back('_');
    }
    return out;
}

const char* find_tool(const char* name) {
    static std::string found;
    const char* dirs[] = {"/usr/sbin/", "/sbin/", "/usr/bin/", "/bin/"};
    for (const char* dir : dirs) {
        std::string candidate = std::string(dir) + name;
        if (access(candidate.c_str(), X_OK) == 0) {
            found = candidate;
            return found.c_str();
        }
    }
    return nullptr;
}

} // namespace

FsInfo detect_fs(const std::string& path) {
    FsInfo info;
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return info;

    unsigned char block[2048];
    ssize_t n = pread(fd, block, sizeof(block), 0);
    close(fd);
    if (n < 512) return info;

    // ext2/3/4: superblock at byte 1024, magic 0xEF53 at offset 56.
    if (n >= 1280 && block[1024 + 56] == 0x53 && block[1024 + 57] == 0xEF) {
        const unsigned char* sb = block + 1024;
        uint32_t compat = le32(sb + 92);
        uint32_t incompat = le32(sb + 96);
        bool journal = (compat & 0x4) != 0;
        bool ext4_features = (incompat & (0x40 | 0x80 | 0x200)) != 0; // extents, 64bit, flex_bg
        info.type = journal ? (ext4_features ? "ext4" : "ext3") : (ext4_features ? "ext4" : "ext2");
        info.label = printable_label(sb + 120, 16);
        return info;
    }

    // FAT: boot sector signature plus a sane sector size and a FAT type string.
    unsigned bytes_per_sector = block[11] | (block[12] << 8);
    bool sane_sector = bytes_per_sector == 512 || bytes_per_sector == 1024 ||
                       bytes_per_sector == 2048 || bytes_per_sector == 4096;
    if (block[510] == 0x55 && block[511] == 0xAA && sane_sector) {
        bool fat32 = std::memcmp(block + 82, "FAT32   ", 8) == 0;
        bool fat16 = std::memcmp(block + 54, "FAT12   ", 8) == 0 || std::memcmp(block + 54, "FAT16   ", 8) == 0;
        if (fat32 || fat16) {
            info.type = "vfat";
            info.label = printable_label(block + (fat32 ? 71 : 43), 11);
            if (info.label == "NO NAME") info.label.clear();
            return info;
        }
    }
    return info;
}

std::vector<Disk> list_disks(const std::string& sys_block, const std::string& dev_dir,
                             const std::string& proc_mounts) {
    std::vector<Disk> disks;
    std::map<std::string, std::string> mounts = read_mounts(proc_mounts);

    std::vector<std::string> names;
    if (DIR* dir = opendir(sys_block.c_str())) {
        while (dirent* entry = readdir(dir)) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            if (starts_with(name, "loop") || starts_with(name, "ram") || starts_with(name, "zram") ||
                starts_with(name, "nbd")) continue;
            names.push_back(name);
        }
        closedir(dir);
    }
    std::sort(names.begin(), names.end());

    for (const std::string& name : names) {
        std::string base = sys_block + "/" + name;
        Disk disk;
        disk.name = name;
        disk.path = dev_dir + "/" + name;
        disk.size_bytes = read_u64(base + "/size") * 512;
        disk.model = read_text(base + "/device/model");
        disk.removable = read_text(base + "/removable") == "1";
        disk.read_only = read_text(base + "/ro") == "1";
        disk.optical = starts_with(name, "sr");
        if (disk.size_bytes == 0 && !disk.optical) continue;

        if (!disk.optical) {
            FsInfo fs = detect_fs(disk.path);
            disk.fstype = fs.type;
            disk.label = fs.label;
        }
        disk.mountpoint = mount_point_for(mounts, disk.path);

        std::vector<std::string> parts;
        if (DIR* dir = opendir(base.c_str())) {
            while (dirent* entry = readdir(dir)) {
                std::string child = entry->d_name;
                if (child.size() <= name.size() || !starts_with(child, name)) continue;
                if (access((base + "/" + child + "/partition").c_str(), F_OK) != 0) continue;
                parts.push_back(child);
            }
            closedir(dir);
        }
        std::sort(parts.begin(), parts.end(), [](const std::string& a, const std::string& b) {
            if (a.size() != b.size()) return a.size() < b.size();
            return a < b;
        });
        for (const std::string& child : parts) {
            Partition part;
            part.name = child;
            part.path = dev_dir + "/" + child;
            part.size_bytes = read_u64(base + "/" + child + "/size") * 512;
            FsInfo fs = detect_fs(part.path);
            part.fstype = fs.type;
            part.label = fs.label;
            part.mountpoint = mount_point_for(mounts, part.path);
            disk.partitions.push_back(part);
        }
        disks.push_back(disk);
    }
    return disks;
}

Result mount_device(const std::string& dev_path, std::string& mountpoint_out) {
    if (!valid_device_path(dev_path)) return fail("invalid device path");

    std::map<std::string, std::string> mounts = read_mounts("/proc/mounts");
    std::string existing = mount_point_for(mounts, dev_path);
    if (!existing.empty()) {
        mountpoint_out = existing;
        return success();
    }

    FsInfo fs = detect_fs(dev_path);
    if (fs.type.empty()) return fail("no supported filesystem found (format the device first)");

    std::string name = base_name(dev_path);
    std::string target = mount_root() + "/" + name;
    ensure_dir(mount_root());
    ensure_dir(target);

    unsigned long flags = MS_NOATIME;
    if (read_text("/sys/class/block/" + name + "/ro") == "1") flags |= MS_RDONLY;
    const char* fstype = fs.type == "vfat" ? "vfat" : "ext4";
    const char* data = fs.type == "vfat" ? "utf8=1" : nullptr;   // keeps FAT case-insensitive, unlike iocharset=utf8
    if (mount(dev_path.c_str(), target.c_str(), fstype, flags, data) != 0) {
        Result r = errno_fail("mount " + dev_path);
        rmdir(target.c_str());
        return r;
    }
    mountpoint_out = target;
    return success();
}

Result unmount_path(const std::string& mountpoint) {
    std::string resolved;
    Result r = resolve_allowed(mountpoint, true, resolved);
    if (!r.ok) return r;

    char root_buf[PATH_MAX];
    if (!realpath(mount_root().c_str(), root_buf)) return errno_fail("mount root");
    std::string parent = resolved.substr(0, resolved.find_last_of('/'));
    if (parent.empty()) parent = "/";
    if (parent != root_buf) return fail("not a mount point under " + mount_root());

    sync();
    if (umount2(resolved.c_str(), 0) != 0) return errno_fail("umount " + mountpoint);
    rmdir(resolved.c_str());
    return success();
}

Result wipe_partition_tables(const std::string& target) {
    int fd = open(target.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) return errno_fail(target);
    off_t size = lseek(fd, 0, SEEK_END);
    if (size <= 0) {
        close(fd);
        return fail("cannot determine the size of " + target);
    }

    constexpr off_t kWipe = 1024 * 1024;
    const std::vector<char> zeros(static_cast<size_t>(kWipe), 0);
    auto zero_at = [&](off_t offset, off_t length) {
        off_t done = 0;
        while (done < length) {
            ssize_t n = pwrite(fd, zeros.data(), static_cast<size_t>(length - done), offset + done);
            if (n <= 0) return false;
            done += n;
        }
        return true;
    };
    bool ok = zero_at(0, std::min(size, kWipe));
    if (ok && size > 2 * kWipe) ok = zero_at(size - kWipe, kWipe);   // GPT backup header
    if (!ok) {
        Result err = errno_fail("wipe " + target);
        close(fd);
        return err;
    }
    fsync(fd);
    ioctl(fd, BLKRRPART);   // only meaningful for real disks; ignored for image files
    close(fd);
    return success();
}

Result run_mkfs(const std::string& target, const std::string& fs, const std::string& label) {
    std::vector<std::string> args;
    const char* tool = nullptr;
    if (fs == "ext4") {
        tool = find_tool("mkfs.ext4");
        args = {"mkfs.ext4", "-F", "-q"};
        std::string clean = sanitize_fs_label(label, 16, false);
        if (!clean.empty()) { args.push_back("-L"); args.push_back(clean); }
    } else if (fs == "vfat") {
        tool = find_tool("mkfs.vfat");
        args = {"mkfs.vfat", "--mbr=n"}; // no fake MBR/partition when formatting a whole disk
        std::string clean = sanitize_fs_label(label, 11, true);
        if (!clean.empty()) { args.push_back("-n"); args.push_back(clean); }
    } else {
        return fail("unsupported filesystem: " + fs);
    }
    if (!tool) return fail("mkfs." + fs + " is not installed in this system");
    args.push_back(target);

    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(&a[0]);
    argv.push_back(nullptr);

    int pipe_fd[2];
    if (pipe(pipe_fd) != 0) return errno_fail("pipe");
    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return errno_fail("fork");
    }
    if (pid == 0) {
        dup2(pipe_fd[1], STDOUT_FILENO);
        dup2(pipe_fd[1], STDERR_FILENO);
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        execv(tool, argv.data());
        _exit(127);
    }
    close(pipe_fd[1]);
    std::string output;
    char chunk[256];
    ssize_t n;
    while ((n = read(pipe_fd[0], chunk, sizeof(chunk))) > 0) {
        if (output.size() < 400) output.append(chunk, static_cast<size_t>(n));
    }
    close(pipe_fd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return success();
    output = trim_text(output);
    return fail("mkfs." + fs + " failed" + (output.empty() ? "" : ": " + output));
}

Result format_device(const std::string& dev_path, const std::string& fs,
                     const std::string& label, const std::string& confirm) {
    if (!valid_device_path(dev_path)) return fail("invalid device path");
    if (fs != "ext4" && fs != "vfat") return fail("unsupported filesystem: " + fs);
    std::string name = base_name(dev_path);
    if (confirm != name) return fail("confirmation does not match the device name");

    std::vector<Disk> disks = list_disks();
    const Disk* disk = nullptr;
    const Partition* partition = nullptr;
    for (const Disk& d : disks) {
        if (d.path == dev_path) disk = &d;
        for (const Partition& p : d.partitions) {
            if (p.path == dev_path) { disk = &d; partition = &p; }
        }
    }
    if (!disk) return fail("device not found: " + dev_path);
    if (disk->optical) return fail("optical drives cannot be formatted");
    if (disk->read_only) return fail("device is read-only");

    if (partition) {
        if (!partition->mountpoint.empty()) return fail("unmount " + dev_path + " first");
    } else {
        if (!disk->mountpoint.empty()) return fail("unmount " + dev_path + " first");
        for (const Partition& p : disk->partitions) {
            if (!p.mountpoint.empty()) return fail("unmount " + p.path + " first");
        }
    }
    if (!partition) {
        // A whole disk may carry a partition table; mkfs alone would leave stale partitions
        // (for example the GPT backup) overlapping the new filesystem.
        Result wiped = wipe_partition_tables(dev_path);
        if (!wiped.ok) return wiped;
    }
    return run_mkfs(dev_path, fs, label);
}

Result resolve_existing(const std::string& path, std::string& resolved) {
    return resolve_allowed(path, true, resolved);
}

Result list_dir(const std::string& path, std::vector<DirEntry>& out, bool& truncated) {
    out.clear();
    truncated = false;
    std::string resolved;
    Result r = resolve_allowed(path, true, resolved);
    if (!r.ok) return r;

    DIR* dir = opendir(resolved.c_str());
    if (!dir) return errno_fail(path);
    while (dirent* entry = readdir(dir)) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        if (out.size() >= kMaxDirEntries) { truncated = true; break; }
        DirEntry e;
        e.name = name;
        struct stat st;
        if (lstat((resolved + "/" + name).c_str(), &st) == 0) {
            e.is_dir = S_ISDIR(st.st_mode);
            e.size = e.is_dir ? 0 : static_cast<uint64_t>(st.st_size);
        }
        out.push_back(e);
    }
    closedir(dir);

    std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir;
        std::string la = a.name, lb = b.name;
        std::transform(la.begin(), la.end(), la.begin(), [](unsigned char c) { return std::tolower(c); });
        std::transform(lb.begin(), lb.end(), lb.begin(), [](unsigned char c) { return std::tolower(c); });
        return la < lb;
    });
    return success();
}

Result read_file(const std::string& path, size_t max_bytes, std::string& out) {
    out.clear();
    std::string resolved;
    Result r = resolve_allowed(path, true, resolved);
    if (!r.ok) return r;
    if (max_bytes == 0 || max_bytes > kMaxFileBytes) max_bytes = kMaxFileBytes;

    int fd = open(resolved.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return errno_fail(path);
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return fail("not a regular file");
    }
    out.resize(max_bytes);
    size_t total = 0;
    while (total < max_bytes) {
        ssize_t n = read(fd, &out[total], max_bytes - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            Result err = errno_fail(path);
            close(fd);
            out.clear();
            return err;
        }
        if (n == 0) break;
        total += static_cast<size_t>(n);
    }
    close(fd);
    out.resize(total);
    return success();
}

Result write_file(const std::string& path, const std::string& data) {
    if (data.size() > kMaxFileBytes) return fail("file is larger than 1 MiB");
    std::string resolved;
    Result r = resolve_allowed(path, false, resolved);
    if (!r.ok) return r;

    int fd = open(resolved.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (fd < 0) return errno_fail(path);
    size_t total = 0;
    while (total < data.size()) {
        ssize_t n = write(fd, data.data() + total, data.size() - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            Result err = errno_fail(path);
            close(fd);
            return err;
        }
        total += static_cast<size_t>(n);
    }
    if (fsync(fd) != 0) {
        Result err = errno_fail(path);
        close(fd);
        return err;
    }
    close(fd);
    return success();
}

Result make_dir(const std::string& path) {
    std::string resolved;
    Result r = resolve_allowed(path, false, resolved);
    if (!r.ok) return r;
    if (mkdir(resolved.c_str(), 0755) != 0) return errno_fail(path);
    return success();
}

Result remove_path(const std::string& path) {
    std::string resolved;
    Result r = resolve_allowed(path, true, resolved);
    if (!r.ok) return r;

    // Children of the mount root are mount points; never remove them from here.
    char root_buf[PATH_MAX];
    if (!realpath(mount_root().c_str(), root_buf)) return errno_fail("mount root");
    std::string parent = resolved.substr(0, resolved.find_last_of('/'));
    if (parent.empty()) parent = "/";
    if (resolved == root_buf || parent == root_buf) return fail("refusing to remove a mount point");

    struct stat st;
    if (lstat(resolved.c_str(), &st) != 0) return errno_fail(path);
    int rc = S_ISDIR(st.st_mode) ? rmdir(resolved.c_str()) : unlink(resolved.c_str());
    if (rc != 0) return errno_fail(path);
    return success();
}

#else // !__linux__

namespace {
Result unsupported() {
    Result r;
    r.error = "storage is only supported on Linux";
    return r;
}
}

FsInfo detect_fs(const std::string&) { return FsInfo(); }
std::vector<Disk> list_disks(const std::string&, const std::string&, const std::string&) { return {}; }
Result mount_device(const std::string&, std::string&) { return unsupported(); }
Result unmount_path(const std::string&) { return unsupported(); }
Result wipe_partition_tables(const std::string&) { return unsupported(); }
Result run_mkfs(const std::string&, const std::string&, const std::string&) { return unsupported(); }
Result format_device(const std::string&, const std::string&, const std::string&, const std::string&) { return unsupported(); }
Result resolve_existing(const std::string&, std::string& resolved) { resolved.clear(); return unsupported(); }
Result list_dir(const std::string&, std::vector<DirEntry>& out, bool& truncated) { out.clear(); truncated = false; return unsupported(); }
Result read_file(const std::string&, size_t, std::string& out) { out.clear(); return unsupported(); }
Result write_file(const std::string&, const std::string&) { return unsupported(); }
Result make_dir(const std::string&) { return unsupported(); }
Result remove_path(const std::string&) { return unsupported(); }

#endif

} // namespace storage
} // namespace linweb
