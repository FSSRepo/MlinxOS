#!/bin/bash

echo "Binary name (e.g. my_app):"
read binary

echo "Arguments for the binary (e.g. index.html --fullscreen, leave empty to omit):"
read args

echo "Working directory (cwd) (e.g. /root, leave empty to omit):"
read cwd

if [ -z "$args" ] && [ -z "$cwd" ]; then
    # No cwd and no args: launch the binary directly
    path=/usr/bin/$binary
else
    # Generate startup script with cwd and/or args
    cat << EOF > output/target/usr/bin/startup.sh
#!/bin/sh
EOF

    if [ -n "$cwd" ]; then
cat << EOF >> output/target/usr/bin/startup.sh
cd $cwd || exit 1
EOF
    fi

cat << EOF >> output/target/usr/bin/startup.sh
exec /usr/bin/$binary $args
EOF

    chmod +x output/target/usr/bin/startup.sh
    path=/usr/bin/startup.sh
fi

mkdir -p output/target/etc/xdg/weston

cat << EOF > output/target/etc/xdg/weston/weston.ini
[core]
shell=kiosk.so

[autolaunch]
path=$path

restart=true
EOF
