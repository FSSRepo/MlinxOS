// Music player for the PS3-style menu: browses the mounted storage under /mnt, plays
// MP3 files through an <audio> element and keeps playing while the panel is closed.
// app.js creates it with createMusicUI() and forwards key events while it is open.
function createMusicUI(options) {
    const PANEL_ROWS = 8;
    const TICK_MS = 500;
    const MUSIC_ROOT = '/mnt';
    const VOLUME_STEP = 5;
    const DEFAULT_VOLUME = 70;   // the initial volume of the player, in percent
    const FADE_WAIT_MS = 120;    // the panel stays invisible this long before it fades in
    const FADE_MS = 300;         // crossfade with the storage browser (fade-in / fade-out in style.css)

    const ui = options;           // { panel, title, subtitle, list, hint, progress (a <progress>), setStatus }
    const player = new Audio();   // one element for the whole life of the panel; several sounds are mixed by the engine
    player.volume = DEFAULT_VOLUME / 100;
    const state = {
        mode: null,               // null | browser | player
        path: MUSIC_ROOT,
        entries: [],
        error: '',
        index: 0,
        track: null,              // { dir, files, pos } of the loaded track
        active: false,            // a track was started and neither stopped nor ended nor failed
        volume: { percent: DEFAULT_VOLUME, muted: false },   // { percent, muted } of the player, always available
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

    // The shape the rendering code was written for (the old native player returned it)
    function status() {
        return {
            playing: state.active,
            paused: state.active && player.paused,
            position: player.currentTime || 0,
            duration: isFinite(player.duration) ? player.duration : 0,
            path: state.track ? joinPath(state.track.dir, state.track.files[state.track.pos]) : '',
            error: player.error ? player.error.message : ''
        };
    }

    function refreshVolume() {
        state.volume = { percent: Math.round(player.volume * 100), muted: player.muted };
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

    // The buttons of the player, left to right: the main one (play / pause) is the second. They are also the labels
    // runPlayerRow() acts on.
    const CONTROL_ICONS = {
        'Previous': 'prev', 'Pause': 'pause', 'Resume': 'play', 'Play again': 'play',
        'Next': 'next', 'Stop': 'stop', 'Mute': 'mute', 'Unmute': 'volume'
    };
    const MAIN_CONTROL = 1;

    function playerRows(status) {
        const rows = ['Previous', status.playing ? (status.paused ? 'Resume' : 'Pause') : 'Play again', 'Next', 'Stop'];
        if (state.volume) rows.push(state.volume.muted ? 'Unmute' : 'Mute');
        return rows;
    }

    function trackTitle(file) {
        return String(file || '').replace(/\.mp3$/i, '');
    }

    // The now-playing screen (the panel in .player-mode): title, line of details, time line, buttons and volume.
    // The buttons are rebuilt only when their set changes (Pause becomes Resume); the selection is a class.
    function renderPlayer(status) {
        xmbSetPanel(ui.panel, true);
        const get = (id) => document.getElementById(id);
        const track = state.track;
        const file = track ? track.files[track.pos] : '';
        get('mp-track').innerText = trackTitle(file);
        let state_text;
        if (state.message) state_text = state.message;
        else if (status.playing) {
            state_text = (status.paused ? 'Paused' : 'Playing') +
                (status.sampleRate ? '  ·  ' + status.sampleRate + ' Hz ' + (status.channels === 1 ? 'mono' : 'stereo') : '');
        } else state_text = 'Stopped';
        const where = track ? baseName(track.dir) + (track.files.length > 1 ? '  ·  Track ' + (track.pos + 1) + ' of ' + track.files.length : '') : '';
        get('mp-album').innerText = (where ? where + '  ·  ' : '') + state_text;
        const next = track ? track.files[track.pos + 1] : '';
        get('mp-next').innerText = next ? 'Up next: ' + trackTitle(next) : '';

        get('mp-pos').innerText = fmtTime(status.position);
        get('mp-dur').innerText = status.duration ? fmtTime(status.duration) : '--:--';
        const bar = get('mp-bar');
        if (status.duration > 0 && (status.playing || status.position > 0)) {
            bar.max = status.duration;
            bar.value = Math.min(status.position, status.duration);
        } else {
            bar.max = 1;
            bar.value = 0;
        }

        const labels = playerRows(status);
        state.index = Math.max(0, Math.min(labels.length - 1, state.index));
        const row = get('mp-controls');
        const signature = labels.join('|');
        if (row.getAttribute('data-signature') !== signature) {
            row.innerHTML = '';
            labels.forEach((label) => {
                const button = document.createElement('div');
                const icon = document.createElement('img');
                icon.className = 'mp-ico';
                icon.setAttribute('src', 'icons/' + (CONTROL_ICONS[label] || 'chevron') + '.svg');
                icon.setAttribute('alt', '');
                button.appendChild(icon);
                row.appendChild(button);
            });
            row.setAttribute('data-signature', signature);
        }
        for (let i = 0; i < labels.length; i++) {
            row.children[i].className = 'mp-btn' + (i === MAIN_CONTROL ? ' main' : '') + (i === state.index ? ' active' : '');
        }

        const volume = get('mp-volume');
        if (state.volume) {
            volume.style.display = 'flex';
            get('mp-vol-bar').value = state.volume.muted ? 0 : state.volume.percent;
            get('mp-vol-text').innerText = state.volume.muted ? 'Muted' : state.volume.percent + '%';
            get('mp-vol-icon').setAttribute('src', state.volume.muted ? 'icons/mute.svg' : 'icons/volume.svg');
        } else {
            volume.style.display = 'none';
        }
        ui.hint.innerText = 'Left/Right: select   Enter: run   Up/Down: volume   Esc: back (keeps playing)';
    }

    function render() {
        // the player takes the whole screen; the browser keeps the column of the XMB
        if (state.mode === 'player' && !ui.panel.classList.contains('fullscreen')) {
            if (state.fromStorage) {
                // opened from the storage browser: it fades in over the full screen instead of growing
                // The first frame of the full-screen picture is slow (layout and the shader): it is drawn invisible
                // (fade-wait) and the fade starts a moment later, so the fade is not spent on that frame.
                ui.panel.classList.add('fullscreen');
                xmbSetPanel(ui.panel, true);
                ui.panel.classList.remove('fade-in', 'fade-out');
                ui.panel.classList.add('fade-wait');
                setTimeout(() => {
                    if (!ui.panel.classList.contains('fade-wait')) return;       // it was closed meanwhile
                    ui.panel.classList.remove('fade-wait');
                    ui.panel.classList.add('fade-in');
                }, FADE_WAIT_MS);
            } else {
                // show the panel where it normally is first, so the growth to full screen has a start point
                ui.panel.classList.remove('fullscreen');
                xmbSetPanel(ui.panel, true);
                void ui.panel.offsetWidth;
            }
        }
        if (state.mode === 'player') ui.panel.classList.add('fullscreen');
        else ui.panel.classList.remove('fullscreen');
        ui.panel.classList.toggle('player-mode', state.mode === 'player');
        if (state.mode === 'browser') {
            const rows = browserRows();
            const sub = state.message || state.error ||
                (state.entries.length ? state.entries.length + ' item(s)' : 'No folders or MP3 files here. Mount a disk from Storage first.');
            draw('Music: ' + state.path, sub, rows, state.index,
                'Up/Down: select   Enter: open/play   Esc: ' + (state.path === MUSIC_ROOT ? 'close' : 'back'));
        } else if (state.mode === 'player') {
            renderPlayer(status());
        }
        if (state.mode !== 'player') ui.progress.classList.remove('visible');
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
        state.track = { dir: dir, files: files, pos: pos };
        state.message = '';
        player.src = joinPath(dir, files[pos]);
        state.active = true;
        player.play().catch(err => {
            state.active = false;
            state.message = 'Cannot play: ' + err.message;
            ui.setStatus('Playback failed');
            if (state.mode === 'player') render();
        });
        ui.setStatus('Playing ' + files[pos]);
    }

    // The engine mixes the sounds of every element, so the end of a track comes as an event, not from the clock
    player.addEventListener('ended', () => {
        state.active = false;
        if (state.autoNext && state.track && state.track.pos + 1 < state.track.files.length) step(1);
        else state.autoNext = false;
        if (state.mode === 'player') render();
    });

    player.addEventListener('error', () => {
        state.active = false;
        state.message = 'Playback error: ' + (player.error && player.error.message);
        state.autoNext = false;
        if (state.mode === 'player') render();
    });

    function step(delta) {
        if (!state.track) return;
        const next = state.track.pos + delta;
        if (next < 0 || next >= state.track.files.length) return;
        playTrack(state.track.dir, state.track.files, next);
    }

    function openFile(name) {
        const files = state.entries.filter(e => !e.dir).map(e => e.name);
        state.mode = 'player';
        state.index = MAIN_CONTROL;
        state.autoNext = true;
        refreshVolume();
        playTrack(state.path, files, files.indexOf(name));
        render();
        scheduleTick();
    }

    // Refresh the clock while a track plays; the next file starts from the ended event.
    function scheduleTick() {
        if (state.ticking || typeof setTimeout !== 'function') return;
        state.ticking = true;
        setTimeout(tick, TICK_MS);
    }

    function tick() {
        state.ticking = false;
        if (!state.track) return;
        const current = status();
        if (current.error) { state.message = 'Playback error: ' + current.error; state.autoNext = false; }
        if (state.mode === 'player') render();
        if (state.active || state.autoNext) scheduleTick();
    }

    function runPlayerRow(label) {
        state.message = '';
        if (label === 'Pause') player.pause();
        else if (label === 'Resume') { player.play(); state.autoNext = true; }
        else if (label === 'Play again' && state.track) { state.autoNext = true; playTrack(state.track.dir, state.track.files, state.track.pos); }
        else if (label === 'Stop') {
            state.autoNext = false;
            state.active = false;
            player.pause();
            player.currentTime = 0;
            ui.setStatus('Stopped');
        } else if (label === 'Previous') { state.autoNext = true; step(-1); }
        else if (label === 'Next') { state.autoNext = true; step(1); }
        else if (label === 'Mute' || label === 'Unmute') {
            player.muted = !player.muted;
            refreshVolume();
        }
        render();
        scheduleTick();
    }

    function changeVolume(delta) {
        player.volume = Math.max(0, Math.min(1, player.volume + delta / 100));
        refreshVolume();
        render();
    }

    // ---- input ------------------------------------------------------------
    function open() {
        state.fromStorage = false;
        state.message = '';
        refreshVolume();
        if (state.track && state.active) {
            state.mode = 'player';
            state.index = MAIN_CONTROL;
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
        ui.panel.classList.remove('player-mode', 'fade-wait');
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
                else openFile(row.name);
            } else if (back) {
                if (state.path !== MUSIC_ROOT) { loadDir(parentPath(state.path)); render(); xmbSlideBack(ui.list); }
                else close();
            }
        } else if (state.mode === 'player') {
            const rows = playerRows(status());
            if (left) move(-1, rows.length);
            else if (right) move(1, rows.length);
            else if (up) changeVolume(VOLUME_STEP);
            else if (down) changeVolume(-VOLUME_STEP);
            else if (enter) runPlayerRow(rows[state.index]);
            else if (back && state.onExit) {
                // The music keeps playing in the background; the caller (the storage browser) takes the screen
                const done = state.onExit;
                state.onExit = null;
                state.mode = null;
                ui.progress.classList.remove('visible');
                // fade out, then hide it and let the storage browser fade in
                ui.panel.classList.remove('fade-in', 'fade-wait');
                ui.panel.classList.add('fade-out');
                setTimeout(() => {
                    ui.panel.classList.remove('fade-out', 'fullscreen', 'player-mode');
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
        state.onExit = onExit || null;
        state.fromStorage = !!onExit;
        loadDir(parentPath(path));
        openFile(baseName(path));
    }

    // The status bar of the menu (network.js) reads the player through this
    window.musicStatus = status;

    return {
        open: open,
        play: playPath,
        // another player takes the sound: the track stops and the next one does not start by itself
        stop: () => {
            state.autoNext = false;
            state.active = false;
            player.pause();
        },
        handleKey: handleKey,
        isOpen: () => state.mode !== null,
        // a track is loaded and playing (paused counts)
        isPlaying: () => !!(state.track && state.active),
        // L2: the player over whatever is on screen, which is left as it is. Esc (or triangle) takes the player away
        // with a fade and calls onExit; the music goes on. Same screen change as a file opened from the storage browser.
        showPlayer: (onExit) => {
            if (!state.track) return false;
            state.message = '';
            state.onExit = onExit || null;
            state.fromStorage = true;
            state.mode = 'player';
            state.index = MAIN_CONTROL;
            refreshVolume();
            render();
            scheduleTick();
            return true;
        }
    };
}
