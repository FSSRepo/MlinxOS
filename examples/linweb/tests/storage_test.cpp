// Host-side checks for src/system/storage.cpp (needs mkfs.ext4 and mkfs.vfat).
//   g++ -std=c++17 -Isrc tests/storage_test.cpp src/system/storage.cpp -o /tmp/storage_test && /tmp/storage_test
#include "system/storage.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <sys/stat.h>

using namespace linweb::storage;

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

static void write_text(const std::string& path, const std::string& text) {
    std::ofstream out(path);
    out << text;
}

static void make_image(const std::string& path, long megabytes) {
    std::string cmd = "truncate -s " + std::to_string(megabytes) + "M '" + path + "'";
    if (std::system(cmd.c_str()) != 0) std::printf("truncate failed\n");
}

int main() {
    char tmpl[] = "/tmp/linweb-storage-XXXXXX";
    std::string base = mkdtemp(tmpl);

    // --- filesystem detection and mkfs ---------------------------------
    std::string ext_img = base + "/ext.img";
    make_image(ext_img, 16);
    CHECK(detect_fs(ext_img).type.empty());
    Result r = run_mkfs(ext_img, "ext4", "bad label!");
    CHECK(r.ok);
    FsInfo info = detect_fs(ext_img);
    CHECK(info.type == "ext4");
    CHECK(info.label == "bad_label");

    std::string fat_img = base + "/fat.img";
    make_image(fat_img, 16);
    r = run_mkfs(fat_img, "vfat", "testfat");
    CHECK(r.ok);
    info = detect_fs(fat_img);
    CHECK(info.type == "vfat");
    CHECK(info.label == "TESTFAT");

    CHECK(!run_mkfs(fat_img, "ntfs", "").ok);

    // A whole-disk format must not leave a fake MBR partition behind (mkfs.fat --mbr=n).
    {
        std::string vfat_whole = base + "/whole.img";
        make_image(vfat_whole, 16);
        CHECK(run_mkfs(vfat_whole, "vfat", "WHOLE").ok);
        std::ifstream in(vfat_whole, std::ios::binary);
        char sector[512] = {0};
        in.read(sector, sizeof(sector));
        bool entries_empty = true;
        for (int i = 446; i < 510; ++i) if (sector[i] != 0) entries_empty = false;
        CHECK(entries_empty);
    }

    // wipe_partition_tables clears the first and last MiB (MBR, GPT, GPT backup) and nothing else.
    {
        std::string wipe_img = base + "/wipe.img";
        make_image(wipe_img, 8);
        system(("printf 'XXXXXXXX' | dd of=" + wipe_img + " bs=1 seek=510 conv=notrunc 2>/dev/null").c_str());
        system(("printf 'GPTBACKUP' | dd of=" + wipe_img + " bs=1 seek=8388000 conv=notrunc 2>/dev/null").c_str());
        system(("printf 'KEEPME' | dd of=" + wipe_img + " bs=1 seek=4194304 conv=notrunc 2>/dev/null").c_str());
        CHECK(wipe_partition_tables(wipe_img).ok);
        std::ifstream in(wipe_img, std::ios::binary);
        std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(all.size() == 8u * 1024 * 1024);
        CHECK(all.compare(510, 8, std::string(8, '\0')) == 0);          // MBR area cleared
        CHECK(all.compare(8388000, 9, std::string(9, '\0')) == 0);      // trailing MiB cleared
        CHECK(all.compare(4194304, 6, "KEEPME") == 0);                   // data in the middle untouched
        CHECK(!wipe_partition_tables(base + "/missing.img").ok);
    }
    CHECK(detect_fs(base + "/missing.img").type.empty());

    // --- device enumeration against a fake sysfs -------------------------
    std::string sys = base + "/sys/block";
    std::string dev = base + "/dev";
    system(("mkdir -p " + sys + "/vda/vda1 " + sys + "/vda/device " + sys + "/loop0 " + sys + "/sr0 " + dev).c_str());
    write_text(sys + "/vda/size", "32768\n");          // 16 MiB
    write_text(sys + "/vda/removable", "0\n");
    write_text(sys + "/vda/ro", "0\n");
    write_text(sys + "/vda/device/model", "QEMU HARDDISK  \n");
    write_text(sys + "/vda/vda1/partition", "1\n");
    write_text(sys + "/vda/vda1/size", "16384\n");
    write_text(sys + "/loop0/size", "2048\n");
    write_text(sys + "/sr0/size", "0\n");
    write_text(sys + "/sr0/removable", "1\n");
    system(("cp " + fat_img + " " + dev + "/vda").c_str());
    system(("cp " + ext_img + " " + dev + "/vda1").c_str());
    write_text(base + "/mounts", "proc /proc proc rw 0 0\n" + dev + "/vda1 /mnt/vda1 ext4 rw 0 0\n");

    std::vector<Disk> disks = list_disks(sys, dev, base + "/mounts");
    CHECK(disks.size() == 2);                          // loop0 skipped, sr0 and vda kept
    if (disks.size() == 2) {
        CHECK(disks[0].name == "sr0" && disks[0].optical && disks[0].fstype.empty());
        const Disk& vda = disks[1];
        CHECK(vda.name == "vda");
        CHECK(vda.size_bytes == 32768ull * 512);
        CHECK(vda.model == "QEMU HARDDISK");
        CHECK(vda.fstype == "vfat" && vda.label == "TESTFAT");
        CHECK(vda.partitions.size() == 1);
        if (vda.partitions.size() == 1) {
            CHECK(vda.partitions[0].name == "vda1");
            CHECK(vda.partitions[0].size_bytes == 16384ull * 512);
            CHECK(vda.partitions[0].fstype == "ext4");
            CHECK(vda.partitions[0].mountpoint == "/mnt/vda1");
        }
    }

    // --- device safety checks --------------------------------------------
    CHECK(!format_device("/dev/../etc/passwd", "ext4", "", "passwd").ok);
    CHECK(!format_device("/dev/vda", "ntfs", "", "vda").ok);
    CHECK(!format_device("/dev/vda", "ext4", "", "wrong").ok);       // confirmation mismatch
    CHECK(!format_device("/dev/definitely-not-a-device", "ext4", "", "definitely-not-a-device").ok);
    std::string mounted;
    CHECK(!mount_device("/etc/passwd", mounted).ok);
    CHECK(!mount_device("/dev/../etc/passwd", mounted).ok);

    // --- confined file operations ---------------------------------------
    std::string root = base + "/mnt";
    system(("mkdir -p " + root + "/vda1/sub").c_str());
    set_mount_root(root);
    std::string file = root + "/vda1/hello.txt";
    CHECK(write_file(file, "hola linweb\n").ok);
    std::string content;
    CHECK(read_file(file, 0, content).ok && content == "hola linweb\n");
    CHECK(read_file(file, 4, content).ok && content == "hola");
    CHECK(write_file(root + "/vda1/b.txt", "x").ok);
    CHECK(make_dir(root + "/vda1/Alpha").ok);

    std::vector<DirEntry> entries;
    bool truncated = true;
    CHECK(list_dir(root + "/vda1", entries, truncated).ok);
    CHECK(!truncated);
    CHECK(entries.size() == 4);
    if (entries.size() == 4) {                         // directories first, then case-insensitive names
        CHECK(entries[0].name == "Alpha" && entries[0].is_dir);
        CHECK(entries[1].name == "sub" && entries[1].is_dir);
        CHECK(entries[2].name == "b.txt" && !entries[2].is_dir && entries[2].size == 1);
        CHECK(entries[3].name == "hello.txt" && entries[3].size == 12);
    }

    CHECK(!read_file(root + "/vda1/../../ext.img", 0, content).ok);   // .. escape
    CHECK(!read_file("/etc/passwd", 0, content).ok);                   // outside the root
    CHECK(!write_file("/tmp/linweb-escape.txt", "x").ok);
    CHECK(!write_file("relative.txt", "x").ok);
    system(("ln -s /etc " + root + "/vda1/etclink").c_str());
    CHECK(!read_file(root + "/vda1/etclink/passwd", 0, content).ok);   // symlink escape
    CHECK(!write_file(root + "/vda1/etclink/new.txt", "x").ok);

    CHECK(!remove_path(root + "/vda1").ok);                            // mount points are protected
    CHECK(!remove_path(root).ok);
    CHECK(!remove_path(root + "/vda1/Alpha/missing").ok);
    CHECK(remove_path(root + "/vda1/Alpha").ok);                       // empty directory
    CHECK(remove_path(file).ok);
    CHECK(!read_file(file, 0, content).ok);
    CHECK(!unmount_path(root + "/vda1/sub").ok);                       // not a direct mount point child

    system(("rm -rf " + base).c_str());
    std::printf(g_failures ? "%d check(s) failed\n" : "all storage checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}
