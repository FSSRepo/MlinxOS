// Storage manager for the PS3-style menu: disk list, per-device actions, file
// browser and format confirmation. It talks to the engine through linweb.storage.
// app.js creates it with createStorageUI() and forwards key events while it is open.
function createStorageUI(options) {
    const PANEL_ROWS = 8;
    const VIEW_LINES = 12;
    const VIEW_BYTES = 4096;

    const ui = options;           // { panel, title, subtitle, list, hint, setStatus, onItemsChanged }
    let items = [];               // entries behind the Storage submenu rows
    const state = {
        mode: null,               // null | actions | browser | viewer | confirm | message | busy
        ctx: null,                // { dev, disk, isDisk } of the device being managed
        actions: [],
        rows: [],
        index: 0,
        root: '',
        path: '',
        browserIndex: 0,
        fileName: '',
        lines: [],
        offset: 0,
        fs: '',
        title: '',
        text: '',
        after: null
    };

    function fmtSize(bytes) {
        if (!bytes) return '0 B';
        const units = ['B', 'KB', 'MB', 'GB', 'TB'];
        let value = bytes;
        let unit = 0;
        while (value >= 1024 && unit < units.length - 1) {
            value /= 1024;
            unit++;
        }
        return (unit === 0 ? String(value) : String(Math.round(value * 10) / 10)) + ' ' + units[unit];
    }

    function listDisks() {
        try {
            return linweb.storage.list();
        } catch (err) {
            console.error('storage list failed', err);
            return null;
        }
    }

    function findDevice(path) {
        const disks = listDisks() || [];
        for (const disk of disks) {
            if (disk.path === path) return { dev: disk, disk: disk, isDisk: true };
            for (const part of disk.partitions) {
                if (part.path === path) return { dev: part, disk: disk, isDisk: false };
            }
        }
        return null;
    }

    function describe(ctx) {
        const dev = ctx.dev;
        const lines = ['Path: ' + dev.path, 'Size: ' + fmtSize(dev.sizeBytes)];
        if (ctx.isDisk && ctx.disk.model) lines.push('Model: ' + ctx.disk.model);
        const fs = dev.fstype || (ctx.isDisk && dev.partitions.length ? 'partition table (' + dev.partitions.length + ')' : 'none');
        lines.push('Filesystem: ' + fs);
        if (dev.label) lines.push('Label: ' + dev.label);
        lines.push('Mounted: ' + (dev.mounted ? dev.mountpoint : 'no'));
        if (ctx.disk.optical) lines.push('Optical drive (read only)');
        else if (ctx.disk.readOnly) lines.push('Read-only device');
        return lines.join('\n');
    }

    // ---- submenu rows ----------------------------------------------------
    function buildItems() {
        items = [];
        const disks = listDisks();
        if (disks === null) {
            items.push({ label: 'Storage unavailable', kind: 'none' });
            return;
        }
        disks.forEach(disk => {
            items.push({ label: disk.name + '   ' + fmtSize(disk.sizeBytes) + (disk.mounted ? '   [mounted]' : ''), kind: 'device', path: disk.path });
            disk.partitions.forEach(part => {
                items.push({ label: '- ' + part.name + '   ' + fmtSize(part.sizeBytes) + (part.mounted ? '   [mounted]' : ''), kind: 'device', path: part.path });
            });
        });
        if (!disks.length) items.push({ label: 'No disks detected', kind: 'none' });
        items.push({ label: 'Refresh', kind: 'refresh' });
    }

    function labels() {
        return items.map(item => item.label);
    }

    // ---- rendering --------------------------------------------------------
    function showRows(rows, index, dangerIndex) {
        ui.list.innerHTML = '';
        const start = Math.min(Math.max(0, index - 3), Math.max(0, rows.length - PANEL_ROWS));
        const end = Math.min(rows.length, start + PANEL_ROWS);
        for (let i = start; i < end; i++) {
            const row = document.createElement('div');
            row.className = 'sp-item' + (i === index ? ' active' : '') + (i === dangerIndex ? ' danger' : '');
            row.innerText = rows[i];
            ui.list.appendChild(row);
        }
    }

    function draw(title, subtitle, rows, index, hint, dangerIndex) {
        ui.panel.classList.add('visible');
        ui.title.innerText = title;
        ui.subtitle.innerText = subtitle;
        showRows(rows, index, dangerIndex === undefined ? -1 : dangerIndex);
        ui.hint.innerText = hint;
    }

    function render() {
        const ctx = state.ctx;
        if (state.mode === 'actions') {
            draw((ctx.isDisk ? 'Disk ' : 'Partition ') + ctx.dev.name, describe(ctx),
                state.actions.map(a => a.label), state.index, 'Up/Down: select   Enter: run   Esc: close');
        } else if (state.mode === 'browser') {
            const rows = browserRows();
            const sub = state.text || (state.entries.length + ' item(s)' + (state.truncated ? ' (list truncated)' : ''));
            draw('Files: ' + state.path, sub, rows.map(r => r.label), state.index,
                'Up/Down: select   Enter: open   Esc: back   ' + (state.index + 1) + '/' + rows.length);
        } else if (state.mode === 'viewer') {
            const text = state.lines.slice(state.offset, state.offset + VIEW_LINES).join('\n');
            draw('View: ' + state.fileName, text, [], -1,
                'Up/Down: scroll   Esc: back   (first ' + VIEW_BYTES + ' bytes)');
        } else if (state.mode === 'confirm') {
            const warn = 'ERASE ALL DATA on ' + ctx.dev.path + ' (' + fmtSize(ctx.dev.sizeBytes) + ') and create ' +
                (state.fs === 'vfat' ? 'a FAT' : 'an ext4') + ' filesystem?\nThis cannot be undone.' +
                (ctx.isDisk && ctx.dev.partitions.length ? '\nThe ' + ctx.dev.partitions.length + ' partition(s) of this disk will be lost.' : '');
            draw('Format ' + ctx.dev.name, warn, ['Cancel', 'Erase everything'], state.index,
                'Left/Right: choose   Enter: confirm   Esc: cancel', 1);
        } else if (state.mode === 'message') {
            draw(state.title, state.text, ['OK'], 0, 'Enter: continue');
        } else if (state.mode === 'busy') {
            draw(state.title, state.text, [], -1, '');
        }
    }

    function close() {
        state.mode = null;
        state.ctx = null;
        ui.panel.classList.remove('visible');
        ui.setStatus('STORAGE');
    }

    // ---- modes ------------------------------------------------------------
    function deviceActions(ctx) {
        const dev = ctx.dev;
        const actions = [];
        const busyParts = ctx.isDisk && dev.partitions.some(p => p.mounted);
        if (dev.mounted) {
            actions.push({ label: 'Browse files', run: () => showBrowser(dev.mountpoint, dev.mountpoint) });
            actions.push({ label: 'Create test file', run: () => createTestFile(ctx) });
            actions.push({ label: 'Unmount', run: () => unmount(ctx) });
        } else if (dev.fstype && !ctx.disk.optical) {
            actions.push({ label: 'Mount', run: () => mount(ctx) });
        }
        if (!ctx.disk.optical && !ctx.disk.readOnly && !dev.mounted && !busyParts) {
            actions.push({ label: 'Format as ext4', run: () => showConfirm(ctx, 'ext4') });
            actions.push({ label: 'Format as FAT', run: () => showConfirm(ctx, 'vfat') });
        }
        actions.push({ label: 'Close', run: close });
        return actions;
    }

    function showActions(ctx, keepIndex) {
        state.mode = 'actions';
        state.ctx = ctx;
        state.actions = deviceActions(ctx);
        state.index = keepIndex ? Math.min(state.index, state.actions.length - 1) : 0;
        ui.setStatus('STORAGE: ' + ctx.dev.path);
        render();
    }

    function showMessage(title, text, after) {
        state.mode = 'message';
        state.title = title;
        state.text = text;
        state.after = after;
        render();
    }

    // Refresh the device list, then show a message that returns to the device actions.
    function finish(title, text, devPath) {
        ui.onItemsChanged();
        showMessage(title, text, () => {
            const ctx = findDevice(devPath);
            if (ctx) showActions(ctx);
            else close();
        });
    }

    function mount(ctx) {
        const result = linweb.storage.mount(ctx.dev.path);
        finish(result.ok ? 'Mounted' : 'Mount failed', result.ok ? ctx.dev.path + ' mounted at ' + result.mountpoint : result.error, ctx.dev.path);
    }

    function unmount(ctx) {
        const result = linweb.storage.unmount(ctx.dev.mountpoint);
        finish(result.ok ? 'Unmounted' : 'Unmount failed', result.ok ? ctx.dev.path + ' is no longer mounted.' : result.error, ctx.dev.path);
    }

    function createTestFile(ctx) {
        const path = ctx.dev.mountpoint + '/linweb-test.txt';
        const body = 'MLinxOS linweb storage test\nwritten: ' + new Date().toISOString() + '\ndevice: ' + ctx.dev.path + '\n';
        const result = linweb.storage.writeFile(path, body);
        finish(result.ok ? 'File written' : 'Write failed', result.ok ? 'Wrote ' + path : result.error, ctx.dev.path);
    }

    function showConfirm(ctx, fs) {
        state.mode = 'confirm';
        state.ctx = ctx;
        state.fs = fs;
        state.index = 0;       // Cancel is the default choice
        render();
    }

    function runFormat() {
        const ctx = state.ctx;
        const fs = state.fs;
        state.mode = 'busy';
        state.title = 'Please wait';
        state.text = 'Formatting ' + ctx.dev.path + ' as ' + (fs === 'vfat' ? 'FAT' : 'ext4') + '...';
        render();
        // mkfs blocks the UI, so let this frame draw the message first.
        setTimeout(() => {
            const result = linweb.storage.format(ctx.dev.path, fs, 'MLINXOS', ctx.dev.name);
            finish(result.ok ? 'Format complete' : 'Format failed',
                result.ok ? ctx.dev.path + ' now holds an empty ' + (fs === 'vfat' ? 'FAT' : 'ext4') + ' filesystem.' : result.error,
                ctx.dev.path);
        }, 60);
    }

    function joinPath(base, name) {
        return base.replace(/\/+$/, '') + '/' + name;
    }

    function parentPath(path) {
        const cut = path.lastIndexOf('/');
        return cut <= 0 ? '/' : path.substring(0, cut);
    }

    function browserRows() {
        const rows = [];
        if (state.path !== state.root) rows.push({ label: '..', up: true });
        state.entries.forEach(entry => {
            rows.push({
                label: entry.dir ? '[DIR] ' + entry.name : entry.name + '   (' + fmtSize(entry.size) + ')',
                name: entry.name,
                dir: entry.dir
            });
        });
        return rows;
    }

    function showBrowser(path, root, restoreIndex) {
        const result = linweb.storage.readDir(path);
        state.mode = 'browser';
        state.root = root === undefined ? state.root : root;
        state.path = path;
        state.entries = result.ok ? result.entries : [];
        state.truncated = result.ok && result.truncated;
        state.text = result.ok ? '' : result.error;
        state.index = restoreIndex || 0;
        render();
    }

    function showViewer(path, name) {
        const result = linweb.storage.readFile(path, VIEW_BYTES);
        state.mode = 'viewer';
        state.fileName = name;
        state.lines = result.ok ? result.data.split('\n') : ['Cannot read file: ' + result.error];
        state.offset = 0;
        render();
    }

    // ---- input -------------------------------------------------------------
    function move(step, length) {
        state.index = Math.max(0, Math.min(length - 1, state.index + step));
        render();
    }

    function handleKey(e) {
        const code = e.keyCode;
        const up = code === 38;
        const down = code === 40;
        const left = code === 37;
        const right = code === 39;
        const enter = code === 13 || code === 10 || e.key === 'Enter';
        const back = code === 27 || code === 461 || code === 8;

        if (state.mode === 'actions') {
            if (up) move(-1, state.actions.length);
            else if (down) move(1, state.actions.length);
            else if (enter) state.actions[state.index].run();
            else if (back) close();
        } else if (state.mode === 'browser') {
            const rows = browserRows();
            if (up) move(-1, rows.length);
            else if (down) move(1, rows.length);
            else if (enter && rows.length) {
                const row = rows[state.index];
                if (row.up) showBrowser(parentPath(state.path));
                else if (row.dir) showBrowser(joinPath(state.path, row.name));
                else {
                    state.browserIndex = state.index;
                    showViewer(joinPath(state.path, row.name), row.name);
                }
            } else if (back) {
                if (state.path !== state.root) showBrowser(parentPath(state.path));
                else showActions(findDevice(state.ctx.dev.path) || state.ctx);
            }
        } else if (state.mode === 'viewer') {
            if (up && state.offset > 0) { state.offset--; render(); }
            else if (down && state.offset < state.lines.length - VIEW_LINES) { state.offset++; render(); }
            else if (back || enter) showBrowser(state.path, undefined, state.browserIndex);
        } else if (state.mode === 'confirm') {
            if (left || up) { state.index = 0; render(); }
            else if (right || down) { state.index = 1; render(); }
            else if (enter) {
                if (state.index === 1) runFormat();
                else showActions(state.ctx);
            } else if (back) showActions(state.ctx);
        } else if (state.mode === 'message') {
            if (enter || back) state.after();
        }
        // 'busy' ignores input until the blocking operation ends.
    }

    function activate(index) {
        const item = items[index];
        if (!item) return;
        if (item.kind === 'refresh') {
            ui.onItemsChanged();
            ui.setStatus('Storage list refreshed');
            return;
        }
        if (item.kind !== 'device') return;
        const ctx = findDevice(item.path);
        if (!ctx) {
            ui.onItemsChanged();
            return;
        }
        showActions(ctx);
    }

    return {
        buildItems: buildItems,
        labels: labels,
        activate: activate,
        handleKey: handleKey,
        isOpen: () => state.mode !== null
    };
}
