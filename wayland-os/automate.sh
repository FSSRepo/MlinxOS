#!/bin/bash

set -euo pipefail

BINARY=
APP_ARGS=
CWD=
CLI_MODE=0
usage() {
    cat <<'EOF'
Usage: bash automate.sh [options]

Options:
  -b, --binary FILE|NAME  Executable to copy into output/target/usr/bin, or
                          the name of an executable already there.
  -a, --args STRING       Arguments passed to the app by the startup script.
  -c, --cwd DIRECTORY     Working directory inside the target system.
  -h, --help              Show this help.

Example:
  bash automate.sh --binary /tmp/cube --args "--fullscreen" --cwd /root
EOF
}
while (($#)); do
    case "$1" in
        -b|--binary) (($# >= 2)) || { echo "ERROR: --binary requires a value." >&2; exit 2; }; BINARY=$2; CLI_MODE=1; shift 2 ;;
        -a|--args) (($# >= 2)) || { echo "ERROR: --args requires a value." >&2; exit 2; }; APP_ARGS=$2; CLI_MODE=1; shift 2 ;;
        -c|--cwd) (($# >= 2)) || { echo "ERROR: --cwd requires a value." >&2; exit 2; }; CWD=$2; CLI_MODE=1; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "ERROR: unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ -z "$BINARY" ]]; then
    (( CLI_MODE == 0 )) || { echo "ERROR: --binary is required in CLI mode." >&2; exit 2; }
    read -r -p "Binary name or file to install (e.g. cube): " BINARY
    read -r -p "Arguments for the binary (leave empty for none): " APP_ARGS
    read -r -p "Working directory inside the target (leave empty for none): " CWD
fi

TARGET_BIN=output/target/usr/bin
[[ -d output/target ]] || { echo "ERROR: output/target not found. Run the Buildroot build first." >&2; exit 1; }
mkdir -p "$TARGET_BIN" output/target/etc/xdg/weston

if [[ -f "$BINARY" ]]; then
    BIN_NAME=${BINARY##*/}
    [[ "$BIN_NAME" =~ ^[A-Za-z0-9._+-]+$ ]] || { echo "ERROR: unsupported executable filename: $BIN_NAME" >&2; exit 2; }
    cp "$BINARY" "$TARGET_BIN/$BIN_NAME"
    chmod 0755 "$TARGET_BIN/$BIN_NAME"
else
    BIN_NAME=${BINARY##*/}
    [[ "$BIN_NAME" =~ ^[A-Za-z0-9._+-]+$ ]] || { echo "ERROR: use a simple executable name or a file path." >&2; exit 2; }
    [[ -x "$TARGET_BIN/$BIN_NAME" ]] || { echo "ERROR: $TARGET_BIN/$BIN_NAME not found or not executable." >&2; exit 1; }
fi

shell_quote() {
    local escaped
    escaped=$(printf '%s' "$1" | sed "s/'/'\\\\''/g")
    printf "'%s'" "$escaped"
}

if [[ -z "$APP_ARGS" && -z "$CWD" ]]; then
    APP_PATH="/usr/bin/$BIN_NAME"
    rm -f "$TARGET_BIN/startup.sh"
else
    {
        printf '#!/bin/sh\n'
        if [[ -n "$CWD" ]]; then
            printf 'cd %s || exit 1\n' "$(shell_quote "$CWD")"
        fi
        printf 'exec /usr/bin/%s' "$BIN_NAME"
        [[ -z "$APP_ARGS" ]] || printf ' %s' "$APP_ARGS"
        printf '\n'
    } > "$TARGET_BIN/startup.sh"
    chmod 0755 "$TARGET_BIN/startup.sh"
    APP_PATH=/usr/bin/startup.sh
fi

{
    printf '[core]\nshell=kiosk.so\n\n[autolaunch]\npath=%s\nrestart=true\n' "$APP_PATH"
} > output/target/etc/xdg/weston/weston.ini

echo "Installed /usr/bin/$BIN_NAME and configured Weston autolaunch at $APP_PATH."
