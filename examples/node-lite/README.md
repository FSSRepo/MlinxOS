# node-lite

QuickJS plus a small Node.js compatibility layer, to write servers in JavaScript on MLinxOS without
Node itself (about 2.5 MB with TLS, only libc/libm needed, builds in about a minute).

```
bash tests/smoke.sh        # after building; starts the example servers and checks them with curl
build/node-lite example/server.js 8080        # CommonJS
build/node-lite example/server.mjs 8081       # ES modules
```

## Build

POSIX only (sockets use epoll), so on Linux or with the Buildroot toolchain:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target node-lite

# for the MLinxOS target
cmake -S . -B build-target -DCMAKE_TOOLCHAIN_FILE=$BUILDROOT/output/host/share/buildroot/toolchainfile.cmake
```

`../external/quickjs` (shared with linweb) is QuickJS-ng; `lib/node-lite.js` is embedded into the binary at configure time.

## What "importing Node libraries" means here

`require('x')`, `import x from 'x'` and `import { y } from 'node:x'` all resolve, in this order, to:

1. a builtin: `http`, `dns`, `fs`, `fs/promises`, `path`, `os`, `events`, `util`, `assert`, `url`, `querystring`, `buffer`, `timers`, `process`;
2. a relative or absolute file (`.js`, `.cjs`, `.json`, `index.js`, or a directory's `package.json` `"main"`);
3. `node_modules/<name>` searched upwards from the requiring file (plain CommonJS packages).

Globals: `process`, `Buffer`, `setTimeout`/`setInterval`/`setImmediate` and their `clear*`, `URL`, `URLSearchParams`,
`TextEncoder`/`TextDecoder`, `fetch`, `Headers`, `Response`, `AbortController`/`AbortSignal`, `queueMicrotask`, `structuredClone`, `console` (`log`, `error`, `warn`, `info`, `debug`).

QuickJS' own modules are reachable as `qjs:std` and `qjs:os` only from C, so the name `os` is Node's.

## http

`http.createServer(handler)` with `listen(port[, host][, cb])`, `close()`, `address()`, and the usual
`req.method/url/headers`, `req.on('data'|'end')`, `res.statusCode/setHeader/writeHead/write/end`.
Limits: HTTP/1.1 with one request per connection (`Connection: close`), no chunked request bodies (answers 501),
headers up to 64 KB and bodies up to 16 MB, no HTTPS server, and no `http.request`/`http.get` client (use `fetch`).

## dns

`dns.lookup`, `dns.resolve4` / `dns.resolve` (callbacks) and `dns.promises.lookup|resolve4`, IPv4 only. They use
`getaddrinfo` on a helper thread, so `/etc/hosts`, `/etc/resolv.conf` and `libnss_dns` apply and the event loop keeps
running. On MLinxOS `/etc/resolv.conf` is written by `udhcpc` once Wi-Fi gets an address.

## fetch

```js
const res = await fetch('https://www.google.com', { headers: { 'accept-language': 'en' } });
console.log(res.status, res.headers.get('content-type'), (await res.text()).length);
```

`http://` and `https://`, methods, headers, string/Buffer/`URLSearchParams` bodies, redirects (`redirect: 'follow' |
'manual' | 'error'`, up to 20, `Authorization`/`Cookie` dropped across origins), `AbortController`, and the non-standard
`timeout` option (ms, default 60000, `0` disables). `Response` has `status`, `ok`, `headers`, `url`, `redirected`,
`text()`, `json()`, `arrayBuffer()`, `bytes()`, `clone()`. Bodies are buffered (no streams, `Request` objects or
cookies), servers must answer `identity` content (no gzip/br), and failures throw `TypeError: fetch failed` with the
cause in `error.cause` (`ENOTFOUND`, `ECONNREFUSED`, certificate errors, ...).

HTTPS uses a statically linked copy of mbedTLS 3.6.4 (`../external/mbedtls`, TLS 1.2/1.3) and Mozilla's root certificates
embedded from `../external/ca/cacert.pem`, so the system needs no TLS library or CA package. Certificates are verified
(expired, self-signed and wrong-host ones are rejected) and the system clock must be roughly right. `SSL_CERT_FILE`
adds extra CAs and `NODE_TLS_REJECT_UNAUTHORIZED=0` disables verification, as in Node. To refresh the CAs, replace
`../external/ca/cacert.pem` (from https://curl.se/ca/cacert.pem) and rebuild.

```
build/node-lite example/fetch.js https://www.google.com
build/node-lite example/fetch.js http://google.com        # follows the redirect to https
```

## Example: vector database with OpenAI embeddings

`example/vectordb/` is a small vector store plus a script that embeds a set of texts in batches with `fetch` and the
OpenAI API (key from `OPENAI_API_KEY`). It also shows a real run and how to read the scores; see
[`example/vectordb/README.md`](example/vectordb/README.md). Its test, `tests/vectordb_test.js`, needs no key.

## Not supported

npm packages that need native addons, `child_process`, `net`, `stream`, `worker_threads`, `crypto`, `zlib`, `fs.createReadStream`.
The `fs` callback and promise forms run the synchronous call and deliver the result on the next tick.

## Notes

The vendored QuickJS `js_os_poll` only dispatches among the first N polled fds (N = number of ready fds), so
with more than one `os.setReadHandler` fd a ready socket can be ignored. node-lite registers a single epoll fd
and dispatches itself (`src/net_native.c`, `poller` in `lib/node-lite.js`) instead of patching the library.

## In the MLinxOS image

`bash build.sh [BUILDROOT_DIR]` cross-compiles node-lite with the Buildroot toolchain and installs it as
`/usr/bin/node-lite`, with the examples, the tests and this README under `/root/node-lite` (the generated
`vectors.json` and `docs.vec.json` are left out). linweb runs it from there: `linweb.node` in scripts and
System > Node Lite in the PS3 menu (see `examples/linweb-system-os/README.md`). The image brings the network up by itself:
`S41ethernet` runs `udhcpc` on wired interfaces (the kernel has `virtio-net` and `8139cp`) and System > Network
Settings connects Wi-Fi. In QEMU, `make-iso.sh --qemu` adds a virtio NIC with NAT, so the guest gets 10.0.2.15 and
reaches the internet through the host.
