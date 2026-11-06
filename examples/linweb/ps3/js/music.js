// Music player for the PS3-style menu: browses the mounted storage under /mnt, plays
// MP3 files through linweb.audio and keeps playing while the panel is closed.
// app.js creates it with createMusicUI() and forwards key events while it is open.
function createMusicUI(options) {
    const PANEL_ROWS = 8;
    const TICK_MS = 500;
    const MUSIC_ROOT = '/mnt';
    const VOLUME_STEP = 5;
    const DEFAULT_VOLUME = 70;   // used when the mixer starts muted or at 0% (QEMU's HDA card does)
    const FADE_MS = 300;         // crossfade with the storage browser (fade-in / fade-out in style.css)

    const ui = options;           // { panel, title, subtitle, list, hint, progress (a <progress>), setStatus }
    const state = {
        mode: null,               // null | browser | player
        path: MUSIC_ROOT,
        entries: [],
        error: '',
        index: 0,
        track: null,              // { dir, files, pos } of the loaded track
        card: -1,
        volume: null,             // { percent, muted } or null when the mixer is unavailable
        message: '',
        autoNext: false,          // move on to the next file when a track ends by itself
        ticking: false
    };

    function fmtTime(seconds) {
        const total = Math.max(0, Math.floor(seconds || 0));
        const s = total % 60;
        return Math.floor(total / 60) + ':' + (s < 10 ? '0' : '') + s;
    }

    function isMp3(name) {
        return /\.mp3$/i.test(name);
    }

    function joinPath(base, name) {
        return base.replace(/\/+$/, '') + '/' + name;
    }

    function parentPath(path) {
        const cut = path.lastIndexOf('/');
        return cut <= 0 ? '/' : path.substring(0, cut);
    }

    function baseName(path) {
        return path.substring(path.lastIndexOf('/') + 1);
    }

    function pickCard() {
        try {
            // Prefer a card with speakers or a jack: on laptops the HDMI audio is often card 0.
            const cards = linweb.audio.cards();
            for (const card of cards) {
                if (card.playback && !card.hdmi) return card.index;
            }
            for (const card of cards) {
                if (card.playback) return card.index;
            }
        } catch (err) {
            console.error('audio cards failed', err);
        }
        return -1;
    }

    function refreshVolume() {
        if (state.card < 0) { state.volume = null; return; }
        const v = linweb.audio.getVolume(state.card);
        state.volume = v.ok ? v : null;
    }

    // ---- rendering --------------------------------------------------------
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
        xmbSetPanel(ui.panel, true);
        ui.title.innerText = title;
        ui.subtitle.innerText = subtitle;
        showRows(rows, index);
        ui.hint.innerText = hint;
        xmbRevealSelection(ui.panel);
    }

    function browserRows() {
        const rows = [];
        if (state.path !== MUSIC_ROOT) rows.push({ label: 'Parent folder', up: true, icon: 'up' });
        state.entries.forEach(entry => {
            rows.push({
                label: entry.name,
                name: entry.name,
                dir: entry.dir,
                icon: entry.dir ? 'folder' : 'music'
            });
        });
        return rows;
    }

    function playerRows(status) {
        const rows = [status.playing ? (status.paused ? 'Resume' : 'Pause') : 'Play again', 'Stop', 'Previous', 'Next'];
        if (state.volume) {
            rows.push('Volume ' + state.volume.percent + '%' + (state.volume.muted ? ' (muted)' : '') + '   < >');
            rows.push(state.volume.muted ? 'Unmute' : 'Mute');
        }
        return rows;
    }

    function render() {
        // the player takes the whole screen; the browser keeps the column of the XMB
        if (state.mode === 'player' && !ui.panel.classList.contains('fullscreen')) {
            if (state.fromStorage) {
                // opened from the storage browser: it fades in over the full screen instead of growing
                ui.panel.classList.add('fullscreen');
                xmbSetPanel(ui.panel, true);
                ui.panel.classList.remove('fade-in');
                void ui.panel.offsetWidth;
                ui.panel.classList.add('fade-in');
            } else {
                // show the panel where it normally is first, so the growth to full screen has a start point
                ui.panel.classList.remove('fullscreen');
                xmbSetPanel(ui.panel, true);
                void ui.panel.offsetWidth;
            }
        }
        if (state.mode === 'player') ui.panel.classList.add('fullscreen');
        else ui.panel.classList.remove('fullscreen');
        if (state.mode === 'browser') {
            const rows = browserRows();
            const sub = state.message || state.error ||
                (state.entries.length ? state.entries.length + ' item(s)' : 'No folders or MP3 files here. Mount a disk from Storage first.');
            draw('Music: ' + state.path, sub, rows, state.index,
                'Up/Down: select   Enter: open/play   Esc: ' + (state.path === MUSIC_ROOT ? 'close' : 'back'));
        } else if (state.mode === 'player') {
            const status = linweb.audio.status();
            const name = state.track ? state.track.files[state.track.pos] : '';
            let sub;
            if (state.message) sub = state.message;
            else if (status.playing) {
                sub = name + '\n' + (status.paused ? 'Paused  ' : 'Playing  ') + fmtTime(status.position) +
                    (status.duration ? ' / ' + fmtTime(status.duration) : '') +
                    '   ' + status.sampleRate + ' Hz ' + (status.channels === 1 ? 'mono' : 'stereo');
            } else sub = name + '\nStopped';
            draw('Music Player', sub, playerRows(status), state.index,
                'Up/Down: select   Enter: run   Left/Right: volume   Esc: back (keeps playing)');
            // position in the track: a <progress> bar (the duration is an estimate for VBR files, so it is never full-scale exact)
            if (status.duration > 0 && (status.playing || status.position > 0)) {
                ui.progress.max = status.duration;
                ui.progress.value = Math.min(status.position, status.duration);
            } else {
                ui.progress.removeAttribute('value');
            }
            // a stopped track has no position to show
            if (status.playing || status.paused) ui.progress.classList.add('visible');
            else ui.progress.classList.remove('visible');
        }
        if (state.mode !== 'player') ui.progress.classList.remove('visible');
    }

    // A freshly booted mixer is often muted at 0%, which would play in silence.
    function ensureAudible() {
        if (!state.volume || !(state.volume.muted || state.volume.percent === 0)) return;
        const result = linweb.audio.setVolume(state.card, DEFAULT_VOLUME);
        if (result.ok) state.volume = result;
    }

    // ---- playback ---------------------------------------------------------
    function loadDir(path) {
        const result = linweb.storage.readDir(path);
        state.path = path;
        state.error = result.ok ? '' : result.error;
        state.entries = result.ok ? result.entries.filter(e => e.dir || isMp3(e.name)) : [];
        state.entries.sort((a, b) => (a.dir === b.dir) ? (a.name < b.name ? -1 : 1) : (a.dir ? -1 : 1));
        state.index = 0;
    }

    function playTrack(dir, files, pos) {
        const result = linweb.audio.play(joinPath(dir, files[pos]), state.card);
        state.track = { dir: dir, files: files, pos: pos };
        state.message = result.ok ? '' : 'Cannot play: ' + result.error;
        ui.setStatus(result.ok ? 'Playing ' + files[pos] : 'Playback failed');
        return result.ok;
    }

    function step(delta) {
        if (!state.track) return;
        const next = state.track.pos + delta;
        if (next < 0 || next >= state.track.files.length) return;
        playTrack(state.track.dir, state.track.files, next);
    }

    function openFile(name) {
        const files = state.entries.filter(e => !e.dir).map(e => e.name);
        state.mode = 'player';
        state.index = 0;
        state.autoNext = true;
        refreshVolume();
        ensureAudible();
        playTrack(state.path, files, files.indexOf(name));
        render();
        scheduleTick();
    }

    // Refresh the clock and start the next file when a track ends by itself.
    function scheduleTick() {
        if (state.ticking || typeof setTimeout !== 'function') return;
        state.ticking = true;
        setTimeout(tick, TICK_MS);
    }

    function tick() {
        state.ticking = false;
        if (!state.track) return;
        const status = linweb.audio.status();
        if (status.error) { state.message = 'Playback error: ' + status.error; state.autoNext = false; }
        if (!status.playing && state.autoNext && !state.message && state.track.pos + 1 < state.track.files.length) {
            step(1);
        } else if (!status.playing) {
            state.autoNext = false;
        }
        if (state.mode === 'player') render();
        if (status.playing || state.autoNext) scheduleTick();
    }

    function runPlayerRow(label) {
        state.message = '';
        if (label === 'Pause') linweb.audio.pause();
        else if (label === 'Resume') { linweb.audio.resume(); state.autoNext = true; }
        else if (label === 'Play again' && state.track) { state.autoNext = true; playTrack(state.track.dir, state.track.files, state.track.pos); }
        else if (label === 'Stop') {
            state.autoNext = false;
            linweb.audio.stop();
            ui.setStatus('Stopped');
        } else if (label === 'Previous') { state.autoNext = true; step(-1); }
        else if (label === 'Next') { state.autoNext = true; step(1); }
        else if (label === 'Mute' || label === 'Unmute') {
            const result = linweb.audio.setMute(state.card, label === 'Mute');
            if (!result.ok) state.message = result.error;
            refreshVolume();
        }
        render();
        scheduleTick();
    }

    function changeVolume(delta) {
        if (!state.volume) return;
        const result = linweb.audio.setVolume(state.card, Math.max(0, Math.min(100, state.volume.percent + delta)));
        if (result.ok) state.volume = result;
        else state.message = result.error;
        render();
    }

    // ---- input ------------------------------------------------------------
    function open() {
        state.fromStorage = false;
        state.card = pickCard();
        state.message = state.card < 0 ? 'No sound card found (QEMU needs -device intel-hda -device hda-duplex)' : '';
        if (state.track && linweb.audio.status().playing) {
            state.mode = 'player';
            state.index = 0;
            refreshVolume();
        } else {
            state.mode = 'browser';
            loadDir(MUSIC_ROOT);
        }
        render();
        scheduleTick();
    }

    function close() {
        state.mode = null;
        xmbSetPanel(ui.panel, false);
        ui.progress.classList.remove('visible');
    }

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
        const enter = code === 13 || code === 10 || e.key === 'Enter';
        const back = code === 27 || code === 461 || code === 8;

        if (state.mode === 'browser') {
            const rows = browserRows();
            if (up) move(-1, rows.length);
            else if (down) move(1, rows.length);
            else if (enter && rows.length) {
                const row = rows[state.index];
                state.message = '';
                if (row.up) { loadDir(parentPath(state.path)); render(); xmbSlideBack(ui.list); }
                else if (row.dir) { loadDir(joinPath(state.path, row.name)); render(); xmbSlideForward(ui.list); }
                else if (state.card < 0) { state.message = 'No sound card found'; render(); }
                else openFile(row.name);
            } else if (back) {
                if (state.path !== MUSIC_ROOT) { loadDir(parentPath(state.path)); render(); xmbSlideBack(ui.list); }
                else close();
            }
        } else if (state.mode === 'player') {
            const rows = playerRows(linweb.audio.status());
            if (up) move(-1, rows.length);
            else if (down) move(1, rows.length);
            else if (left) changeVolume(-VOLUME_STEP);
            else if (right) changeVolume(VOLUME_STEP);
            else if (enter) runPlayerRow(rows[state.index]);
            else if (back && state.onExit) {
                // The music keeps playing in the background; the caller (the storage browser) takes the screen
                const done = state.onExit;
                state.onExit = null;
                state.mode = null;
                ui.progress.classList.remove('visible');
                // fade out, then hide it and let the storage browser fade in
                ui.panel.classList.remove('fade-in');
                ui.panel.classList.add('fade-out');
                setTimeout(() => {
                    ui.panel.classList.remove('fade-out', 'fullscreen');
                    xmbSetPanel(ui.panel, false);
                    done();
                }, FADE_MS);
            } else if (back) {
                state.mode = 'browser';
                state.message = '';
                loadDir(state.track ? state.track.dir : MUSIC_ROOT);
                render();
            }
        }
    }

    // A file opened in the storage browser: the player takes the folder of the file
    // onExit (optional) is called on Esc instead of the player's own browser: the caller takes the screen back
    function playPath(path, onExit) {
        state.card = pickCard();
        state.onExit = onExit || null;
        state.fromStorage = !!onExit;
        loadDir(parentPath(path));
        openFile(baseName(path));
    }

    return {
        open: open,
        play: playPath,
        // another player takes the sound: the track stops and the next one does not start by itself
        stop: () => {
            state.autoNext = false;
            if (state.track) linweb.audio.stop();
        },
        handleKey: handleKey,
        isOpen: () => state.mode !== null
    };
}
