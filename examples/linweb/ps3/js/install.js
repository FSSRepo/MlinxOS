// Install to disk, from System > Install to Disk. Lists the disks that can be erased, asks for the disk name to
// confirm, runs /usr/sbin/mlinx-install and shows its progress. Keys: Up/Down (or the D-pad) choose, Enter confirms,
// Esc goes back. The name is typed into a field, with the on-screen keyboard (js/osk.js) for use without a keyboard.
// The disks go in the column of the XMB like the other option lists; the confirmation and the progress take the
// whole screen, like the Wi-Fi password.
const installUI = (() => {
    const CHECK_EVERY_FRAMES = 30;   // the installer's progress is read about twice a second
    const LOG_LINES = 10;
    const PANEL_ROWS = 8;
    const state = { step: null, disks: [], index: 0, target: null, message: '', frames: 0, status: null, level: 0, fieldShown: false };

    const el = (id) => document.getElementById(id);
    const ui = {
        panel: el('in-panel'),
        title: el('in-title'),
        subtitle: el('in-subtitle'),
        list: el('in-list'),
        input: el('in-input'),
        keyboard: el('in-osk'),
        hint: el('in-hint')
    };

    const osk = (ui.keyboard && typeof createOnScreenKeyboard === 'function')
        ? createOnScreenKeyboard({ container: ui.keyboard, input: ui.input, onGo: () => confirmName() })
        : null;

    function isOpen() { return state.step !== null; }

    function gigabytes(bytes) { return (bytes / 1e9).toFixed(1) + ' GB'; }

    // The boot medium is the optical drive or the disk the system runs from, so both are left out, with
    // any disk that has something mounted.
    function usable(disk) {
        return !disk.optical && !disk.readOnly && !disk.mounted && !disk.partitions.some(p => p.mounted);
    }

    function tail(log) {
        return log.split('\n').filter(line => line !== '').slice(-LOG_LINES).join('\n');
    }

    // ---- rendering --------------------------------------------------------
    function showRows(rows, index, dangerIndex) {
        const start = Math.min(Math.max(0, index - 3), Math.max(0, rows.length - PANEL_ROWS));
        const end = Math.min(rows.length, start + PANEL_ROWS);
        const cells = xmbListRows(ui.list, end - start);
        for (let i = start; i < end; i++) {
            const entry = rows[i];
            const row = cells[i - start];
            row.className = 'sp-item' + (i === index ? ' active' : '') + (i === dangerIndex ? ' danger' : '');
            xmbFillItem(row, entry.label, entry.icon, entry.meta);
        }
    }

    function draw(title, subtitle, rows, index, hint, full) {
        const wasVisible = ui.panel.classList.contains('visible');
        if (full && !ui.panel.classList.contains('fullscreen')) {
            // grow from the column: show it where it normally is first
            ui.panel.classList.remove('fullscreen');
            xmbSetPanel(ui.panel, true);
            void ui.panel.offsetWidth;
        }
        ui.panel.classList.toggle('fullscreen', full);
        xmbSetPanel(ui.panel, true);
        if (!wasVisible) xmbZoomIn(ui.panel);
        ui.title.innerText = title;
        ui.subtitle.innerText = subtitle;
        showRows(rows, index, -1);
        ui.hint.innerText = hint;
        xmbRevealSelection(ui.panel);
    }

    // Deeper screens slide in from the right, going back slides in from the left (as in the storage panel)
    const LEVEL = { disks: 1, confirm: 2, running: 2, done: 2 };
    function slideBetweenLevels() {
        const level = LEVEL[state.step] || 0;
        if (level && state.level && level !== state.level) {
            if (level > state.level) xmbSlideForward(ui.list);
            else xmbSlideBack(ui.list);
        }
        state.level = level;
    }

    function diskRows() {
        const rows = state.disks.map(d => ({ label: d.path, icon: 'database', meta: gigabytes(d.sizeBytes) + (d.model ? '  ' + d.model : '') }));
        rows.push({ label: 'Cancel', icon: 'close' });
        return rows;
    }

    function showField() {
        ui.input.value = '';
        ui.input.classList.add('visible');
        ui.input.focus();
        state.fieldShown = true;
        if (osk) osk.show();
    }

    function hideField() {
        ui.input.blur();
        ui.input.classList.remove('visible');
        ui.input.value = '';
        state.fieldShown = false;
        if (osk) osk.hide();
    }

    function render() {
        if (state.step !== 'confirm' && state.fieldShown) hideField();
        slideBetweenLevels();
        if (state.step === 'disks') {
            const sub = state.disks.length
                ? state.disks.length + ' disk(s) can be erased'
                : 'No disk can be erased here (the boot medium and mounted disks are hidden)';
            draw('Install MLinxOS to disk', sub, diskRows(), state.index,
                'Up/Down: select   Enter: next   Esc: close', false);
        } else if (state.step === 'confirm') {
            const d = state.target;
            draw('Erase ' + d.path + '?',
                'This ERASES everything on ' + d.path + ' (' + gigabytes(d.sizeBytes) + ') and cannot be undone.\n' +
                'Type ' + d.name + ' to confirm and press Enter.' + (state.message ? '\n' + state.message : ''),
                [], -1, 'Enter: install   Esc: back', true);
            if (!state.fieldShown) showField();
        } else if (state.step === 'running') {
            draw('Installing on ' + state.target.path, tail(state.status ? state.status.log : ''), [], -1,
                'Do not turn the computer off', true);
        } else if (state.step === 'done') {
            const st = state.status;
            const ok = st.exitCode === 0;
            draw(ok ? 'Installed on ' + state.target.path : 'Install failed (exit code ' + st.exitCode + ')',
                tail(st.log) + (ok ? '\n\nRemove the CD and start the computer again.' : ''),
                [{ label: 'OK', icon: ok ? 'check' : 'alert' }], 0, 'Enter or Esc: close', true);
        }
    }

    // ---- actions ----------------------------------------------------------
    function open() {
        state.step = 'disks';
        state.disks = linweb.storage.list().filter(usable);
        state.index = 0;
        state.target = null;
        state.message = '';
        state.level = 0;
        render();
    }

    function close() {
        state.step = null;
        state.level = 0;
        hideField();
        ui.panel.classList.remove('fullscreen');
        xmbSetPanel(ui.panel, false);
    }

    function refresh() {
        const st = linweb.system.installStatus();
        state.status = st;
        if (st.done) state.step = 'done';
        render();
    }

    function startInstall() {
        const result = linweb.system.install(state.target.path, ui.input.value);
        if (!result.ok) {
            state.message = result.error;
            render();
            return;
        }
        state.step = 'running';
        state.frames = 0;
        state.status = null;
        refresh();
    }

    function confirmName() {
        if (state.step !== 'confirm') return;
        if (ui.input.value !== state.target.name) {
            state.message = 'Type ' + state.target.name + ' to confirm.';
            render();
            return;
        }
        startInstall();
    }

    function handleKey(e) {
        const code = e.keyCode;
        const enter = e.key === 'Enter' || code === 13 || code === 10;
        const back = e.key === 'Escape' || code === 27 || code === 461;

        if (state.step === 'disks') {
            const count = diskRows().length;
            if (code === 38 && state.index > 0) state.index--;
            else if (code === 40 && state.index < count - 1) state.index++;
            else if (enter) {
                if (state.index >= state.disks.length) { close(); return; }      // Cancel
                state.target = state.disks[state.index];
                state.message = '';
                state.step = 'confirm';
            } else if (back) { close(); return; }
            render();
        } else if (state.step === 'confirm') {
            // While the on-screen keyboard is open, the arrows and Enter move and press its keys
            if (osk && osk.isOpen() && osk.handleKey(e)) return;
            // typing, Backspace and the arrows are the field's own business; only the keys that mean something
            // to this dialog are handled here
            if (enter) confirmName();
            else if (back) {
                state.step = 'disks';
                state.message = '';
                render();
            }
        } else if (state.step === 'done' && (enter || back)) {
            close();
        }
    }

    // The installer runs in the background; poll its progress while it runs.
    function frame() {
        if (state.step === 'running' && ++state.frames % CHECK_EVERY_FRAMES === 0) refresh();
        requestAnimationFrame(frame);
    }
    frame();

    return { open, close, isOpen, handleKey };
})();
