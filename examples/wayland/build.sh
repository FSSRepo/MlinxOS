[ -d bin ] || mkdir bin
gcc -c libs/xdg-shell-client-protocol.c -o bin/xdg-shell-client-protocol.o -I.
g++ 3d-wayland.cpp bin/xdg-shell-client-protocol.o -o bin/cube -lwayland-client -lEGL -lGLESv2 -lwayland-egl -Ilibs

gcc weston-app.c libs/xdg-shell-client-protocol.c -o bin/weston-green -lwayland-client -lEGL -lGLESv2 -Ilibs -lwayland-egl