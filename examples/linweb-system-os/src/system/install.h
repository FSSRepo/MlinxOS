#pragma once
#include <string>

namespace linweb {
namespace install {

struct Result {
    bool ok = false;
    std::string error;
};

struct Status {
    bool running = false;
    bool done = false;
    int exit_code = -1;   // valid once done
    std::string log;      // what /usr/sbin/mlinx-install has printed so far
};

// Starts /usr/sbin/mlinx-install on `disk` (for example "/dev/sda") in the background. `confirm`
// must equal the disk's name ("sda"). The install erases the whole disk.
Result start(const std::string& disk, const std::string& confirm);

// Progress of the last install started with start().
Status status();

// True when the system runs from an installed disk (its root is a disk partition), false on the live
// medium, where the root is the initramfs.
bool runs_from_disk();

// True when this image includes the installer (built without --no-installer).
bool available();

} // namespace install
} // namespace linweb
