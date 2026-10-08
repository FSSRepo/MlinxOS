#!/usr/bin/env python3
import subprocess
import sys
import shutil
import os

def run_ldd(target):
    try:
        output = subprocess.check_output(["ldd", target], text=True)
    except subprocess.CalledProcessError:
        return []

    libs = set()

    for line in output.splitlines():
        line = line.strip()

        # libfoo.so => /path/libfoo.so (0x...)
        if "=>" in line:
            parts = line.split("=>")
            path = parts[1].strip().split()[0]
            if path != "not":
                libs.add(path)

        # /lib64/ld-linux-x86-64.so.2 (0x...)
        elif line.startswith("/"):
            libs.add(line.split()[0])

    return libs

def copy_with_structure(src, root_dir):
    dest = os.path.join(root_dir, src.lstrip("/"))
    os.makedirs(os.path.dirname(dest), exist_ok=True)

    if not os.path.exists(dest):
        print(f"Copying {src}")
        shutil.copy2(src, dest)
        return dest
    return None

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <binary>")
        sys.exit(1)

    binary = os.path.abspath(sys.argv[1])

    if not os.path.isfile(binary):
        print("The binary does not exist")
        sys.exit(1)

    root_dir = os.path.dirname(binary)

    processed = set()
    pending = set(run_ldd(binary))

    while pending:
        lib = pending.pop()

        if lib in processed or not os.path.exists(lib):
            continue

        processed.add(lib)

        copied = copy_with_structure(lib, root_dir)
        if copied:
            new_libs = run_ldd(lib)
            pending.update(new_libs - processed)

    print("\n✔ Dependencies copied:", len(processed))

if __name__ == "__main__":
    main()
