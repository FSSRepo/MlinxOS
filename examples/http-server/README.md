# Steward HTTP

Minimal sequential HTTP server in C, listening on port 8080 by default.
GET and HEAD are supported on `/` (HTML) and `/health` (JSON).
No external libraries, file serving or command execution.

Build with the target toolchain:

```sh
output/host/bin/x86_64-buildroot-linux-gnu-gcc -std=c11 -O2 -Wall -Wextra -Werror server.c -o steward-httpd
```

Install the executable as `/usr/sbin/steward-httpd` and the executable
init script as `/etc/init.d/S45httpd`. Include both in the Buildroot overlay.
Open `http://<guest-ip>:8080/` after boot.
