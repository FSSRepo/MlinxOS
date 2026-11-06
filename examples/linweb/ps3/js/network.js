// Network settings for the PS3-style menu: Wi-Fi networks, password entry, connection status
// in the corner of the screen, and an on/off switch for the web server. It talks to the engine
// through linweb.wifi and linweb.webserver; app.js creates it with createNetworkUI() and
// forwards key events while the panel is open.
function createNetworkUI(options) {
    const PANEL_ROWS = 8;
    const POLL_MS = 1000;
    const BADGE_MS = 2000;
    const SCAN_SECONDS = 8;
    const PORTS = [8080, 80, 8000, 3000, 8888];

    const ui = options;           // { panel, title, subtitle, list, hint, badge, setStatus }
    const state = {
        mode: null,               // null | menu | networks | password | connecting | message | webserver
        index: 0,
        networks: [],
        scanning: false,
        scanEnds: 0,
        target: null,             // network being connected to
        showPassword: false,
        fieldShown: false,        // the password <input> is on screen
        title: '',
        text: '',
        after: null,
        portIndex: 0,
        direct: false,            // opened straight on the web server page: Back closes the panel
        polling: false,
        badgeTimer: false
    };

    // ---- helpers ------------------------------------------------------------
    function wifiStatus() {
        try {
            return linweb.wifi.status();
        } catch (err) {
            console.error('wifi status failed', err);
            return { available: false, state: 'OFF', ssid: '', ip: '', error: 'Wi-Fi is not available' };
        }
    }

    function serverStatus() {
        try {
            return linweb.webserver.status();
        } catch (err) {
            console.error('web server status failed', err);
            return { running: false, port: 0, addresses: [] };
        }
    }

    function clip(text, length) {
        return text.length > length ? text.substring(0, length - 1) + '~' : text;
    }

    function serverUrls(status) {
        const urls = (status.addresses || []).map(a => 'http://' + a.ip + ':' + status.port);
        return urls.length ? urls : ['http://127.0.0.1:' + status.port + '  (no network address yet)'];
    }

    function wifiLine(status) {
        if (!status.available) return 'Wi-Fi: no adapter found';
        if (status.state === 'CONNECTED') return 'Wi-Fi: ' + clip(status.ssid, 24) + '  ' + status.ip;
        if (status.state === 'CONNECTING' || status.state === 'OBTAINING_IP') return 'Wi-Fi: connecting to ' + clip(status.ssid, 20) + '...';
        if (status.state === 'FAILED') return 'Wi-Fi: connection failed';
        return 'Wi-Fi: not connected';
    }

    // ---- status bar: network name, state and address -------------------------------
    // Each item has a tone (ok, info, warn) that picks the colour of its dot.
    function badgeItems() {
        const wifi = wifiStatus();
        const items = [];
        if (wifi.available) {
            if (wifi.state === 'CONNECTED') items.push({ tone: 'ok', title: clip(wifi.ssid, 24), detail: 'Connected  ' + wifi.ip });
            else if (wifi.state === 'CONNECTING' || wifi.state === 'OBTAINING_IP') items.push({ tone: 'info', title: 'Connecting', detail: clip(wifi.ssid, 20) + '...' });
            else if (wifi.state === 'FAILED') items.push({ tone: 'warn', title: 'Wi-Fi', detail: 'connection failed' });
        }
        const server = serverStatus();
        if (server.running) {
            const addr = (server.addresses && server.addresses.length) ? server.addresses[0].ip : '127.0.0.1';
            items.push({ tone: 'ok', title: 'Web server', detail: 'http://' + addr + ':' + server.port });
        }
        // What the music player is playing stays in the bar while it plays or is paused, and leaves it when stopped
        const audio = (typeof linweb !== 'undefined' && linweb.audio && linweb.audio.status) ? linweb.audio.status() : null;
        if (audio && (audio.playing || audio.paused) && audio.path) {
            const track = audio.path.substring(audio.path.lastIndexOf('/') + 1);
            const clock = (seconds) => Math.floor(seconds / 60) + ':' + ('0' + Math.floor(seconds % 60)).slice(-2);
            const time = audio.duration > 0 ? clock(audio.position) + ' / ' + clock(audio.duration) : clock(audio.position);
            items.push({ tone: 'info', title: clip(track, 28), detail: (audio.paused ? 'Paused  ' : 'Playing  ') + time });
        }
        return items;
    }

    function refreshBadge() {
        if (!ui.badge) return;
        const items = badgeItems();
        ui.badge.innerHTML = '';
        items.forEach(item => {
            const line = document.createElement('div');
            line.className = 'nb-item';
            const dot = document.createElement('span');
            dot.className = 'nb-dot ' + item.tone;
            const title = document.createElement('span');
            title.className = 'nb-title';
            title.innerText = item.title;
            const detail = document.createElement('span');
            detail.className = 'nb-detail';
            detail.innerText = item.detail;
            line.appendChild(dot);
            line.appendChild(title);
            line.appendChild(detail);
            ui.badge.appendChild(line);
        });
        if (items.length) ui.badge.classList.add('visible');
        else ui.badge.classList.remove('visible');
    }

    function badgeTick() {
        state.badgeTimer = false;
        refreshBadge();
        scheduleBadge();
    }

    function scheduleBadge() {
        if (state.badgeTimer || typeof setTimeout !== 'function') return;
        state.badgeTimer = true;
        // the track clock in the bar counts seconds: while music plays it is refreshed more often than the network state
        const audio = (typeof linweb !== 'undefined' && linweb.audio && linweb.audio.status) ? linweb.audio.status() : null;
        setTimeout(badgeTick, audio && audio.playing ? 500 : BADGE_MS);
    }

    // ---- rendering ----------------------------------------------------------------------
    function showRows(rows, index) {
        const start = Math.min(Math.max(0, index - 3), Math.max(0, rows.length - PANEL_ROWS));
        const end = Math.min(rows.length, start + PANEL_ROWS);
        const cells = xmbListRows(ui.list, end - start);
        for (let i = start; i < end; i++) {
            const entry = rows[i];
            const label = (entry && typeof entry === 'object') ? entry.label : entry;
            const icon = (entry && typeof entry === 'object') ? entry.icon : undefined;
            const row = cells[i - start];
            row.className = 'sp-item' + (i === index ? ' active' : '');
            if (typeof xmbFillItem === 'function') xmbFillItem(row, label, icon);
            else row.innerText = label;
        }
    }

    function draw(title, subtitle, rows, index, hint) {
        // Choosing a Wi-Fi network takes the whole screen (password and connecting), growing from the column
        const full = state.mode === 'password' || state.mode === 'connecting';
        if (full && !ui.panel.classList.contains('fullscreen')) {
            ui.panel.classList.remove('fullscreen');
            xmbSetPanel(ui.panel, true);
            void ui.panel.offsetWidth;
        }
        ui.panel.classList.toggle('fullscreen', full);
        xmbSetPanel(ui.panel, true);
        ui.title.innerText = title;
        ui.subtitle.innerText = subtitle;
        showRows(rows, index);
        ui.hint.innerText = hint;
        xmbRevealSelection(ui.panel);
    }

    function menuRows() {
        const wifi = wifiStatus();
        const server = serverStatus();
        const rows = [{ label: wifiLine(wifi), run: openNetworks }];
        if (wifi.available && (wifi.state === 'CONNECTED' || wifi.state === 'CONNECTING' || wifi.state === 'OBTAINING_IP')) {
            rows.push({ label: 'Disconnect Wi-Fi', run: disconnect });
        }
        rows.push({ label: 'Web server: ' + (server.running ? 'ON  (port ' + server.port + ')' : 'OFF'), run: openWebServer });
        rows.push({ label: 'Close', run: close });
        return rows;
    }

    function networkRows() {
        const rows = state.networks.map(n => ({
            label: clip(n.ssid, 26) + '   ' + n.signal + '%   ' + n.security,
            icon: n.connected ? 'check' : 'wifi',
            network: n
        }));
        rows.push({ label: state.scanning ? 'Scanning...' : 'Scan again', scan: true, icon: 'refresh' });
        rows.push({ label: 'Back', back: true, icon: 'left' });
        return rows;
    }

    function webRows() {
        const server = serverStatus();
        return [
            { label: server.running ? 'Turn OFF' : 'Turn ON', run: toggleServer },
            { label: 'Port: ' + PORTS[state.portIndex] + (server.running ? '  (stop the server to change)' : '   < >'), port: true },
            { label: 'Back', back: true }
        ];
    }

    // The password is typed into a real <input type="password"> (max length in the HTML): the engine edits it, with
    // caret, Backspace and the arrow keys. It is cleared whenever it goes away, so a password is not kept around.
    // The on-screen keyboard (js/osk.js) is shown with the field, for use without a physical keyboard.
    const osk = (ui.keyboard && typeof createOnScreenKeyboard === 'function')
        ? createOnScreenKeyboard({ container: ui.keyboard, input: ui.input, onGo: () => submitPassword() })
        : null;

    function showPasswordField() {
        ui.input.value = '';
        ui.input.setAttribute('type', state.showPassword ? 'text' : 'password');
        ui.input.classList.add('visible');
        ui.input.focus();
        state.fieldShown = true;
        if (osk) osk.show();
    }

    function hidePasswordField() {
        ui.input.blur();
        ui.input.classList.remove('visible');
        ui.input.value = '';
        state.fieldShown = false;
        if (osk) osk.hide();
    }

    function submitPassword() {
        const password = ui.input.value;
        if (!password.length) { state.text = 'Type the password first'; render(); return; }
        beginConnect(password);
    }

    // Same rule as the storage panel: deeper screens slide in from the right, going back slides in from the left.
    // Results are toasts (xmbToast), not screens.
    const LEVEL = { menu: 1, networks: 2, webserver: 2, password: 3, connecting: 3 };
    function slideBetweenLevels() {
        const level = LEVEL[state.mode] || 0;
        const last = state.level || 0;
        if (level && last && level !== last) {
            if (level > last) xmbSlideForward(ui.list);
            else xmbSlideBack(ui.list);
        }
        state.level = level;
    }

    function render() {
        if (state.mode !== 'password' && state.fieldShown) hidePasswordField();
        slideBetweenLevels();
        if (state.mode === 'menu') {
            const rows = menuRows();
            draw('Network', 'Wi-Fi and web server', rows.map(r => r.label), state.index,
                'Up/Down: select   Enter: open   Esc: close');
        } else if (state.mode === 'networks') {
            const rows = networkRows();
            const sub = state.networks.length ? state.networks.length + ' network(s)' : (state.scanning ? 'Looking for networks...' : 'No networks found');
            draw('Wi-Fi networks', sub, rows, state.index,
                'Up/Down: select   Enter: connect   Esc: back');
        } else if (state.mode === 'password') {
            draw('Password for ' + clip(state.target.ssid, 26), 'Type the password and press Enter' + (state.text ? '\n' + state.text : ''), [], -1,
                'Enter: connect   Tab: show/hide   Esc: cancel');
            if (!state.fieldShown) showPasswordField();
        } else if (state.mode === 'connecting') {
            const wifi = wifiStatus();
            const steps = { CONNECTING: 'Joining the network...', OBTAINING_IP: 'Asking for an IP address...' };
            draw('Connecting to ' + clip(state.target.ssid, 24), steps[wifi.state] || 'Starting...', [], -1, 'Esc: cancel');
        } else if (state.mode === 'webserver') {
            const server = serverStatus();
            const lines = server.running ? ['Running. Open from another device:'].concat(serverUrls(server)) : ['Stopped.'];
            if (state.text) lines.push(state.text);
            const rows = webRows();
            draw('Web server', lines.join('\n'), rows.map(r => r.label), state.index,
                'Up/Down: select   Enter: run   Left/Right: port   Esc: back');
        }
    }

    // ---- actions ------------------------------------------------------------------------
    function open(which) {
        const wasVisible = ui.panel.classList.contains('visible');
        state.index = 0;
        state.text = '';
        state.direct = which === 'webserver';
        if (which === 'webserver') openWebServer();
        else openMenu();
        refreshBadge();
        if (!wasVisible) xmbZoomIn(ui.panel);
    }

    function openMenu() {
        state.mode = 'menu';
        state.index = 0;
        render();
    }

    function close() {
        state.level = 0;
        if (state.fieldShown) hidePasswordField();
        state.mode = null;
        xmbSetPanel(ui.panel, false);
        refreshBadge();
    }

    // A result is a toast; the screen it came from (or the next one) is shown at once
    function message(title, text, after) {
        xmbToast(title, text);
        (after || openMenu)();
    }

    function openNetworks() {
        state.mode = 'networks';
        state.index = 0;
        state.networks = [];
        startScan();
        render();
    }

    function startScan() {
        const result = linweb.wifi.scan();
        if (!result.ok) {
            message('Wi-Fi', result.error, openMenu);
            return;
        }
        state.scanning = true;
        state.scanEnds = Date.now() + SCAN_SECONDS * 1000;
        schedulePoll();
    }

    function loadNetworks() {
        const current = state.networks[state.index];
        state.networks = linweb.wifi.networks();
        if (current) {
            const at = state.networks.findIndex(n => n.ssid === current.ssid);
            if (at >= 0) state.index = at;
        }
    }

    function selectNetwork(network) {
        state.target = network;
        state.text = '';
        if (network.connected) {
            message('Already connected', network.ssid + '\n' + (wifiStatus().ip || ''), openNetworksKeep);
        } else if (network.secure) {
            state.mode = 'password';
            state.showPassword = false;
            render();
        } else {
            beginConnect('');
        }
    }

    function openNetworksKeep() {
        state.mode = 'networks';
        render();
    }

    function beginConnect(password) {
        const result = linweb.wifi.connect(state.target.ssid, password);
        if (!result.ok) {
            if (state.target.secure) {
                state.mode = 'password';
                state.text = result.error;
                render();
            } else {
                message('Cannot connect', result.error, openNetworksKeep);
            }
            return;
        }
        state.mode = 'connecting';
        ui.setStatus('Connecting to ' + state.target.ssid);
        render();
        schedulePoll();
    }

    function disconnect() {
        const result = linweb.wifi.disconnect();
        ui.setStatus(result.ok ? 'Wi-Fi disconnected' : result.error);
        refreshBadge();
        render();
    }

    function leaveWebServer() {
        if (state.direct) close();
        else openMenu();
    }

    function openWebServer() {
        state.mode = 'webserver';
        state.index = 0;
        state.text = '';
        const server = serverStatus();
        if (server.running) {
            const at = PORTS.indexOf(server.port);
            if (at >= 0) state.portIndex = at;
        }
        render();
    }

    function toggleServer() {
        const server = serverStatus();
        const result = server.running ? linweb.webserver.stop() : linweb.webserver.start(PORTS[state.portIndex]);
        state.text = result.ok ? '' : result.error;
        ui.setStatus(result.ok ? (server.running ? 'Web server stopped' : 'Web server started') : 'Web server error');
        refreshBadge();
        render();
    }

    // Follows a scan or a connection attempt.
    function schedulePoll() {
        if (state.polling || typeof setTimeout !== 'function') return;
        state.polling = true;
        setTimeout(poll, POLL_MS);
    }

    function poll() {
        state.polling = false;
        if (state.mode === 'networks') {
            loadNetworks();
            if (Date.now() >= state.scanEnds) state.scanning = false;
            render();
            if (state.scanning) schedulePoll();
        } else if (state.mode === 'connecting') {
            const wifi = wifiStatus();
            if (wifi.state === 'CONNECTED') {
                refreshBadge();
                ui.setStatus('Connected to ' + wifi.ssid);
                message('Connected', wifi.ssid + '\nIP address: ' + wifi.ip, openMenu);
            } else if (wifi.state === 'FAILED') {
                refreshBadge();
                const secure = state.target.secure;
                message('Connection failed', wifi.error || 'Could not connect', secure ? retryPassword : openNetworksKeep);
            } else {
                render();
                schedulePoll();
            }
        }
        refreshBadge();
    }

    function retryPassword() {
        state.mode = 'password';
        state.text = '';
        render();
    }

    function cancelConnect() {
        linweb.wifi.disconnect();
        ui.setStatus('Wi-Fi connection cancelled');
        openNetworksKeep();
    }

    // ---- input ----------------------------------------------------------------------
    function move(delta, length) {
        state.index = Math.max(0, Math.min(length - 1, state.index + delta));
        render();
    }

    function handleKey(e) {
        const code = e.keyCode;
        const up = code === 38;
        const down = code === 40;
        const left = code === 37;
        const right = code === 39;
        const enter = e.key === 'Enter' || code === 13 || code === 10;
        const back = e.key === 'Escape' || code === 27 || code === 461;

        if (state.mode === 'password') {
            // While the on-screen keyboard is open, the arrows and Enter move and press its keys
            if (osk && osk.isOpen() && osk.handleKey(e)) return;
            // typing, Backspace and the arrows are the input's own business (its default action); only the keys
            // that mean something to this dialog are handled here
            if (enter) {
                submitPassword();
            } else if (back) {
                openNetworksKeep();
            } else if (e.key === 'Tab') {
                if (e.preventDefault) e.preventDefault();          // Tab shows or hides the password, it does not move the focus
                state.showPassword = !state.showPassword;
                ui.input.setAttribute('type', state.showPassword ? 'text' : 'password');
            }
        } else if (state.mode === 'menu') {
            const rows = menuRows();
            if (up) move(-1, rows.length);
            else if (down) move(1, rows.length);
            else if (enter) rows[state.index].run();
            else if (back) close();
        } else if (state.mode === 'networks') {
            const rows = networkRows();
            if (up) move(-1, rows.length);
            else if (down) move(1, rows.length);
            else if (enter) {
                const row = rows[state.index];
                if (row.network) selectNetwork(row.network);
                else if (row.scan) { if (!state.scanning) { startScan(); render(); } }
                else openMenu();
            } else if (back) openMenu();
        } else if (state.mode === 'connecting') {
            if (back) cancelConnect();
        } else if (state.mode === 'webserver') {
            const rows = webRows();
            if (up) move(-1, rows.length);
            else if (down) move(1, rows.length);
            else if ((left || right) && rows[state.index].port && !serverStatus().running) {
                state.portIndex = (state.portIndex + (right ? 1 : PORTS.length - 1)) % PORTS.length;
                render();
            } else if (enter) {
                const row = rows[state.index];
                if (row.run) row.run();
                else if (row.back) leaveWebServer();
            } else if (back) leaveWebServer();
        }
    }

    return {
        open: open,
        handleKey: handleKey,
        isOpen: () => state.mode !== null,
        startBadge: () => { refreshBadge(); scheduleBadge(); }
    };
}
