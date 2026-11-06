#!/bin/bash
# Starts the example servers (CommonJS and ES module) and checks them with curl.
# Usage: bash tests/smoke.sh [path/to/node-lite]
set -u
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BIN=${1:-"$ROOT/build/node-lite"}
CJS_PORT=${CJS_PORT:-18080}
ESM_PORT=${ESM_PORT:-18081}
LOG=$(mktemp -d)
fail=0

check() { # description, expected substring, actual
    if [[ "$3" == *"$2"* ]]; then echo "ok   $1"; else echo "FAIL $1: expected '$2', got '$3'"; fail=1; fi
}
get() { curl -m 4 -s "$@"; }

"$BIN" "$ROOT/example/server.js" "$CJS_PORT" >"$LOG/cjs.log" 2>&1 & CJS=$!
"$BIN" "$ROOT/example/server.mjs" "$ESM_PORT" >"$LOG/esm.log" 2>&1 & ESM=$!
trap 'kill $CJS $ESM 2>/dev/null; rm -rf "$LOG"' EXIT
sleep 1

check "require('./local') and node_modules package" "[ NODE-LITE ]" "$(get "localhost:$CJS_PORT/?name=Ana")"
check "query string through URL" "Hello, Ana!" "$(get "localhost:$CJS_PORT/?name=Ana")"
check "JSON response" '"uptime"' "$(get "localhost:$CJS_PORT/api/time")"
check "fs.readdirSync + statSync" '"name": "server.js"' "$(get "localhost:$CJS_PORT/api/files")"
check "POST body (data/end events)" '"b": [' "$(get -X POST -d '{"a":1,"b":[1,2]}' "localhost:$CJS_PORT/api/echo")"
check "invalid JSON is a 400" "400" "$(get -o /dev/null -w '%{http_code}' -X POST -d nope "localhost:$CJS_PORT/api/echo")"
check "404" "404" "$(get -o /dev/null -w '%{http_code}' "localhost:$CJS_PORT/nope")"
check "large body (200 KB)" '"received"' "$(head -c 200000 /dev/zero | tr '\0' 'a' | sed 's/^/"/; s/$/"/' | get -X POST --data-binary @- "localhost:$CJS_PORT/api/echo" | head -c 40)"
check "ES module import" "ES module server, request number 1" "$(get "localhost:$ESM_PORT/")"
check "import from 'node:fs' + 'node:path'" "module.exports" "$(get "localhost:$ESM_PORT/readme")"
check "state across requests" "request number 3" "$(get "localhost:$ESM_PORT/x")"

# a port in use makes listen() fail with EADDRINUSE and a non-zero exit code
out=$("$BIN" "$ROOT/example/server.js" "$CJS_PORT" 2>&1); rc=$?
check "EADDRINUSE reported" "EADDRINUSE" "$out"
check "non-zero exit code on listen failure" "1" "$rc"

# a script that requires a missing module fails with MODULE_NOT_FOUND
echo "require('does-not-exist')" > "$LOG/missing.js"
out=$("$BIN" "$LOG/missing.js" 2>&1); rc=$?
check "Cannot find module" "Cannot find module 'does-not-exist'" "$out"

# fetch and dns against a local server
out=$("$BIN" "$ROOT/tests/fetch_test.js" 2>&1); rc=$?
check "fetch/dns suite ($(grep -c '^ok' <<<"$out") passed)" "" "$out"
if [[ $rc -ne 0 ]]; then echo "$out" | grep -v '^ok'; fail=1; fi

# the real internet: google.com over DNS, HTTP (redirects to HTTPS) and HTTPS, and a bad certificate
if curl -m 5 -sI https://www.google.com >/dev/null 2>&1; then
    out=$("$BIN" "$ROOT/example/fetch.js" https://www.google.com 2>&1)
    check "dns + https://www.google.com" "200 OK  https://www.google.com/" "$out"
    out=$("$BIN" "$ROOT/example/fetch.js" http://google.com 2>&1)
    check "http://google.com follows the redirect" "(redirected)" "$out"
    echo "fetch('https://expired.badssl.com/').catch(e => console.log(e.cause.message))" > "$LOG/badcert.js"
    check "expired certificate is rejected" "certificate verification failed" "$("$BIN" "$LOG/badcert.js" 2>&1)"
else
    echo "skip internet checks (no connectivity)"
fi

exit $fail
