/* node-lite: a minimal Node.js compatibility layer for QuickJS.
   Provides require() / import for events, path, fs, os, util, assert, url, querystring, buffer, timers and http,
   and the globals process, Buffer, setTimeout & co, URL, TextEncoder/TextDecoder. */
(function (global) {
'use strict';

const std = global.__std, os = global.__os, net = global.__net, evalNamed = global.__evalNamed;
delete global.__std; delete global.__os; delete global.__net; delete global.__evalNamed;

/* QuickJS' e.stack has no "Error: message" header line, so print both. */
function errorText(e) {
    const text = e && e.stack ? String(e) + '\n' + e.stack : String(e);
    return e && e.cause ? text + '\nCaused by: ' + errorText(e.cause) : text;
}

const VERSION = '0.1.0';

/* ---------- errors ---------- */

const ERRNO = { 1: 'EPERM', 2: 'ENOENT', 9: 'EBADF', 11: 'EAGAIN', 13: 'EACCES', 17: 'EEXIST', 20: 'ENOTDIR',
    21: 'EISDIR', 22: 'EINVAL', 24: 'EMFILE', 32: 'EPIPE', 39: 'ENOTEMPTY', 98: 'EADDRINUSE', 99: 'EADDRNOTAVAIL',
    101: 'ENETUNREACH', 103: 'ECONNABORTED', 104: 'ECONNRESET', 110: 'ETIMEDOUT', 111: 'ECONNREFUSED', 113: 'EHOSTUNREACH' };
const ERRTEXT = { ECONNREFUSED: 'connection refused', ENETUNREACH: 'network is unreachable', EHOSTUNREACH: 'no route to host',
    ECONNRESET: 'connection reset by peer', ETIMEDOUT: 'connection timed out', ENOENT: 'no such file or directory', EACCES: 'permission denied', EEXIST: 'file already exists',
    ENOTDIR: 'not a directory', EISDIR: 'illegal operation on a directory', ENOTEMPTY: 'directory not empty',
    EADDRINUSE: 'address already in use', EPERM: 'operation not permitted', EINVAL: 'invalid argument' };

function sysError(errno, syscall, target) {
    const code = ERRNO[Math.abs(errno)] || 'E' + Math.abs(errno);
    const err = new Error(`${code}: ${ERRTEXT[code] || 'system error'}, ${syscall}${target ? " '" + target + "'" : ''}`);
    err.errno = -Math.abs(errno); err.code = code; err.syscall = syscall;
    if (target) err.path = target;
    return err;
}

/* ---------- utf8 / Buffer ---------- */

function utf8Encode(str) {
    const out = [];
    for (let i = 0; i < str.length; i++) {
        const c = str.codePointAt(i);
        if (c > 0xffff) i++;
        if (c < 0x80) out.push(c);
        else if (c < 0x800) out.push(0xc0 | c >> 6, 0x80 | c & 63);
        else if (c < 0x10000) out.push(0xe0 | c >> 12, 0x80 | c >> 6 & 63, 0x80 | c & 63);
        else out.push(0xf0 | c >> 18, 0x80 | c >> 12 & 63, 0x80 | c >> 6 & 63, 0x80 | c & 63);
    }
    return Uint8Array.from(out);
}

function utf8Decode(bytes, start = 0, end = bytes.length) {
    let out = '', chunk = [];
    const flush = () => { out += String.fromCodePoint(...chunk); chunk = []; };
    for (let i = start; i < end;) {
        const b = bytes[i];
        let c, n;
        if (b < 0x80) { c = b; n = 1; }
        else if (b >= 0xc2 && b < 0xe0) { c = b & 31; n = 2; }
        else if (b >= 0xe0 && b < 0xf0) { c = b & 15; n = 3; }
        else if (b >= 0xf0 && b < 0xf5) { c = b & 7; n = 4; }
        else { c = 0xfffd; n = 1; }
        if (n > 1) {
            if (i + n > end) { c = 0xfffd; n = end - i; }
            else {
                for (let k = 1; k < n; k++) {
                    if ((bytes[i + k] & 0xc0) !== 0x80) { c = 0xfffd; n = k; break; }
                    c = c << 6 | bytes[i + k] & 63;
                }
            }
        }
        chunk.push(c); i += n;
        if (chunk.length >= 4096) flush();
    }
    flush();
    return out;
}

const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

class Buffer extends Uint8Array {
    static from(value, encoding) {
        if (typeof value === 'string') {
            if (encoding === 'hex') {
                const out = new Buffer(value.length >> 1);
                for (let i = 0; i < out.length; i++) out[i] = parseInt(value.substr(i * 2, 2), 16);
                return out;
            }
            if (encoding === 'base64') {
                const clean = value.replace(/[^A-Za-z0-9+/]/g, '');
                const out = new Buffer(Math.floor(clean.length * 3 / 4));
                let acc = 0, bits = 0, k = 0;
                for (const ch of clean) {
                    acc = acc << 6 | B64.indexOf(ch); bits += 6;
                    if (bits >= 8) { bits -= 8; out[k++] = acc >> bits & 255; }
                }
                return out;
            }
            if (encoding === 'latin1' || encoding === 'binary' || encoding === 'ascii') {
                const out = new Buffer(value.length);
                for (let i = 0; i < value.length; i++) out[i] = value.charCodeAt(i) & 255;
                return out;
            }
            const bytes = utf8Encode(value);
            return new Buffer(bytes.buffer, 0, bytes.length);
        }
        if (value instanceof ArrayBuffer) return new Buffer(value);
        if (ArrayBuffer.isView(value) || Array.isArray(value)) { const out = new Buffer(value.length); out.set(value); return out; }
        throw new TypeError('The first argument must be a string, Buffer, ArrayBuffer, Array, or array-like object.');
    }
    static alloc(size, fill = 0) { const out = new Buffer(size); if (fill) out.fill(fill); return out; }
    static isBuffer(value) { return value instanceof Buffer; }
    static byteLength(value) { return typeof value === 'string' ? utf8Encode(value).length : value.byteLength; }
    static concat(list, total) {
        if (total === undefined) total = list.reduce((sum, b) => sum + b.length, 0);
        const out = new Buffer(total);
        let pos = 0;
        for (const b of list) { if (pos >= total) break; out.set(b.subarray(0, total - pos), pos); pos += b.length; }
        return out;
    }
    toString(encoding = 'utf8', start = 0, end = this.length) {
        if (encoding === 'hex') {
            let out = '';
            for (let i = start; i < end; i++) out += (this[i] < 16 ? '0' : '') + this[i].toString(16);
            return out;
        }
        if (encoding === 'base64') {
            let out = '';
            for (let i = start; i < end; i += 3) {
                const n = this[i] << 16 | (this[i + 1] || 0) << 8 | (this[i + 2] || 0);
                out += B64[n >> 18 & 63] + B64[n >> 12 & 63] + (i + 1 < end ? B64[n >> 6 & 63] : '=') + (i + 2 < end ? B64[n & 63] : '=');
            }
            return out;
        }
        if (encoding === 'latin1' || encoding === 'binary' || encoding === 'ascii') {
            let out = '';
            for (let i = start; i < end; i++) out += String.fromCharCode(this[i]);
            return out;
        }
        return utf8Decode(this, start, end);
    }
    toJSON() { return { type: 'Buffer', data: Array.from(this) }; }
    equals(other) { return this.length === other.length && this.every((b, i) => b === other[i]); }
    write(string, offset = 0) { const bytes = utf8Encode(string); this.set(bytes.subarray(0, this.length - offset), offset); return Math.min(bytes.length, this.length - offset); }
}

class TextEncoder { get encoding() { return 'utf-8'; } encode(s = '') { return utf8Encode(String(s)); } }
class TextDecoder { get encoding() { return 'utf-8'; } decode(b) { return b ? utf8Decode(new Uint8Array(b.buffer || b, b.byteOffset || 0, b.byteLength)) : ''; } }

/* ---------- events ---------- */

class EventEmitter {
    constructor() { this._events = new Map(); }
    on(name, fn) {
        if (!this._events) this._events = new Map();
        if (!this._events.has(name)) this._events.set(name, []);
        this._events.get(name).push(fn);
        return this;
    }
    addListener(name, fn) { return this.on(name, fn); }
    prependListener(name, fn) { this.on(name, fn); const list = this._events.get(name); list.unshift(list.pop()); return this; }
    once(name, fn) {
        const wrapper = (...args) => { this.off(name, wrapper); fn.apply(this, args); };
        wrapper.listener = fn;
        return this.on(name, wrapper);
    }
    off(name, fn) {
        const list = this._events && this._events.get(name);
        if (list) {
            const i = list.findIndex(l => l === fn || l.listener === fn);
            if (i >= 0) list.splice(i, 1);
        }
        return this;
    }
    removeListener(name, fn) { return this.off(name, fn); }
    removeAllListeners(name) { if (this._events) { if (name === undefined) this._events.clear(); else this._events.delete(name); } return this; }
    emit(name, ...args) {
        const list = this._events && this._events.get(name);
        if (!list || list.length === 0) {
            if (name === 'error') throw args[0] instanceof Error ? args[0] : new Error('Unhandled error: ' + args[0]);
            return false;
        }
        for (const fn of list.slice()) fn.apply(this, args);
        return true;
    }
    listenerCount(name) { const list = this._events && this._events.get(name); return list ? list.length : 0; }
    listeners(name) { const list = this._events && this._events.get(name); return list ? list.map(l => l.listener || l) : []; }
    setMaxListeners() { return this; }
}
EventEmitter.EventEmitter = EventEmitter;

/* ---------- timers / process ---------- */

const timers = new Map();
let timerSeq = 0;
function setTimeout_(fn, ms = 0, ...args) {
    const id = ++timerSeq;
    timers.set(id, os.setTimeout(() => { timers.delete(id); fn(...args); }, ms));
    return id;
}
function setInterval_(fn, ms = 0, ...args) {
    const id = ++timerSeq;
    const tick = () => { timers.set(id, os.setTimeout(() => { fn(...args); if (timers.has(id)) tick(); }, ms)); };
    tick();
    return id;
}
function clearTimeout_(id) {
    const handle = timers.get(id);
    if (handle !== undefined) { os.clearTimeout(handle); timers.delete(id); }
}
const setImmediate_ = (fn, ...args) => setTimeout_(fn, 0, ...args);
const nextTick = (fn, ...args) => { Promise.resolve().then(() => fn(...args)); };

const startTime = Date.now();
const process = Object.assign(new EventEmitter(), {
    argv: ['node-lite'],
    env: std.getenviron(),
    platform: 'linux',
    arch: 'x64',
    version: 'v0.0.0-node-lite',
    versions: { 'node-lite': VERSION },
    pid: os.getpid(),
    exitCode: undefined,
    cwd: () => os.getcwd()[0],
    exit(code) {
        this.emit('exit', code === undefined ? (this.exitCode || 0) : code);
        std.out.flush();
        std.exit(code === undefined ? (this.exitCode || 0) : code);
    },
    nextTick,
    uptime: () => (Date.now() - startTime) / 1000,
    hrtime: Object.assign(() => { const t = os.now(); return [Math.floor(t / 1000), Math.floor(t % 1000 * 1e6)]; }, { bigint: () => BigInt(Math.floor(os.now() * 1e6)) }),
    memoryUsage: () => ({ rss: 0, heapTotal: 0, heapUsed: 0, external: 0 }),
    emitWarning: (w) => std.err.puts('Warning: ' + w + '\n'),
    stdout: { write(s) { std.out.puts(typeof s === 'string' ? s : Buffer.from(s).toString()); std.out.flush(); return true; }, isTTY: false },
    stderr: { write(s) { std.err.puts(typeof s === 'string' ? s : Buffer.from(s).toString()); std.err.flush(); return true; }, isTTY: false },
});

for (const level of ['error', 'warn', 'info', 'debug']) {
    if (typeof console[level] !== 'function') {
        console[level] = (...args) => { std.err.puts(args.map(a => typeof a === 'string' ? a : inspect(a)).join(' ') + '\n'); std.err.flush(); };
    }
}

/* ---------- path (posix) ---------- */

function normalizeParts(p, absolute) {
    const out = [];
    for (const part of p.split('/')) {
        if (!part || part === '.') continue;
        if (part === '..') { if (out.length && out[out.length - 1] !== '..') out.pop(); else if (!absolute) out.push('..'); }
        else out.push(part);
    }
    return out;
}
const path = {
    sep: '/', delimiter: ':',
    isAbsolute: p => p.startsWith('/'),
    normalize(p) {
        if (p === '') return '.';
        const abs = p.startsWith('/'), trailing = p.endsWith('/');
        let out = normalizeParts(p, abs).join('/');
        if (!out && !abs) out = '.';
        if (out && trailing) out += '/';
        return (abs ? '/' : '') + out;
    },
    join(...parts) { const joined = parts.filter(p => p !== '').join('/'); return joined ? path.normalize(joined) : '.'; },
    resolve(...parts) {
        let resolved = '';
        for (let i = parts.length - 1; i >= 0 && !resolved.startsWith('/'); i--) if (parts[i]) resolved = parts[i] + (resolved ? '/' + resolved : '');
        if (!resolved.startsWith('/')) resolved = process.cwd() + (resolved ? '/' + resolved : '');
        return '/' + normalizeParts(resolved, true).join('/');
    },
    dirname(p) {
        const trimmed = p.length > 1 ? p.replace(/\/+$/, '') : p;
        const i = trimmed.lastIndexOf('/');
        return i < 0 ? '.' : i === 0 ? '/' : trimmed.slice(0, i);
    },
    basename(p, ext) {
        let base = p.replace(/\/+$/, '');
        base = base.slice(base.lastIndexOf('/') + 1);
        return ext && base.endsWith(ext) && base !== ext ? base.slice(0, -ext.length) : base;
    },
    extname(p) { const base = path.basename(p); const i = base.lastIndexOf('.'); return i <= 0 ? '' : base.slice(i); },
    relative(from, to) {
        const a = path.resolve(from).split('/').filter(Boolean), b = path.resolve(to).split('/').filter(Boolean);
        let i = 0;
        while (i < a.length && i < b.length && a[i] === b[i]) i++;
        return [...a.slice(i).map(() => '..'), ...b.slice(i)].join('/');
    },
    parse(p) {
        const base = path.basename(p), ext = path.extname(p);
        return { root: p.startsWith('/') ? '/' : '', dir: path.dirname(p), base, ext, name: ext ? base.slice(0, -ext.length) : base };
    },
};
path.posix = path;

/* ---------- fs (synchronous underneath; the async forms defer the result) ---------- */

function pathString(p) { return p instanceof URL ? p.pathname : String(p); }
function encodingOf(options) { return typeof options === 'string' ? options : options && options.encoding; }

function readFileSync(file, options) {
    const p = pathString(file);
    const fd = os.open(p, os.O_RDONLY);
    if (fd < 0) throw sysError(fd, 'open', p);
    try {
        const chunks = [];
        for (;;) {
            const ab = new ArrayBuffer(65536);
            const n = os.read(fd, ab, 0, 65536);
            if (n < 0) throw sysError(n, 'read', p);
            if (n === 0) break;
            chunks.push(new Buffer(ab, 0, n));
        }
        const data = Buffer.concat(chunks);
        const enc = encodingOf(options);
        return enc ? data.toString(enc) : data;
    } finally { os.close(fd); }
}

function writeWith(file, data, flags, options) {
    const p = pathString(file);
    const buf = typeof data === 'string' ? Buffer.from(data, encodingOf(options) || 'utf8') : Buffer.from(data);
    const fd = os.open(p, flags, 0o666);
    if (fd < 0) throw sysError(fd, 'open', p);
    try {
        let done = 0;
        while (done < buf.length) {
            const n = os.write(fd, buf.buffer, buf.byteOffset + done, buf.length - done);
            if (n < 0) throw sysError(n, 'write', p);
            done += n;
        }
    } finally { os.close(fd); }
}

function statSync(file, options) {
    const p = pathString(file);
    const [st, err] = os.stat(p);
    if (err) { if (options && options.throwIfNoEntry === false) return undefined; throw sysError(err, 'stat', p); }
    const kind = st.mode & os.S_IFMT;
    return {
        size: st.size, mode: st.mode, mtimeMs: st.mtime, atimeMs: st.atime, ctimeMs: st.ctime, mtime: new Date(st.mtime),
        isFile: () => kind === os.S_IFREG, isDirectory: () => kind === os.S_IFDIR, isSymbolicLink: () => false,
    };
}

function mkdirSync(dir, options) {
    const p = pathString(dir);
    const recursive = options && options.recursive;
    if (recursive) {
        let cur = '';
        for (const part of path.resolve(p).split('/').filter(Boolean)) {
            cur += '/' + part;
            const [, err] = os.stat(cur);
            if (err) { const r = os.mkdir(cur, 0o777); if (r < 0 && r !== -17) throw sysError(r, 'mkdir', cur); }
        }
        return;
    }
    const r = os.mkdir(p, 0o777);
    if (r < 0) throw sysError(r, 'mkdir', p);
}

function rmSync(file, options) {
    const p = pathString(file);
    const [st, err] = os.stat(p);
    if (err) { if (options && options.force) return; throw sysError(err, 'rm', p); }
    if ((st.mode & os.S_IFMT) === os.S_IFDIR) {
        if (!(options && options.recursive)) throw sysError(21, 'rm', p);
        for (const name of fs.readdirSync(p)) rmSync(p + '/' + name, options);
    }
    const r = os.remove(p);
    if (r < 0) throw sysError(r, 'rm', p);
}

const fs = {
    readFileSync,
    writeFileSync: (file, data, options) => writeWith(file, data, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, options),
    appendFileSync: (file, data, options) => writeWith(file, data, os.O_WRONLY | os.O_CREAT | os.O_APPEND, options),
    existsSync: file => { const [, err] = os.stat(pathString(file)); return !err; },
    statSync, lstatSync: statSync, mkdirSync, rmSync,
    readdirSync(dir) {
        const p = pathString(dir);
        const [names, err] = os.readdir(p);
        if (err) throw sysError(err, 'scandir', p);
        return names.filter(n => n !== '.' && n !== '..').sort();
    },
    unlinkSync(file) { const p = pathString(file); const r = os.remove(p); if (r < 0) throw sysError(r, 'unlink', p); },
    rmdirSync(dir) { const p = pathString(dir); const r = os.remove(p); if (r < 0) throw sysError(r, 'rmdir', p); },
    renameSync(from, to) { const r = os.rename(pathString(from), pathString(to)); if (r < 0) throw sysError(r, 'rename', pathString(from)); },
    copyFileSync(from, to) { fs.writeFileSync(to, fs.readFileSync(from)); },
    constants: { F_OK: 0, R_OK: 4, W_OK: 2, X_OK: 1 },
    accessSync(file) { if (!fs.existsSync(file)) throw sysError(2, 'access', pathString(file)); },
};
fs.promises = {};
for (const name of ['readFile', 'writeFile', 'appendFile', 'stat', 'lstat', 'readdir', 'mkdir', 'rm', 'unlink', 'rmdir', 'rename', 'copyFile', 'access']) {
    const sync = fs[name + 'Sync'];
    fs.promises[name] = async (...args) => sync(...args);
    fs[name] = (...args) => {
        const cb = typeof args[args.length - 1] === 'function' ? args.pop() : () => {};
        let result, error = null;
        try { result = sync(...args); } catch (e) { error = e; }
        setTimeout_(() => error ? cb(error) : cb(null, result), 0);
    };
}
fs.exists = (file, cb) => setTimeout_(() => cb(fs.existsSync(file)), 0);

/* ---------- os ---------- */

function procFile(name) { try { return fs.readFileSync(name, 'utf8'); } catch (e) { return ''; } }
function memInfo(key) { const m = procFile('/proc/meminfo').match(new RegExp('^' + key + ':\\s+(\\d+) kB', 'm')); return m ? +m[1] * 1024 : 0; }
const nodeOs = {
    EOL: '\n',
    platform: () => 'linux', type: () => 'Linux', arch: () => 'x64',
    homedir: () => process.env.HOME || '/root',
    tmpdir: () => process.env.TMPDIR || '/tmp',
    hostname: () => procFile('/proc/sys/kernel/hostname').trim() || 'localhost',
    release: () => procFile('/proc/sys/kernel/osrelease').trim(),
    uptime: () => parseFloat(procFile('/proc/uptime')) || 0,
    totalmem: () => memInfo('MemTotal'),
    freemem: () => memInfo('MemAvailable'),
    cpus: () => procFile('/proc/cpuinfo').split('\n').filter(l => l.startsWith('model name')).map(l => ({ model: l.split(':')[1].trim() })),
    // Same shape as Node's: { name: [{ address, netmask, family, mac, internal, cidr, scopeid? }] }.
    networkInterfaces: () => {
        const result = {};
        for (const a of net.interfaces()) {
            const mac = procFile('/sys/class/net/' + a.name + '/address').trim() || '00:00:00:00:00:00';
            const entry = { address: a.address, netmask: a.netmask, family: a.family, mac: mac, internal: a.internal, cidr: a.address + '/' + a.prefix };
            if (a.family === 'IPv6') entry.scopeid = a.scopeid;
            (result[a.name] = result[a.name] || []).push(entry);
        }
        return result;
    },
};

/* ---------- util / assert ---------- */

function inspect(value, depth = 2) {
    const seen = [];
    const fmt = (v, d) => {
        if (typeof v === 'string') return d === 0 ? v : "'" + v.replace(/'/g, "\\'") + "'";
        if (typeof v === 'function') return '[Function' + (v.name ? ': ' + v.name : '') + ']';
        if (typeof v === 'bigint') return v + 'n';
        if (typeof v === 'symbol') return v.toString();
        if (v === null || typeof v !== 'object') return String(v);
        if (v instanceof Error) return v.stack || String(v);
        if (v instanceof Date) return v.toISOString();
        if (seen.includes(v)) return '[Circular]';
        if (d > depth) return Array.isArray(v) ? '[Array]' : '[Object]';
        seen.push(v);
        const inner = Array.isArray(v) ? v.map(x => fmt(x, d + 1)) : Object.keys(v).map(k => k + ': ' + fmt(v[k], d + 1));
        seen.pop();
        const text = inner.join(', ');
        const [open, close] = Array.isArray(v) ? ['[', ']'] : ['{', '}'];
        return inner.length ? `${open} ${text} ${close}` : open + close;
    };
    return fmt(value, 0);
}

function format(first, ...rest) {
    if (typeof first !== 'string') return [first, ...rest].map(v => typeof v === 'string' ? v : inspect(v)).join(' ');
    let i = 0;
    let out = first.replace(/%[sdifjoO%]/g, token => {
        if (token === '%%') return '%';
        if (i >= rest.length) return token;
        const v = rest[i++];
        switch (token) {
            case '%s': return typeof v === 'string' ? v : inspect(v);
            case '%d': return String(Number(v));
            case '%i': return String(parseInt(v));
            case '%f': return String(parseFloat(v));
            case '%j': return JSON.stringify(v);
            default: return inspect(v);
        }
    });
    for (; i < rest.length; i++) out += ' ' + (typeof rest[i] === 'string' ? rest[i] : inspect(rest[i]));
    return out;
}

const util = {
    format, inspect,
    promisify: fn => (...args) => new Promise((resolve, reject) => fn(...args, (err, value) => err ? reject(err) : resolve(value))),
    inherits(ctor, superCtor) { Object.setPrototypeOf(ctor.prototype, superCtor.prototype); Object.setPrototypeOf(ctor, superCtor); },
    isDeepStrictEqual: (a, b) => JSON.stringify(a) === JSON.stringify(b),
    TextEncoder, TextDecoder,
};

class AssertionError extends Error { constructor(message) { super(message); this.name = 'AssertionError'; this.code = 'ERR_ASSERTION'; } }
function assert(value, message) { if (!value) throw new AssertionError(message || 'The expression evaluated to a falsy value'); }
Object.assign(assert, {
    ok: assert, AssertionError,
    equal: (a, b, m) => { if (a != b) throw new AssertionError(m || `${inspect(a)} == ${inspect(b)}`); },
    notEqual: (a, b, m) => { if (a == b) throw new AssertionError(m || `${inspect(a)} != ${inspect(b)}`); },
    strictEqual: (a, b, m) => { if (!Object.is(a, b)) throw new AssertionError(m || `Expected values to be strictly equal:\n${inspect(a)} !== ${inspect(b)}`); },
    notStrictEqual: (a, b, m) => { if (Object.is(a, b)) throw new AssertionError(m || `Expected "actual" to be strictly unequal to: ${inspect(b)}`); },
    deepStrictEqual: (a, b, m) => { if (!util.isDeepStrictEqual(a, b)) throw new AssertionError(m || `Expected values to be strictly deep-equal:\n${inspect(a)}\n\nshould equal\n\n${inspect(b)}`); },
    deepEqual: (a, b, m) => assert.deepStrictEqual(a, b, m),
    throws(fn, expected, m) {
        try { fn(); } catch (e) { if (typeof expected === 'function' && !(e instanceof expected)) throw e; return; }
        throw new AssertionError(typeof expected === 'string' ? expected : m || 'Missing expected exception.');
    },
    fail: m => { throw new AssertionError(m || 'Failed'); },
});
assert.strict = assert;

/* ---------- url / querystring ---------- */

const enc = encodeURIComponent, dec = s => { try { return decodeURIComponent(s.replace(/\+/g, ' ')); } catch (e) { return s; } };

class URLSearchParams {
    constructor(init = '') {
        this._list = [];
        if (typeof init === 'string') {
            for (const pair of init.replace(/^\?/, '').split('&')) {
                if (!pair) continue;
                const i = pair.indexOf('=');
                this._list.push(i < 0 ? [dec(pair), ''] : [dec(pair.slice(0, i)), dec(pair.slice(i + 1))]);
            }
        } else if (init instanceof URLSearchParams) this._list = init._list.map(p => p.slice());
        else if (init) for (const [k, v] of (Symbol.iterator in init ? init : Object.entries(init))) this._list.push([String(k), String(v)]);
    }
    append(k, v) { this._list.push([String(k), String(v)]); }
    delete(k) { this._list = this._list.filter(p => p[0] !== k); }
    get(k) { const p = this._list.find(p => p[0] === k); return p ? p[1] : null; }
    getAll(k) { return this._list.filter(p => p[0] === k).map(p => p[1]); }
    has(k) { return this._list.some(p => p[0] === k); }
    set(k, v) { const i = this._list.findIndex(p => p[0] === k); if (i < 0) this.append(k, v); else { this._list[i][1] = String(v); this._list = this._list.filter((p, j) => p[0] !== k || j === i); } }
    forEach(fn) { for (const [k, v] of this._list) fn(v, k, this); }
    keys() { return this._list.map(p => p[0])[Symbol.iterator](); }
    values() { return this._list.map(p => p[1])[Symbol.iterator](); }
    entries() { return this._list.map(p => p.slice())[Symbol.iterator](); }
    [Symbol.iterator]() { return this.entries(); }
    toString() { return this._list.map(([k, v]) => enc(k) + '=' + enc(v)).join('&'); }
}

class URL {
    constructor(input, base) {
        let m = /^([a-zA-Z][a-zA-Z0-9+.-]*:)(?:\/\/(?:([^@/?#]*)@)?([^:/?#]*)(?::(\d+))?)?([^?#]*)(\?[^#]*)?(#.*)?$/.exec(String(input));
        if (!m && base !== undefined) {
            const b = new URL(base);
            const rel = String(input);
            if (rel.startsWith('//')) return new URL(b.protocol + rel);
            const p = rel.startsWith('/') ? rel : rel.startsWith('?') ? b.pathname + rel
                : path.join(path.dirname(b.pathname), rel);
            m =/^([^:]+:)(?:\/\/(?:([^@/?#]*)@)?([^:/?#]*)(?::(\d+))?)?([^?#]*)(\?[^#]*)?(#.*)?$/.exec(b.protocol + '//' + b.host + p);
        }
        if (!m) { const e = new TypeError('Invalid URL'); e.code = 'ERR_INVALID_URL'; e.input = String(input); throw e; }
        this.protocol = m[1].toLowerCase(); this.hostname = (m[3] || '').toLowerCase(); this.port = m[4] || '';
        this.pathname = m[5] || '/'; this.search = m[6] && m[6] !== '?' ? m[6] : ''; this.hash = m[7] && m[7] !== '#' ? m[7] : '';
        this.username = m[2] ? m[2].split(':')[0] : ''; this.password = m[2] && m[2].includes(':') ? m[2].split(':')[1] : '';
        this.searchParams = new URLSearchParams(this.search);
    }
    get host() { return this.hostname + (this.port ? ':' + this.port : ''); }
    get origin() { return this.protocol + '//' + this.host; }
    get href() { const q = this.searchParams.toString(); return this.origin + this.pathname + (q ? '?' + q : '') + this.hash; }
    toString() { return this.href; }
    toJSON() { return this.href; }
}

const querystring = {
    parse(str) {
        const out = {};
        for (const [k, v] of new URLSearchParams(str)) {
            if (k in out) out[k] = [].concat(out[k], v); else out[k] = v;
        }
        return out;
    },
    stringify: obj => Object.keys(obj).flatMap(k => [].concat(obj[k]).map(v => enc(k) + '=' + enc(v))).join('&'),
    escape: enc, unescape: dec,
};

const urlModule = {
    URL, URLSearchParams,
    parse(str, parseQuery) {
        const m = /^(?:([a-zA-Z][a-zA-Z0-9+.-]*:)\/\/([^/?#]*))?([^?#]*)(\?[^#]*)?(#.*)?$/.exec(str);
        const search = m[4] || '';
        return {
            protocol: m[1] || null, host: m[2] || null, hostname: m[2] ? m[2].split(':')[0] : null, pathname: m[3] || null,
            search: search || null, query: parseQuery ? querystring.parse(search) : search.replace(/^\?/, '') || null,
            hash: m[5] || null, path: (m[3] || '') + search, href: str,
        };
    },
};

/* ---------- http (HTTP/1.1 server; one request per connection) ---------- */

const STATUS_CODES = { 100: 'Continue', 200: 'OK', 201: 'Created', 202: 'Accepted', 204: 'No Content', 206: 'Partial Content',
    301: 'Moved Permanently', 302: 'Found', 303: 'See Other', 304: 'Not Modified', 307: 'Temporary Redirect', 308: 'Permanent Redirect',
    400: 'Bad Request', 401: 'Unauthorized', 403: 'Forbidden', 404: 'Not Found', 405: 'Method Not Allowed', 408: 'Request Timeout',
    409: 'Conflict', 411: 'Length Required', 413: 'Payload Too Large', 414: 'URI Too Long', 415: 'Unsupported Media Type',
    422: 'Unprocessable Entity', 429: 'Too Many Requests', 431: 'Request Header Fields Too Large',
    500: 'Internal Server Error', 501: 'Not Implemented', 502: 'Bad Gateway', 503: 'Service Unavailable' };

const MAX_HEADER = 64 * 1024, MAX_BODY = 16 * 1024 * 1024;

/* Socket readiness: every socket is watched through one epoll fd, which is the only fd given to os.setReadHandler
   (see net_native.c), and the callbacks are dispatched here. The event loop ends once nothing is watched. */
const WATCH_READ = 1, WATCH_WRITE = 2, WATCH_ERROR = 4;
const poller = {
    epfd: -1, entries: new Map(),
    set(fd, mask, callback) {
        if (this.epfd < 0) this.epfd = net.epoll();
        this.entries.set(fd, { mask, callback });
        net.watch(this.epfd, fd, mask);
        if (this.entries.size === 1) os.setReadHandler(this.epfd, () => this.drain());
    },
    on(fd, callback) { this.set(fd, WATCH_READ, callback); },
    off(fd) {
        if (!this.entries.delete(fd)) return;
        net.unwatch(this.epfd, fd);
        if (this.entries.size === 0) os.setReadHandler(this.epfd, null);
    },
    /* Promise that settles once fd can be read (WATCH_READ) or written (WATCH_WRITE) without blocking. */
    ready(fd, mask) { return new Promise(resolve => this.set(fd, mask, () => { this.off(fd); resolve(); })); },
    drain() {
        const ready = net.wait(this.epfd);
        for (let i = 0; i < ready.length; i += 2) {
            const entry = this.entries.get(ready[i]);
            if (entry && ready[i + 1] & (entry.mask | WATCH_ERROR)) entry.callback();
        }
    },
};

class IncomingMessage extends EventEmitter {
    constructor(socket) { super(); this.socket = socket; this.headers = {}; this.method = 'GET'; this.url = '/'; this.httpVersion = '1.1'; this._body = Buffer.alloc(0); this._encoding = null; }
    setEncoding(enc) { this._encoding = enc || 'utf8'; return this; }
    _deliver() {
        if (this._body.length) this.emit('data', this._encoding ? this._body.toString(this._encoding) : this._body);
        this.emit('end');
        this.emit('close');
    }
}

class ServerResponse extends EventEmitter {
    constructor(req, finish) {
        super();
        this.req = req; this._finish = finish;
        this.statusCode = 200; this.statusMessage = undefined;
        this._headers = new Map(); this._chunks = [];
        this.headersSent = false; this.finished = false; this.writableEnded = false;
    }
    setHeader(name, value) { this._headers.set(name.toLowerCase(), [name, value]); return this; }
    getHeader(name) { const h = this._headers.get(name.toLowerCase()); return h && h[1]; }
    getHeaders() { const o = {}; for (const [k, [, v]] of this._headers) o[k] = v; return o; }
    getHeaderNames() { return [...this._headers.keys()]; }
    hasHeader(name) { return this._headers.has(name.toLowerCase()); }
    removeHeader(name) { this._headers.delete(name.toLowerCase()); }
    writeHead(status, message, headers) {
        this.statusCode = status;
        if (typeof message === 'string') this.statusMessage = message; else headers = message;
        if (headers) for (const k of Object.keys(headers)) this.setHeader(k, headers[k]);
        return this;
    }
    write(chunk) { if (chunk !== undefined && chunk !== null) this._chunks.push(Buffer.from(typeof chunk === 'string' ? chunk : chunk)); return true; }
    end(chunk, encoding, cb) {
        if (typeof chunk === 'function') { cb = chunk; chunk = undefined; }
        if (typeof encoding === 'function') { cb = encoding; }
        if (this.finished) return this;
        if (chunk !== undefined && chunk !== null) this.write(chunk);
        this.finished = this.writableEnded = true;
        const body = Buffer.concat(this._chunks);
        const noBody = this.req.method === 'HEAD' || this.statusCode === 204 || this.statusCode === 304;
        if (!this.hasHeader('content-length')) this.setHeader('Content-Length', this.statusCode === 204 || this.statusCode === 304 ? 0 : body.length);
        if (!this.hasHeader('date')) this.setHeader('Date', new Date().toUTCString());
        this.setHeader('Connection', 'close');
        let head = `HTTP/1.1 ${this.statusCode} ${this.statusMessage || STATUS_CODES[this.statusCode] || 'Unknown'}\r\n`;
        for (const [, [name, value]] of this._headers) for (const v of [].concat(value)) head += `${name}: ${v}\r\n`;
        head += '\r\n';
        this.headersSent = true;
        this._finish(Buffer.concat([Buffer.from(head, 'latin1'), noBody ? Buffer.alloc(0) : body]));
        if (cb) setTimeout_(cb, 0);
        setTimeout_(() => { this.emit('finish'); this.emit('close'); }, 0);
        return this;
    }
}

class Server extends EventEmitter {
    constructor(options, handler) {
        super();
        if (typeof options === 'function') handler = options;
        if (handler) this.on('request', handler);
        this._fd = -1; this._port = 0; this.listening = false; this.timeout = 0;
    }
    listen(...args) {
        let cb = typeof args[args.length - 1] === 'function' ? args.pop() : null;
        let [port, host] = args;
        if (port && typeof port === 'object') ({ port, host } = port);
        host = typeof host === 'string' ? host : '0.0.0.0';
        const fd = net.listen(host, port || 0, 511);
        if (fd < 0) {
            const err = sysError(fd, 'listen', `${host}:${port}`);
            err.address = host; err.port = port;
            if (this.listenerCount('error')) setTimeout_(() => this.emit('error', err), 0); else throw err;
            return this;
        }
        this._fd = fd; this._host = host; this._port = net.port(fd); this.listening = true;
        poller.on(fd, () => this._acceptAll());
        if (cb) this.once('listening', cb);
        setTimeout_(() => this.emit('listening'), 0);
        return this;
    }
    address() { return this.listening ? { address: this._host, family: 'IPv4', port: this._port } : null; }
    close(cb) {
        if (this.listening) {
            poller.off(this._fd);
            net.close(this._fd);
            this._fd = -1; this.listening = false;
        }
        if (cb) this.once('close', cb);
        setTimeout_(() => this.emit('close'), 0);
        return this;
    }
    setTimeout() { return this; }
    _acceptAll() {
        for (;;) {
            const c = net.accept(this._fd);
            if (c === null || typeof c === 'number') return;
            this._connection(c[0], c[1], c[2]);
        }
    }
    _connection(fd, ip, port) {
        let acc = Buffer.alloc(0), head = null, bodyStart = 0, closed = false, dispatched = false;
        const socket = { remoteAddress: ip, remotePort: port, destroy: () => close() };
        const close = () => { if (!closed) { closed = true; poller.off(fd); net.close(fd); } };
        const reply = (data) => {
            if (!closed) { net.send(fd, data.buffer, data.byteOffset, data.length); close(); }
        };
        const fail = (code) => {
            const body = Buffer.from(`${code} ${STATUS_CODES[code]}\n`);
            reply(Buffer.concat([Buffer.from(`HTTP/1.1 ${code} ${STATUS_CODES[code]}\r\nContent-Type: text/plain\r\nContent-Length: ${body.length}\r\nConnection: close\r\n\r\n`), body]));
        };
        const dispatch = (body) => {
            const req = new IncomingMessage(socket);
            Object.assign(req, head);
            req._body = body;
            const res = new ServerResponse(req, reply);
            try {
                if (!this.emit('request', req, res)) { res.statusCode = 404; res.end('Not Found\n'); }
            } catch (e) {
                std.err.puts('Unhandled error in request handler: ' + errorText(e) + '\n');
                if (!res.finished) { res.statusCode = 500; res.setHeader('Content-Type', 'text/plain'); res.end('Internal Server Error\n'); }
            }
            setTimeout_(() => req._deliver(), 0);
        };
        const parse = () => {
            if (!head) {
                let end = -1;
                for (let i = 0; i + 3 < acc.length; i++) {
                    if (acc[i] === 13 && acc[i + 1] === 10 && acc[i + 2] === 13 && acc[i + 3] === 10) { end = i; break; }
                }
                if (end < 0) { if (acc.length > MAX_HEADER) fail(431); return; }
                const lines = acc.toString('latin1', 0, end).split('\r\n');
                const m = /^([A-Z]+) (\S+) HTTP\/(\d\.\d)$/.exec(lines[0]);
                if (!m) { fail(400); return; }
                const headers = {};
                for (const line of lines.slice(1)) {
                    const i = line.indexOf(':');
                    if (i <= 0) continue;
                    const k = line.slice(0, i).trim().toLowerCase(), v = line.slice(i + 1).trim();
                    headers[k] = k in headers ? headers[k] + ', ' + v : v;
                }
                if (headers['transfer-encoding']) { fail(501); return; }
                const length = headers['content-length'] === undefined ? 0 : Number(headers['content-length']);
                if (!(length >= 0)) { fail(400); return; }
                if (length > MAX_BODY) { fail(413); return; }
                head = { method: m[1], url: m[2], httpVersion: m[3], headers, _length: length };
                bodyStart = end + 4;
            }
            if (acc.length - bodyStart >= head._length) {
                dispatched = true;
                poller.off(fd);
                dispatch(acc.subarray(bodyStart, bodyStart + head._length));
            }
        };
        poller.on(fd, () => {
            for (;;) {
                const data = net.recv(fd, 65536);
                if (data === null) return;
                if (typeof data === 'number' || data.byteLength === 0) { close(); return; }
                acc = Buffer.concat([acc, new Buffer(data)]);
                parse();
                if (closed || dispatched) return;
            }
        });
    }
}

const http = {
    STATUS_CODES, Server, IncomingMessage, ServerResponse,
    METHODS: ['DELETE', 'GET', 'HEAD', 'OPTIONS', 'PATCH', 'POST', 'PUT'],
    createServer: (options, handler) => new Server(options, handler),
    request() { throw new Error('http.request is not implemented in node-lite (server only)'); },
    get() { throw new Error('http.get is not implemented in node-lite (server only)'); },
};

/* ---------- dns ---------- */

const IPV4 = /^\d{1,3}(\.\d{1,3}){3}$/;

/* IPv4 addresses of a host name (getaddrinfo on a thread, so the event loop keeps running). */
async function resolveHost(host) {
    if (IPV4.test(host)) return [host];
    const fd = net.resolve(host);
    if (fd < 0) throw sysError(fd, 'getaddrinfo', host);
    let text = '';
    try {
        for (;;) {
            const data = net.recv(fd, 1024);
            if (data === null) { await poller.ready(fd, WATCH_READ); continue; }
            if (typeof data === 'number' || data.byteLength === 0) break;
            text += utf8Decode(new Uint8Array(data));
        }
    } finally { net.close(fd); }
    const [kind, ...addresses] = text.trim().split(' ');
    if (kind === 'ok' && addresses.length) return addresses;
    const err = new Error(`getaddrinfo ENOTFOUND ${host}`);
    err.code = 'ENOTFOUND'; err.syscall = 'getaddrinfo'; err.hostname = host; err.errno = -3008;
    throw err;
}

const addressEntries = addresses => addresses.map(address => ({ address, family: 4 }));
const dns = {
    lookup(host, options, callback) {
        if (typeof options === 'function') { callback = options; options = {}; }
        resolveHost(host).then(
            addresses => options && options.all ? callback(null, addressEntries(addresses)) : callback(null, addresses[0], 4),
            error => callback(error));
    },
    resolve4(host, callback) { resolveHost(host).then(addresses => callback(null, addresses), error => callback(error)); },
    promises: {
        lookup: async (host, options) => {
            const addresses = await resolveHost(host);
            return options && options.all ? addressEntries(addresses) : { address: addresses[0], family: 4 };
        },
        resolve4: host => resolveHost(host),
    },
};
dns.resolve = (host, rrtype, callback) => dns.resolve4(host, callback || rrtype);
dns.promises.resolve = dns.promises.resolve4;

/* ---------- fetch ---------- */

class Headers {
    constructor(init) {
        this._map = new Map();
        if (init instanceof Headers) init.forEach((value, name) => this.append(name, value));
        else if (Array.isArray(init)) for (const [name, value] of init) this.append(name, value);
        else if (init) for (const name of Object.keys(init)) this.append(name, init[name]);
    }
    append(name, value) {
        const key = String(name).toLowerCase(), list = this._map.get(key);
        if (list) list.push(String(value).trim()); else this._map.set(key, [String(value).trim()]);
    }
    set(name, value) { this._map.set(String(name).toLowerCase(), [String(value).trim()]); }
    get(name) { const list = this._map.get(String(name).toLowerCase()); return list ? list.join(', ') : null; }
    has(name) { return this._map.has(String(name).toLowerCase()); }
    delete(name) { this._map.delete(String(name).toLowerCase()); }
    getSetCookie() { return (this._map.get('set-cookie') || []).slice(); }
    forEach(callback, thisArg) { for (const [name, list] of this._map) callback.call(thisArg, list.join(', '), name, this); }
    keys() { return [...this._map.keys()][Symbol.iterator](); }
    values() { return [...this._map.values()].map(list => list.join(', '))[Symbol.iterator](); }
    entries() { return [...this._map].map(([name, list]) => [name, list.join(', ')])[Symbol.iterator](); }
    [Symbol.iterator]() { return this.entries(); }
}

function toBytes(body, headers) {
    const setType = type => { if (headers && !headers.has('content-type')) headers.set('content-type', type); };
    if (typeof body === 'string') { setType('text/plain;charset=UTF-8'); return Buffer.from(body); }
    if (body instanceof URLSearchParams) { setType('application/x-www-form-urlencoded;charset=UTF-8'); return Buffer.from(body.toString()); }
    if (body instanceof ArrayBuffer || ArrayBuffer.isView(body)) return Buffer.from(body instanceof ArrayBuffer ? new Uint8Array(body) : body);
    return Buffer.from(String(body));
}

class Response {
    constructor(body = null, init = {}) {
        this.status = init.status === undefined ? 200 : init.status;
        this.statusText = init.statusText === undefined ? STATUS_CODES[this.status] || '' : init.statusText;
        this.headers = init.headers instanceof Headers ? init.headers : new Headers(init.headers);
        this._body = body === null ? Buffer.alloc(0) : toBytes(body, this.headers);
        this.bodyUsed = false; this.url = ''; this.redirected = false; this.type = 'basic';
    }
    get ok() { return this.status >= 200 && this.status < 300; }
    _consume() {
        if (this.bodyUsed) throw new TypeError('Body is unusable: Body has already been read');
        this.bodyUsed = true;
        return this._body;
    }
    async text() { return this._consume().toString(); }
    async json() { return JSON.parse(await this.text()); }
    async arrayBuffer() { const b = this._consume(); return b.buffer.slice(b.byteOffset, b.byteOffset + b.length); }
    async bytes() { return new Uint8Array(await this.arrayBuffer()); }
    clone() {
        if (this.bodyUsed) throw new TypeError('Response.clone: Body has already been consumed.');
        const copy = new Response(Buffer.from(this._body), { status: this.status, statusText: this.statusText, headers: new Headers(this.headers) });
        copy.url = this.url; copy.redirected = this.redirected;
        return copy;
    }
    static json(data, init = {}) {
        const headers = new Headers(init.headers);
        if (!headers.has('content-type')) headers.set('content-type', 'application/json');
        return new Response(JSON.stringify(data), { ...init, headers });
    }
}

function namedError(name, message) { const e = new Error(message); e.name = name; return e; }

class AbortSignal {
    constructor() { this.aborted = false; this.reason = undefined; this.onabort = null; this._listeners = []; }
    addEventListener(type, listener) { if (type === 'abort') this._listeners.push(listener); }
    removeEventListener(type, listener) { this._listeners = this._listeners.filter(l => l !== listener); }
    throwIfAborted() { if (this.aborted) throw this.reason; }
    _abort(reason) {
        if (this.aborted) return;
        this.aborted = true;
        this.reason = reason === undefined ? namedError('AbortError', 'This operation was aborted') : reason;
        const event = { type: 'abort', target: this };
        if (this.onabort) this.onabort(event);
        for (const listener of this._listeners.slice()) listener(event);
    }
    static abort(reason) { const signal = new AbortSignal(); signal._abort(reason); return signal; }
    static timeout(ms) {
        const signal = new AbortSignal();
        setTimeout_(() => signal._abort(namedError('TimeoutError', 'The operation was aborted due to timeout')), ms);
        return signal;
    }
}
class AbortController {
    constructor() { this.signal = new AbortSignal(); }
    abort(reason) { this.signal._abort(reason); }
}

/* One TCP connection, optionally wrapped in TLS; reads wait on the poller, so other work keeps running. */
class Connection {
    constructor(fd) { this.fd = fd; this.tls = -1; this.closed = false; }
    async read() {
        for (;;) {
            const data = this.tls >= 0 ? net.tlsRead(this.tls, 16384) : net.recv(this.fd, 65536);
            if (data === null) { await poller.ready(this.fd, WATCH_READ); continue; }
            if (typeof data === 'number') throw this.tls >= 0 ? new Error(net.tlsError(this.tls)) : sysError(data, 'read');
            return new Buffer(data);
        }
    }
    write(buffer) {
        const sent = this.tls >= 0 ? net.tlsWrite(this.tls, buffer.buffer, buffer.byteOffset, buffer.length)
            : net.send(this.fd, buffer.buffer, buffer.byteOffset, buffer.length);
        if (sent < 0) throw this.tls >= 0 ? new Error(net.tlsError(this.tls)) : sysError(sent, 'write');
    }
    async startTls(hostname) {
        const id = net.tlsOpen(this.fd, hostname);
        if (id < 0) throw new Error(net.tlsError(-1));
        this.tls = id;
        for (;;) {
            const state = net.tlsHandshake(id);
            if (state === 0) return;
            if (state < 0) throw new Error(net.tlsError(id));
            await poller.ready(this.fd, state === 1 ? WATCH_READ : WATCH_WRITE);
        }
    }
    close() {
        if (this.closed) return;
        this.closed = true;
        poller.off(this.fd);
        if (this.tls >= 0) net.tlsClose(this.tls);
        net.close(this.fd);
    }
}

async function connectTcp(hostname, port, connections) {
    let lastError;
    for (const ip of await resolveHost(hostname)) {
        const fd = net.connect(ip, port);
        if (fd < 0) { lastError = sysError(fd, 'connect', `${ip}:${port}`); continue; }
        const connection = new Connection(fd);
        connections.add(connection);
        await poller.ready(fd, WATCH_WRITE);
        const status = net.connectStatus(fd);
        if (status === 0) return connection;
        connection.close(); connections.delete(connection);
        lastError = sysError(status, 'connect', `${ip}:${port}`);
    }
    throw lastError;
}

const MAX_RESPONSE = 64 * 1024 * 1024;

function indexOfBytes(buffer, pattern, from = 0) {
    outer: for (let i = Math.max(from, 0); i + pattern.length <= buffer.length; i++) {
        for (let j = 0; j < pattern.length; j++) if (buffer[i + j] !== pattern[j]) continue outer;
        return i;
    }
    return -1;
}

/* Decodes a complete chunked body, or returns null when more data is needed. */
function decodeChunked(buffer) {
    const parts = [];
    let pos = 0;
    for (;;) {
        const eol = indexOfBytes(buffer, [13, 10], pos);
        if (eol < 0) return null;
        const size = parseInt(buffer.toString('latin1', pos, eol).split(';')[0], 16);
        if (!(size >= 0)) throw new Error('invalid chunk size in the response');
        if (size === 0) return buffer.length >= eol + 4 ? Buffer.concat(parts) : null; /* "0\r\n" + the final "\r\n" */
        if (buffer.length < eol + 2 + size + 2) return null;
        parts.push(buffer.subarray(eol + 2, eol + 2 + size));
        pos = eol + 2 + size + 2;
    }
}

async function request(url, method, headers, body, connections) {
    const secure = url.protocol === 'https:';
    if (!secure && url.protocol !== 'http:') throw new TypeError(`unsupported URL protocol "${url.protocol}"`);
    const hostname = url.hostname.replace(/^\[|\]$/g, '');
    const connection = await connectTcp(hostname, Number(url.port) || (secure ? 443 : 80), connections);
    try {
        if (secure) await connection.startTls(hostname);

        const sent = new Headers(headers);
        if (!sent.has('host')) sent.set('host', url.host);
        if (!sent.has('user-agent')) sent.set('user-agent', 'node-lite/' + VERSION);
        if (!sent.has('accept')) sent.set('accept', '*/*');
        sent.set('accept-encoding', 'identity');
        sent.set('connection', 'close');
        if (body) sent.set('content-length', String(body.length));
        else if (method === 'POST' || method === 'PUT' || method === 'PATCH') sent.set('content-length', '0');
        let head = `${method} ${url.pathname}${url.search} HTTP/1.1\r\n`;
        sent.forEach((value, name) => {
            if (/[\r\n]/.test(value) || /[^!#$%&'*+.^_`|~\w-]/.test(name)) throw new TypeError(`Invalid header "${name}"`);
            head += `${name}: ${value}\r\n`;
        });
        const headBytes = Buffer.from(head + '\r\n', 'latin1');
        connection.write(body ? Buffer.concat([headBytes, body]) : headBytes);

        let received = Buffer.alloc(0), headEnd = -1;
        while (headEnd < 0) {
            const chunk = await connection.read();
            if (chunk.length === 0) throw new Error('the server closed the connection before sending a response');
            const from = received.length - 3;
            received = Buffer.concat([received, chunk]);
            headEnd = indexOfBytes(received, [13, 10, 13, 10], from);
            if (headEnd < 0 && received.length > MAX_HEADER) throw new Error('response headers are too large');
        }
        const lines = received.toString('latin1', 0, headEnd).split('\r\n');
        const status = /^HTTP\/\d\.\d (\d{3})(?: (.*))?$/.exec(lines[0]);
        if (!status) throw new Error(`invalid response: ${lines[0].slice(0, 80)}`);
        const responseHeaders = new Headers();
        for (const line of lines.slice(1)) {
            const colon = line.indexOf(':');
            if (colon > 0) responseHeaders.append(line.slice(0, colon), line.slice(colon + 1));
        }
        const encoding = (responseHeaders.get('content-encoding') || 'identity').toLowerCase();
        if (encoding !== 'identity') throw new Error(`unsupported content-encoding "${encoding}"`);

        const code = Number(status[1]);
        let raw = received.subarray(headEnd + 4);
        const expected = responseHeaders.get('content-length');
        const chunked = /chunked/i.test(responseHeaders.get('transfer-encoding') || '');
        let payload = null;
        if (method === 'HEAD' || code === 204 || code === 304 || code < 200) payload = Buffer.alloc(0);
        for (;;) {
            if (payload) break;
            if (chunked) payload = decodeChunked(raw);
            else if (expected !== null && raw.length >= Number(expected)) payload = raw.subarray(0, Number(expected));
            if (payload) break;
            const chunk = await connection.read();
            if (chunk.length === 0) {
                if (chunked || expected !== null) throw new Error('the response body was cut short');
                payload = raw;
                break;
            }
            raw = Buffer.concat([raw, chunk]);
            if (raw.length > MAX_RESPONSE) throw new Error('response body is too large');
        }
        return new Response(payload, { status: code, statusText: status[2] || '', headers: responseHeaders });
    } finally {
        connection.close();
        connections.delete(connection);
    }
}

async function fetchFollowing(input, init, connections) {
    let url = new URL(input instanceof URL ? input.href : String(input));
    let method = (init.method || 'GET').toUpperCase();
    const headers = new Headers(init.headers);
    let body = init.body === undefined || init.body === null ? null : toBytes(init.body, headers);
    const redirect = init.redirect || 'follow';
    let redirected = false;
    for (let hops = 0; ; hops++) {
        const response = await request(url, method, headers, body, connections);
        response.url = url.href; response.redirected = redirected;
        const location = response.headers.get('location');
        if (![301, 302, 303, 307, 308].includes(response.status) || location === null || redirect === 'manual') return response;
        if (redirect === 'error') throw new Error(`unexpected redirect to ${location}`);
        if (hops >= 20) throw new Error('too many redirects');
        const next = new URL(location, url.href);
        if (response.status === 303 || (response.status !== 307 && response.status !== 308 && method === 'POST')) {
            if (method !== 'HEAD') method = 'GET';
            body = null; headers.delete('content-type'); headers.delete('content-length');
        }
        if (next.origin !== url.origin) { headers.delete('authorization'); headers.delete('cookie'); }
        url = next; redirected = true;
    }
}

/* fetch(url, { method, headers, body, redirect, signal, timeout }): http and https, redirects followed, bodies
   buffered. `timeout` (ms, default 60000, 0 for none) is not part of the standard. */
async function fetch(input, init = {}) {
    const signal = init.signal;
    if (signal && signal.aborted) throw signal.reason;
    const timeout = init.timeout === undefined ? 60000 : init.timeout;
    const connections = new Set();
    let timer = 0, onAbort = null;
    const aborted = new Promise((resolve, reject) => {
        const abort = reason => { for (const c of connections) c.close(); reject(reason); };
        if (signal) { onAbort = () => abort(signal.reason); signal.addEventListener('abort', onAbort); }
        if (timeout > 0) timer = setTimeout_(() => abort(namedError('TimeoutError', 'The operation was aborted due to timeout')), timeout);
    });
    aborted.catch(() => {});
    try {
        return await Promise.race([fetchFollowing(input, init, connections), aborted]);
    } catch (error) {
        if (error && (error.name === 'AbortError' || error.name === 'TimeoutError')) throw error;
        const failure = new TypeError('fetch failed');
        failure.cause = error;
        throw failure;
    } finally {
        clearTimeout_(timer);
        if (signal && onAbort) signal.removeEventListener('abort', onAbort);
        for (const c of connections) c.close();
    }
}

/* ---------- module system ---------- */

const builtins = {
    events: EventEmitter, path, fs, os: nodeOs, util, assert, url: urlModule, querystring, http,
    buffer: { Buffer }, process, 'fs/promises': fs.promises, 'path/posix': path, 'assert/strict': assert,
    dns, 'dns/promises': dns.promises,
    timers: { setTimeout: setTimeout_, setInterval: setInterval_, setImmediate: setImmediate_, clearTimeout: clearTimeout_, clearInterval: clearTimeout_ },
};

function builtinOf(id) {
    return Object.prototype.hasOwnProperty.call(builtins, id) ? builtins[id] : undefined;
}

const RESERVED = new Set(['default', 'delete', 'new', 'class', 'function', 'var', 'let', 'const', 'import', 'export', 'if', 'else',
    'return', 'try', 'catch', 'this', 'typeof', 'void', 'with', 'yield', 'await', 'null', 'true', 'false', 'in', 'for', 'while', 'do',
    'switch', 'case', 'break', 'continue', 'throw', 'finally', 'super', 'enum', 'static', 'extends', 'instanceof', 'debugger']);

/* Source of the ES module the C loader compiles for `import x from 'http'`; undefined when it is not a builtin. */
function builtinModuleSource(id) {
    const mod = builtinOf(id);
    if (mod === undefined) return undefined;
    const names = (typeof mod === 'object' || typeof mod === 'function' ? Object.getOwnPropertyNames(mod) : [])
        .filter(k => /^[A-Za-z_$][\w$]*$/.test(k) && !RESERVED.has(k) && typeof mod[k] !== 'undefined' && !['length', 'name', 'prototype', 'arguments', 'caller'].includes(k));
    return `const m = globalThis.__requireBuiltin(${JSON.stringify(id)});\nexport default m;\n` +
        (names.length ? `export const { ${names.join(', ')} } = m;\n` : '');
}

const moduleCache = new Map();
let mainFile = null;

function notFound(id, from) {
    const err = new Error(`Cannot find module '${id}'\nRequire stack:\n- ${from}`);
    err.code = 'MODULE_NOT_FOUND';
    return err;
}

function isFile(p) { const [st, err] = os.stat(p); return !err && (st.mode & os.S_IFMT) === os.S_IFREG; }

function resolveFile(p) {
    for (const candidate of [p, p + '.js', p + '.json', p + '.cjs', p + '/index.js', p + '/index.json']) if (isFile(candidate)) return candidate;
    if (isFile(p + '/package.json')) {
        try {
            const main = JSON.parse(fs.readFileSync(p + '/package.json', 'utf8')).main;
            if (main) return resolveFile(path.resolve(p, main));
        } catch (e) { /* fall through */ }
    }
    return null;
}

function resolveModule(id, fromDir, fromFile) {
    if (id.startsWith('./') || id.startsWith('../') || id.startsWith('/') || id === '.' || id === '..') {
        const found = resolveFile(path.resolve(fromDir, id));
        if (found) return found;
        throw notFound(id, fromFile);
    }
    for (let dir = fromDir; ; dir = path.dirname(dir)) {
        const found = resolveFile(path.join(dir, 'node_modules', id));
        if (found) return found;
        if (dir === '/' || dir === '.') break;
    }
    throw notFound(id, fromFile);
}

function loadFile(file, parent) {
    if (moduleCache.has(file)) return moduleCache.get(file).exports;
    const module = { id: file === mainFile ? '.' : file, filename: file, exports: {}, loaded: false, parent, children: [], paths: [] };
    moduleCache.set(file, module);
    try {
        if (file.endsWith('.json')) {
            module.exports = JSON.parse(fs.readFileSync(file, 'utf8'));
        } else {
            let source = fs.readFileSync(file, 'utf8');
            if (source.startsWith('#!')) source = '//' + source;
            const wrapper = evalNamed('(function (exports, require, module, __filename, __dirname) {' + source + '\n})', file);
            const dir = path.dirname(file);
            wrapper.call(module.exports, module.exports, makeRequire(dir, file, module), module, file, dir);
        }
    } catch (e) {
        moduleCache.delete(file);
        throw e;
    }
    module.loaded = true;
    return module.exports;
}

function makeRequire(dir, file, parent) {
    const req = (id) => {
        id = String(id);
        const bare = id.startsWith('node:') ? id.slice(5) : id;
        const builtin = builtinOf(bare);
        if (builtin !== undefined) return builtin;
        if (id.startsWith('node:')) { const e = new Error(`No such built-in module: ${id}`); e.code = 'ERR_UNKNOWN_BUILTIN_MODULE'; throw e; }
        return loadFile(resolveModule(id, dir, file), parent);
    };
    req.resolve = (id) => builtinOf(id) !== undefined ? id : resolveModule(id, dir, file);
    req.cache = moduleCache;
    Object.defineProperty(req, 'main', { get: () => moduleCache.get(mainFile) });
    return req;
}

function runMain(file) {
    const abs = path.resolve(file);
    let source;
    try { source = fs.readFileSync(abs, 'utf8'); } catch (e) { std.err.puts(`node-lite: cannot read ${file}: ${e.message}\n`); std.exit(1); }
    process.argv = ['node-lite', abs, ...global.scriptArgs.slice(2)];
    global.global = global;
    if (abs.endsWith('.mjs') || /^\s*(import(\s+[\w{*"']|\s*[{*"'])|export\s)/m.test(source)) {
        return import(abs).catch(e => { std.err.puts(errorText(e) + '\n'); std.exit(1); });
    }
    mainFile = abs;
    try {
        loadFile(abs, null);
    } catch (e) {
        std.err.puts(errorText(e) + '\n');
        std.exit(1);
    }
}

/* ---------- globals ---------- */

Object.assign(global, {
    process, Buffer, URL, URLSearchParams, TextEncoder, TextDecoder,
    fetch, Headers, Response, AbortController, AbortSignal,
    setTimeout: setTimeout_, setInterval: setInterval_, setImmediate: setImmediate_,
    clearTimeout: clearTimeout_, clearInterval: clearTimeout_, clearImmediate: clearTimeout_,
    __requireBuiltin: id => builtinOf(id), __builtinModuleSource: builtinModuleSource, __runMain: runMain,
});
if (typeof global.queueMicrotask !== 'function') global.queueMicrotask = fn => { Promise.resolve().then(fn); };
if (typeof global.structuredClone !== 'function') global.structuredClone = v => JSON.parse(JSON.stringify(v));
global.global = global;
})(globalThis);
