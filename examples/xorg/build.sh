[ -d bin ] || mkdir bin
g++ cube.cpp -o bin/cube -lX11 -lEGL -lGLESv2 -lm -Ilibs
gcc triangle.c -o bin/triangle -lX11 -lEGL -lGLESv2 -lm
