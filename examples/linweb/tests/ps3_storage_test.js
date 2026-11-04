// Drives ps3/js/storage.js + ps3/js/app.js with a tiny fake DOM and an in-memory
// linweb.storage, pressing keys like the remote control would.
//   node tests/ps3_storage_test.js
const fs = require('fs');
const path = require('path');
const vm = require('vm');

let failures = 0;
function check(cond, message) {
    if (!cond) { failures++; console.log('FAIL: ' + message); }
}
function same(actual, expected, message) {
    const a = JSON.stringify(actual), e = JSON.stringify(expected);
    if (a !== e) { failures++; console.log('FAIL: ' + message + '\n  expected ' + e + '\n  actual   ' + a); }
}

// ---- fake DOM -------------------------------------------------------------
class El {
    constructor(tag) {
        this.tag = tag; this.children = []; this.attrs = {}; this._class = '';
        this.innerText = ''; this.style = {}; this.listeners = {};
    }
    get className() { return this._class; }
    set className(v) { this._class = v; }
    get classList() {
        const self = this;
        const names = () => self._class.split(/\s+/).filter(Boolean);
        return {
            add: (...c) => { const n = names(); c.forEach(x => { if (!n.includes(x)) n.push(x); }); self._class = n.join(' '); },
            remove: (...c) => { self._class = names().filter(x => !c.includes(x)).join(' '); },
            contains: (c) => names().includes(c)
        };
    }
    set innerHTML(v) { this.children = []; }
    get innerHTML() { return ''; }
    appendChild(c) { this.children.push(c); return c; }
    getAttribute(n) { return this.attrs[n]; }
    addEventListener(t, cb) { (this.listeners[t] = this.listeners[t] || []).push(cb); }
    has(cls) { return this._class.split(/\s+/).includes(cls); }
}

const root = new El('body');
const byId = {};
function add(parent, tag, id, cls) {
    const el = new El(tag); el._class = cls || ''; if (id) byId[id] = el; parent.appendChild(el); return el;
}
const mainMenu = add(root, 'nav', 'main-menu');
['System', 'Games', 'Multimedia', 'DVD', 'Storage'].forEach(name => {
    const item = add(mainMenu, 'div', null, 'menu-item'); item.attrs['data-name'] = name;
});
add(root, 'div', 'submenu-container'); add(root, 'div', 'status'); add(root, 'pre', 'system-info');
const panelEl = add(root, 'div', 'storage-panel');
add(panelEl, 'div', 'sp-title'); add(panelEl, 'pre', 'sp-subtitle'); add(panelEl, 'div', 'sp-list'); add(panelEl, 'div', 'sp-hint');

function walk(el, out, cls) { if (el.has(cls)) out.push(el); el.children.forEach(c => walk(c, out, cls)); return out; }
const docListeners = {};
const timers = [];
const document = {
    getElementById: (id) => byId[id],
    createElement: (tag) => new El(tag),
    querySelectorAll: (sel) => walk(root, [], sel.replace(/^\./, '')),
    addEventListener: (t, cb) => { (docListeners[t] = docListeners[t] || []).push(cb); }
};
function press(keyCode) { (docListeners.keydown || []).forEach(cb => cb({ keyCode: keyCode, key: keyCode === 13 ? 'Enter' : '' })); }
const KEY = { left: 37, up: 38, right: 39, down: 40, enter: 13, esc: 27 };
function runTimers() { while (timers.length) timers.shift()(); }

// ---- in-memory linweb.storage (same shapes as the native bindings) ----------
const MB = 1024 * 1024;
const disk = (name, mb, extra) => Object.assign({ name, path: '/dev/' + name, model: '', fstype: '', label: '', mountpoint: '', mounted: false,
    removable: false, readOnly: false, optical: false, sizeBytes: mb * MB, partitions: [] }, extra || {});
