// Node Lite panel for the PS3-style menu: runs node-lite scripts (fetch, DNS, a small web server) through
// linweb.node and shows their output while they run.
// app.js creates it with createNodeLiteUI() and forwards key events while it is open.
function createNodeLiteUI(options) {
    const EXAMPLES = '/root/node-lite/example';
    const OUTPUT_ROWS = 12;
    const LINE_WIDTH = 74;
    const TICK_MS = 200;
    const SERVER_PORT = 8090;
    const SERVER_TIMEOUT_MS = 30 * 60 * 1000;

    const ui = options;              // { panel, title, subtitle, list, hint, setStatus }
    const state = {
        mode: null,                  // null | menu | url | output
        index: 0,
        url: 'https://',             // what the address field holds when it is not on screen
        fieldShown: false,           // the address <input> is on screen
        message: '',
        job: null,                   // { id, title }
        result: null,                // last poll() of the job
        scroll: 0,                   // lines above the end of the output
        ticking: false
    };

    function actions() {
        return [
            { label: 'Fetch https://www.google.com', script: EXAMPLES + '/fetch.js', args: ['https://www.google.com'] },
            { label: 'Fetch http://google.com (follows the redirect)', script: EXAMPLES + '/fetch.js', args: ['http://google.com'] },
            { label: 'Fetch a URL...', url: true },
            { label: 'Start the example web server on port ' + SERVER_PORT, script: EXAMPLES + '/server.js',
              args: [String(SERVER_PORT)], timeout: SERVER_TIMEOUT_MS }
        ];
    }

    function addressLine() {
        try {
            const list = linweb.network.addresses();
            return list.length ? 'This machine: ' + list.map(a => a.ip + ' (' + a.iface + ')').join(', ')
                               : 'No network address yet: connect Wi-Fi or a cable first';
        } catch (err) {
            return '';
        }
    }

    function show(rows, index) {
        const cells = xmbListRows(ui.list, rows.length);
        for (let i = 0; i < rows.length; i++) {
            const row = cells[i];
            row.className = 'sp-item' + (index === i ? ' active' : '');
            if (typeof xmbFillItem === 'function') xmbFillItem(row, rows[i]);
            else row.innerText = rows[i];
        }
    }

    function draw(title, subtitle, rows, index, hint) {
        xmbSetPanel(ui.panel, true);
        ui.title.innerText = title;
        ui.subtitle.innerText = subtitle;
        show(rows, index);
        ui.hint.innerText = hint;
        xmbRevealSelection(ui.panel);
    }

    function wrap(text) {
        const lines = [];
        text.replace(/\r/g, '').split('\n').forEach(line => {
            if (line.length === 0) { lines.push(''); return; }
            for (let i = 0; i < line.length; i += LINE_WIDTH) lines.push(line.substring(i, i + LINE_WIDTH));
        });
        while (lines.length && lines[lines.length - 1] === '') lines.pop();
        return lines;
    }

    function statusText() {
        const r = state.result;
        if (!r || !r.found) return 'Stopped';
        if (r.running) return 'Running...';
        if (r.timedOut) return 'Timed out';
        return 'Finished, exit code ' + r.exitCode;
    }

    // The address is typed into a real <input>: the engine edits it (caret, Backspace, arrows).
    function showUrlField() {
        ui.input.value = state.url;
        ui.input.classList.add('visible');
        ui.input.focus();
        state.fieldShown = true;
    }

    function hideUrlField() {
        state.url = ui.input.value;
        ui.input.blur();
        ui.input.classList.remove('visible');
        state.fieldShown = false;
    }

    function render() {
        if (state.mode !== 'url' && state.fieldShown) hideUrlField();
        if (state.mode === 'menu') {
            const available = linweb.node.available();
            const sub = !available ? 'node-lite is not installed in this system'
                : (state.message || addressLine());
            draw('Node Lite', sub, actions().map(a => a.label), state.index,
                'Up/Down: select   Enter: run   Esc: close');
        } else if (state.mode === 'url') {
            draw('Fetch a URL', state.message || 'Type the address and press Enter', [], -1, 'Enter: fetch   Esc: back');
            if (!state.fieldShown) showUrlField();
        } else if (state.mode === 'output') {
            const lines = wrap(state.result ? state.result.output : '');
            state.scroll = Math.min(state.scroll, Math.max(0, lines.length - OUTPUT_ROWS));
            const end = Math.max(OUTPUT_ROWS, lines.length - state.scroll);
            const visible = lines.slice(Math.max(0, end - OUTPUT_ROWS), end);
            const more = lines.length > OUTPUT_ROWS ? '   Up/Down: scroll' : '';
            draw(state.job.title, statusText(), visible.length ? visible : ['(no output yet)'], -1,
                'Esc: ' + (state.result && state.result.running ? 'stop and go back' : 'back') + more);
        }
    }

    function tick() {
        state.ticking = false;
        if (state.mode !== 'output' || !state.job) return;
        state.result = linweb.node.poll(state.job.id);
        render();
        if (state.result.found && state.result.running) schedule();
        else ui.setStatus(state.job.title + ': ' + statusText());
    }

    function schedule() {
        if (state.ticking) return;
        state.ticking = true;
        setTimeout(tick, TICK_MS);
    }

    function startJob(title, started) {
        if (!started.ok) {
            state.message = started.error;
            state.mode = 'menu';
            render();
            return;
        }
        state.job = { id: started.id, title: title };
        state.result = linweb.node.poll(started.id);
        state.scroll = 0;
        state.mode = 'output';
        render();
        schedule();
    }

    function leaveOutput() {
        if (state.job) linweb.node.stop(state.job.id);
        state.job = null;
        state.result = null;
        state.mode = 'menu';
        state.message = '';
        render();
    }

    function runAction(action) {
        state.message = '';
        if (action.url) {
            state.mode = 'url';
            render();
            return;
        }
        startJob(action.label, linweb.node.start(action.script, action.args, action.timeout));
    }

    function runUrl() {
        const url = ui.input.value.trim();
        if (!/^https?:\/\/[^\s]+$/i.test(url)) {
            state.message = 'The address must start with http:// or https://';
            render();
            return;
        }
        startJob('Fetch ' + url, linweb.node.start(EXAMPLES + '/fetch.js', [url]));
    }

    function open() {
        const wasVisible = ui.panel.classList.contains('visible');
        state.mode = 'menu';
        state.index = 0;
        state.message = '';
        render();
        if (!wasVisible) xmbZoomIn(ui.panel);
    }

    function close() {
        if (state.fieldShown) hideUrlField();
        if (state.job) linweb.node.stop(state.job.id);
        state.job = null;
        state.mode = null;
        xmbSetPanel(ui.panel, false);
    }

    function handleKey(e) {
        const code = e.keyCode;
        const up = code === 38;
        const down = code === 40;
        const enter = code === 13 || code === 10 || e.key === 'Enter';
        const back = code === 27 || code === 461;

        if (state.mode === 'menu') {
            const rows = actions();
            if (up) { state.index = Math.max(0, state.index - 1); render(); }
            else if (down) { state.index = Math.min(rows.length - 1, state.index + 1); render(); }
            else if (enter) {
                if (!linweb.node.available()) { render(); return; }
                runAction(rows[state.index]);
            } else if (back || code === 8) close();
        } else if (state.mode === 'url') {
            if (enter) runUrl();
            else if (back) { state.mode = 'menu'; state.message = ''; render(); }
            // typing and Backspace are the input's default action
        } else if (state.mode === 'output') {
            if (back || code === 8) leaveOutput();
            else if (up) { state.scroll++; render(); }
            else if (down) { state.scroll = Math.max(0, state.scroll - 1); render(); }
        }
    }

    return {
        open: open,
        handleKey: handleKey,
        isOpen: () => state.mode !== null
    };
}
