echo "Binary name (e.g. my_app):"
read binary

echo "Arguments for the binary (e.g. index.html --fullscreen, leave empty to omit):"
read args

echo "Working directory (cwd) (e.g. /root, leave empty to omit):"
read cwd

cat << EOF > output/target/etc/init.d/S90app
#!/bin/sh

PIDFILE=/var/run/game-app.pid
APP_BIN=/usr/bin/$binary

start() {
    export DISPLAY=:0
    export HOME=/root

    for i in \$(seq 1 30); do
        if [ -S /tmp/.X11-unix/X0 ]; then
            break
        fi
        sleep 0.4
    done
EOF

if [ -n "$cwd" ]; then
cat << EOF >> output/target/etc/init.d/S90app
    cd $cwd
EOF
fi

cat << EOF >> output/target/etc/init.d/S90app
    start-stop-daemon -S -b -m -p \$PIDFILE --exec \$APP_BIN -- $args
}

stop() {
    start-stop-daemon -K -p \$PIDFILE
}

case "\$1" in
    start)
        start
        ;;
    stop)
        stop
        ;;
    restart)
        stop
        sleep 1
        start
        ;;
    *)
        echo "Usage: \$0 {start|stop|restart}"
        exit 1
esac

exit 0
EOF

chmod +x output/target/etc/init.d/S90app