const world = {
    disks: [
        disk('vda', 512, { model: 'QEMU HARDDISK' }),
        disk('sdb', 64, { fstype: 'vfat', label: 'DATA' }),
        disk('sdc', 128, { partitions: [{ name: 'sdc1', path: '/dev/sdc1', fstype: 'ext4', label: '', mountpoint: '', mounted: false, sizeBytes: 100 * MB }] }),
        disk('sr0', 0, { optical: true, removable: true })
    ],
    files: { }, formatCalls: []
};
function find(p) { for (const d of world.disks) { if (d.path === p) return d; for (const x of d.partitions) if (x.path === p) return x; } return null; }
const linweb = {
    renderer: { info: () => ({ session: 'Test', vendor: 'v', renderer: 'r', version: '1', shadingLanguageVersion: '1', software: false }) },
    debug: { renderer: () => {}, glErrors: () => [] },
    storage: {
        list: () => JSON.parse(JSON.stringify(world.disks)),
        mount: (dev) => {
            const d = find(dev); if (!d) return { ok: false, error: 'no such device', mountpoint: '' };
            if (!d.fstype) return { ok: false, error: 'no supported filesystem found (format the device first)', mountpoint: '' };
            d.mounted = true; d.mountpoint = '/mnt/' + dev.slice(5); world.files[d.mountpoint] = world.files[d.mountpoint] || {};
            return { ok: true, error: '', mountpoint: d.mountpoint };
        },
        unmount: (mp) => {
            for (const d of world.disks) for (const x of [d].concat(d.partitions)) if (x.mountpoint === mp) { x.mounted = false; x.mountpoint = ''; return { ok: true, error: '' }; }
            return { ok: false, error: 'not a mount point' };
        },
        format: (dev, fsName, label, confirm) => {
            world.formatCalls.push([dev, fsName, label, confirm]);
            const d = find(dev);
            if (!d || confirm !== dev.slice(5)) return { ok: false, error: 'confirmation does not match the device name' };
            if (d.mounted) return { ok: false, error: 'unmount first' };
            if (dev === '/dev/sdb') return { ok: false, error: 'mkfs.ext4 failed: simulated error' };
            d.fstype = fsName; d.label = label; if (d.partitions) d.partitions = [];
            return { ok: true, error: '' };
        },
        readDir: (p) => {
            const files = world.files[p.split('/').slice(0, 3).join('/')];
            if (!files) return { ok: false, error: 'No such file or directory', entries: [], truncated: false };
            const prefix = p === p.split('/').slice(0, 3).join('/') ? '' : p.split('/').slice(3).join('/') + '/';
            const seen = {}; const entries = [];
            Object.keys(files).forEach(k => {
                if (!k.startsWith(prefix)) return;
                const rest = k.slice(prefix.length); const slash = rest.indexOf('/');
                const name = slash < 0 ? rest : rest.slice(0, slash);
                if (seen[name]) return; seen[name] = true;
                entries.push({ name, dir: slash >= 0, size: slash >= 0 ? 0 : files[k].length });
            });
            entries.sort((a, b) => (b.dir - a.dir) || a.name.localeCompare(b.name));
            return { ok: true, error: '', entries, truncated: false };
        },
        readFile: (p, max) => {
            const mp = p.split('/').slice(0, 3).join('/'); const key = p.split('/').slice(3).join('/');
            const content = (world.files[mp] || {})[key];
            return content === undefined ? { ok: false, error: 'No such file or directory', data: '' } : { ok: true, error: '', data: content.slice(0, max || content.length) };
        },
        writeFile: (p, data) => {
            const mp = p.split('/').slice(0, 3).join('/'); const key = p.split('/').slice(3).join('/');
            if (!world.files[mp]) return { ok: false, error: 'No such file or directory' };
            world.files[mp][key] = data; return { ok: true, error: '' };
        },
        makeDir: () => ({ ok: true, error: '' }), remove: () => ({ ok: true, error: '' })
    }
};

// ---- load the page scripts the way the engine does ------------------------
const context = vm.createContext({
    document, linweb, console: { log: () => {}, error: console.error, warn: console.warn },
    window: {}, setTimeout: (cb) => { timers.push(cb); }, Date, Math, JSON
});
const ps3 = path.join(__dirname, '..', 'ps3', 'js');
['storage.js', 'app.js'].forEach(file => vm.runInContext(fs.readFileSync(path.join(ps3, file), 'utf8'), context, { filename: file }));
(docListeners.DOMContentLoaded || []).forEach(cb => cb());

// ---- helpers over the rendered state ---------------------------------------
const status = () => byId['status'].innerText;
const panelVisible = () => panelEl.has('visible');
const rows = () => byId['sp-list'].children.map(c => c.innerText);
const activeRow = () => { const a = byId['sp-list'].children.find(c => c.has('active')); return a ? a.innerText : null; };
const subtitle = () => byId['sp-subtitle'].innerText;
const title = () => byId['sp-title'].innerText;
const submenu = () => byId['submenu-container'].children.map(c => c.innerText);
function selectRow(label) {
    for (let i = 0; i < 30 && activeRow() !== label; i++) press(KEY.down);
    check(activeRow() === label, 'row "' + label + '" is reachable (active: ' + activeRow() + ')');
}

// ---- scenario ---------------------------------------------------------------
check(status() === 'SYSTEM', 'starts on System');
check(!panelVisible(), 'panel hidden at start');
for (let i = 0; i < 4; i++) press(KEY.right);
check(status() === 'STORAGE', 'Right x4 reaches Storage, status=' + status());
same(submenu(), ['vda   512 MB', 'sdb   64 MB', 'sdc   128 MB', '- sdc1   100 MB', 'sr0   0 B', 'Refresh'], 'storage submenu lists disks and partitions');

press(KEY.down);                                  // enter the submenu (vda)
press(KEY.enter);
check(panelVisible() && title() === 'Disk vda', 'Enter opens the disk panel: ' + title());
check(subtitle().includes('Model: QEMU HARDDISK') && subtitle().includes('Filesystem: none') && subtitle().includes('Size: 512 MB'), 'disk details: ' + JSON.stringify(subtitle()));
same(rows(), ['Format as ext4', 'Format as FAT', 'Close'], 'blank disk offers formatting only');

// Cancel is the default in the confirmation and nothing is formatted.
press(KEY.enter);
check(title() === 'Format vda' && activeRow() === 'Cancel', 'confirmation defaults to Cancel');
check(subtitle().includes('ERASE ALL DATA on /dev/vda'), 'confirmation warns about data loss');
press(KEY.enter);
check(title() === 'Disk vda' && world.formatCalls.length === 0, 'Cancel returns to the panel without formatting');

// Format as ext4 -> confirm -> busy -> message -> actions.
press(KEY.enter); press(KEY.right); check(activeRow() === 'Erase everything', 'Right selects Erase');
press(KEY.enter);
check(title() === 'Please wait' && world.formatCalls.length === 0, 'busy screen is drawn before mkfs runs');
press(KEY.enter);
check(title() === 'Please wait', 'input is ignored while busy');
runTimers();
check(title() === 'Format complete' && world.formatCalls.length === 1, 'format ran after the busy frame: ' + title());
same(world.formatCalls[0], ['/dev/vda', 'ext4', 'MLINXOS', 'vda'], 'format called with device, fs, label and confirmation name');
press(KEY.enter);
check(title() === 'Disk vda' && subtitle().includes('Filesystem: ext4'), 'panel shows the new ext4 filesystem');
same(rows(), ['Mount', 'Format as ext4', 'Format as FAT', 'Close'], 'formatted disk offers Mount');
check(submenu()[0] === 'vda   512 MB', 'submenu row for vda is unchanged');

// Mount, write a file, browse, view, unmount.
press(KEY.enter);
check(title() === 'Mounted' && subtitle().includes('/mnt/vda'), 'mounted: ' + subtitle());
press(KEY.enter);
same(rows(), ['Browse files', 'Create test file', 'Unmount', 'Close'], 'mounted device actions');
check(submenu()[0] === 'vda   512 MB   [mounted]', 'submenu marks the mounted disk: ' + submenu()[0]);
selectRow('Create test file'); press(KEY.enter);
check(title() === 'File written' && subtitle() === 'Wrote /mnt/vda/linweb-test.txt', 'test file written: ' + subtitle());
check((world.files['/mnt/vda']['linweb-test.txt'] || '').startsWith('MLinxOS linweb storage test'), 'file content stored');
press(KEY.enter);
check(rows()[0] === 'Browse files', 'back to actions'); press(KEY.up); press(KEY.up); press(KEY.up);
selectRow('Browse files'); press(KEY.enter);
check(title() === 'Files: /mnt/vda' && rows().length === 1 && rows()[0].startsWith('linweb-test.txt'), 'browser lists the file: ' + JSON.stringify(rows()));
press(KEY.enter);
check(title() === 'View: linweb-test.txt' && subtitle().startsWith('MLinxOS linweb storage test'), 'viewer shows the file');
press(KEY.esc);
check(title() === 'Files: /mnt/vda', 'Esc leaves the viewer for the browser');
press(KEY.esc);
check(title() === 'Disk vda', 'Esc at the mount root returns to the device actions');
selectRow('Unmount'); press(KEY.enter);
check(title() === 'Unmounted', 'unmounted'); press(KEY.enter);
same(rows(), ['Mount', 'Format as ext4', 'Format as FAT', 'Close'], 'unmounted device offers Mount again');
selectRow('Close'); press(KEY.enter);
check(!panelVisible() && status() === 'STORAGE', 'Close hides the panel');

// A failing mkfs shows the error and keeps the device usable.
press(KEY.down);                                  // sdb (vfat DATA)
press(KEY.enter);
check(title() === 'Disk sdb' && subtitle().includes('Label: DATA'), 'FAT disk details');
selectRow('Format as ext4'); press(KEY.enter); press(KEY.right); press(KEY.enter); runTimers();
check(title() === 'Format failed' && subtitle().includes('simulated error'), 'format error is shown: ' + subtitle());
press(KEY.esc);
check(title() === 'Disk sdb', 'message dismissed with Esc');
press(KEY.esc);
check(!panelVisible(), 'Esc closes the panel');

// A disk with a partition table warns before wiping it; partition rows work too.
press(KEY.down); press(KEY.enter);
check(title() === 'Disk sdc' && subtitle().includes('partition table (1)'), 'partitioned disk details: ' + subtitle());
press(KEY.enter);
check(subtitle().includes('1 partition(s) of this disk will be lost'), 'whole-disk format warns about partitions');
press(KEY.esc); press(KEY.esc);
press(KEY.down); press(KEY.enter);
check(title() === 'Partition sdc1', 'partition opens its own panel: ' + title());
press(KEY.esc);

// Optical drives are never offered formatting; Refresh keeps the position.
press(KEY.down); press(KEY.enter);
check(title() === 'Disk sr0', 'optical drive opens'); same(rows(), ['Close'], 'optical drive only offers Close');
press(KEY.esc);
press(KEY.down); press(KEY.enter);
check(status() === 'Storage list refreshed' && submenu().length === 6, 'Refresh rebuilds the list');
check(byId['submenu-container'].children[5].has('active'), 'Refresh keeps the selection on its row');

// Leaving Storage works as before and other categories are untouched.
press(KEY.esc);
press(KEY.left);
check(status() === 'DVD', 'Left from Storage reaches DVD: ' + status());
same(submenu(), ['Play Disc', 'Disc Info', 'Eject'], 'other categories keep their static items');

console.log(failures ? failures + ' check(s) failed' : 'all PS3 storage UI checks passed');
process.exit(failures ? 1 : 0);
