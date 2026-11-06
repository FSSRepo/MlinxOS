// Video player for the PS3-style menu: browses /mnt for MP4 files (H.264 + AAC) and plays them full screen through
// linweb.video, shown by the <video> element of the page. The sound comes out of the sound card with linweb.audio's
// mixer. app.js creates it with createVideoUI() and forwards key events while it is open.
function createVideoUI(options) {
    const VIDEO_ROOT = '/mnt';
    const PREFERRED_DIR = '/mnt/video';       // where `make-iso.sh --video` puts the embedded videos
    const PANEL_ROWS = 8;
    const TICK_MS = 250;
    const SEEK_SECONDS = 10;
    const SEEK_BIG_SECONDS = 60;
    const VOLUME_STEP = 5;
    const DEFAULT_VOLUME = 70;               // a freshly booted mixer is often muted
    const CONTROLS_HIDE_MS = 4000;
    const SEEK_STEPS = 1000;                 // the position slider goes from 0 to 1000
    const FADE_MS = 300;                     // crossfade with the storage browser (fade-in / fade-out in style.css)

    // ui: { panel, title, subtitle, list, hint,        the file browser
    //       screen, controls, info, time, seek, duration,   the full-screen picture and its controls: seek and volume
    //       help, volumeLabel, volume,                        are <input type="range"> sliders
    //       setStatus }
    const ui = options;
    const state = {
        mode: null,                          // null | browser | player
        path: VIDEO_ROOT,
        entries: [],
        error: '',
        index: 0,
        message: '',
        card: -1,
        volume: null,                        // { percent, muted } or null without a mixer
        file: null,                          // path of the video being played
        name: '',
        showUntil: 0,                        // the controls stay visible until this time
        scrubbing: false,                    // the position slider is being dragged: the clock does not move it
        ticking: false
    };

    function isVideo(name) {
        return /\.(mp4|m4v)$/i.test(name);
    }

    function joinPath(base, name) {
        return base.replace(/\/+$/, '') + '/' + name;
    }

    function parentPath(path) {
        const cut = path.lastIndexOf('/');
        return cut <= 0 ? '/' : path.substring(0, cut);
    }

    function fmtTime(seconds) {
        const total = Math.max(0, Math.floor(seconds || 0));
        const s = total % 60;
        const m = Math.floor(total / 60) % 60;
        const h = Math.floor(total / 3600);
        const two = (n) => (n < 10 ? '0' : '') + n;
        return (h > 0 ? h + ':' + two(m) : m) + ':' + two(s);
    }

    function pickCard() {
        try {
            const cards = linweb.audio.cards();
            for (const card of cards) if (card.playback && !card.hdmi) return card.index;
            for (const card of cards) if (card.playback) return card.index;
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

    function ensureAudible() {
        if (!state.volume || !(state.volume.muted || state.volume.percent === 0)) return;
        const result = linweb.audio.setVolume(state.card, DEFAULT_VOLUME);
        if (result.ok) state.volume = result;
    }

    // ---- rendering ----------------------------------------------------------
    function browserRows() {
        const rows = [];
        if (state.path !== VIDEO_ROOT) rows.push({ label: 'Parent folder', up: true, icon: 'up' });
        state.entries.forEach(entry => {
            rows.push({
                label: entry.name,
                name: entry.name,
                dir: entry.dir,
                icon: entry.dir ? 'folder' : 'movie'
            });
        });
        return rows;
    }

    function drawBrowser() {
        const rows = browserRows();
        const index = state.index;
        xmbSetPanel(ui.panel, true);
        ui.title.innerText = 'Video: ' + state.path;
        ui.subtitle.innerText = state.message || state.error ||
            (state.entries.length ? state.entries.length + ' item(s)' : 'No MP4 files here. Embed some with make-iso.sh --video or mount a disk from Storage.');
        const start = Math.min(Math.max(0, index - 3), Math.max(0, rows.length - PANEL_ROWS));
        const end = Math.min(rows.length, start + PANEL_ROWS);
        // Reuse the row elements when only the selection moves, so the CSS transition can animate the change
        if (ui.list.children.length !== end - start) ui.list.innerHTML = '';
        for (let i = start; i < end; i++) {
            let row = ui.list.children[i - start];
            if (!row) {
                row = document.createElement('div');
                ui.list.appendChild(row);
            }
            row.className = 'sp-item' + (i === index ? ' active' : '');
            if (typeof xmbFillItem === 'function') xmbFillItem(row, rows[i].label, rows[i].icon);
            else row.innerText = rows[i].label;
        }
        ui.hint.innerText = 'Up/Down: select   Enter: open/play   Esc: ' + (state.path === VIDEO_ROOT ? 'close' : 'back');
        xmbRevealSelection(ui.panel);
    }

    function drawPlayer(status) {
        let line;
        if (state.message) line = state.message;
        else if (!status.loaded) line = 'Stopped';
        else if (status.ended) line = 'Finished   Enter: play again';
        else line = (status.paused ? 'Paused' : 'Playing') + (status.loop ? '  (loop)' : '') +
            '   ' + status.width + 'x' + status.height + (status.audio ? '' : '   no sound') +
            (status.error ? '   ' + status.error : '');
        ui.info.innerText = state.name + '\n' + line;
        ui.time.innerText = fmtTime(status.loaded ? status.position : 0);
        ui.duration.innerText = fmtTime(status.loaded ? status.duration : 0);
        if (!state.scrubbing) {
            ui.seek.value = status.loaded && status.duration > 0 ? Math.round(SEEK_STEPS * status.position / status.duration) : 0;
        }
        if (state.volume) {
            ui.volume.value = state.volume.percent;
            ui.volume.disabled = false;
            ui.volumeLabel.innerText = 'Volume ' + state.volume.percent + '%' + (state.volume.muted ? ' (muted)' : '');
        } else {
            ui.volume.disabled = true;
            ui.volumeLabel.innerText = 'No volume control';
        }
        ui.help.innerText = 'Enter: pause   Left/Right: -/+' + SEEK_SECONDS + ' s   Up/Down: volume   M: mute   L: loop   Esc: back';
        if (Date.now() < state.showUntil || !status.loaded || status.paused || status.ended) ui.controls.classList.add('visible');
        else ui.controls.classList.remove('visible');
    }

    function render() {
        if (state.mode === 'browser') {
            drawBrowser();
        } else if (state.mode === 'player') {
            drawPlayer(linweb.video.status());
        }
    }

    // ---- playback -----------------------------------------------------------
    function loadDir(path) {
        const result = linweb.storage.readDir(path);
        state.path = path;
        state.error = result.ok ? '' : result.error;
        state.entries = result.ok ? result.entries.filter(e => e.dir || isVideo(e.name)) : [];
        state.entries.sort((a, b) => (a.dir === b.dir) ? (a.name < b.name ? -1 : 1) : (a.dir ? -1 : 1));
        state.index = 0;
    }

    function showControls() {
        state.showUntil = Date.now() + CONTROLS_HIDE_MS;
    }

    function scheduleTick() {
        if (state.ticking || typeof setTimeout !== 'function') return;
        state.ticking = true;
        setTimeout(tick, TICK_MS);
    }

    function tick() {
        state.ticking = false;
        if (state.mode !== 'player') return;
        render();
        scheduleTick();
    }

    function play(path) {
        state.message = '';
        if (ui.beforePlay) ui.beforePlay();
        ensureAudible();
        const result = linweb.video.play(path, { card: state.card, loop: false });
        if (!result.ok) {
            state.message = 'Cannot play: ' + result.error;
            ui.setStatus('Video playback failed');
        } else {
            ui.setStatus('Playing ' + state.name);
        }
        showControls();
    }

    function openFile(name) {
        state.file = joinPath(state.path, name);
        state.name = name;
        state.mode = 'player';
        xmbSetPanel(ui.panel, false);
        ui.screen.classList.add('visible');
        refreshVolume();
        play(state.file);
        render();
        scheduleTick();
    }

    function leavePlayer() {
        ui.seek.blur();                      // a hidden slider must not keep the keyboard focus
        linweb.video.stop();
        ui.screen.classList.remove('visible');
        ui.controls.classList.remove('visible');
        state.mode = 'browser';
        state.message = '';
        loadDir(state.path);
        drawBrowser();
        ui.setStatus('Video stopped');
    }

    function seekBy(seconds) {
        const status = linweb.video.status();
        if (!status.loaded) return;
        linweb.video.seek(Math.max(0, Math.min(status.duration, status.position + seconds)));
    }

    function changeVolume(delta) {
        if (!state.volume) return;
        const result = linweb.audio.setVolume(state.card, Math.max(0, Math.min(100, state.volume.percent + delta)));
        if (result.ok) state.volume = result; else state.message = result.error;
    }

    function togglePause() {
        const status = linweb.video.status();
        if (!status.loaded) { play(state.file); return; }
        if (status.ended) { linweb.video.seek(0); return; }
        if (status.paused) linweb.video.resume(); else linweb.video.pause();
    }

    // ---- input --------------------------------------------------------------
    function open() {
        state.card = pickCard();
        state.message = state.card < 0 ? 'No sound card found: videos play without sound' : '';
        const status = linweb.video.status();
        if (status.loaded) {                 // still playing from an earlier visit
            state.file = status.path;
            state.name = status.path.substring(status.path.lastIndexOf('/') + 1);
            state.mode = 'player';
            ui.screen.classList.add('visible');
            refreshVolume();
            showControls();
            render();
            scheduleTick();
            return;
        }
        state.mode = 'browser';
        const preferred = linweb.storage.readDir(PREFERRED_DIR);
        loadDir(preferred.ok ? PREFERRED_DIR : VIDEO_ROOT);
        render();
    }

    function close() {
        ui.seek.blur();
        linweb.video.stop();
        ui.screen.classList.remove('visible');
        ui.controls.classList.remove('visible');
        xmbSetPanel(ui.panel, false);
        state.mode = null;
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
        const enter = code === 13 || code === 10 || e.key === 'Enter' || e.key === ' ';
        const back = code === 27 || code === 461 || code === 8;
        const key = (e.key || '').toLowerCase();

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
                if (state.path !== VIDEO_ROOT && state.path !== PREFERRED_DIR) { loadDir(parentPath(state.path)); render(); xmbSlideBack(ui.list); }
                else close();
            }
        } else if (state.mode === 'player') {
            showControls();
            // these keys are handled here: the focused slider (after a click on it) must not also move by itself
            if ((back || enter || left || right || up || down || key === 'm' || key === 'l') && e.preventDefault) e.preventDefault();
            if (back && state.onExit) {
                // fade out over the storage browser, which fades in when the picture has gone
                const done = state.onExit;
                state.onExit = null;
                ui.seek.blur();
                linweb.video.stop();
                ui.controls.classList.remove('visible');
                state.mode = null;
                ui.screen.classList.remove('fade-in');
                ui.screen.classList.add('fade-out');
                setTimeout(() => {
                    ui.screen.classList.remove('visible', 'fade-out');
                    done();
                }, FADE_MS);
                return;
            }
            if (back) { leavePlayer(); return; }
            if (enter) togglePause();
            else if (left) seekBy(e.shiftKey ? -SEEK_BIG_SECONDS : -SEEK_SECONDS);
            else if (right) seekBy(e.shiftKey ? SEEK_BIG_SECONDS : SEEK_SECONDS);
            else if (up) changeVolume(VOLUME_STEP);
            else if (down) changeVolume(-VOLUME_STEP);
            else if (key === 'm' && state.volume) {
                const result = linweb.audio.setMute(state.card, !state.volume.muted);
                if (!result.ok) state.message = result.error;
                refreshVolume();
            } else if (key === 'l') {
                linweb.video.setLoop(!linweb.video.status().loop);
            }
            render();
        }
    }

    // The sliders can also be dragged with the mouse: 'input' fires while dragging, 'change' when released.
    ui.seek.addEventListener('input', () => { state.scrubbing = true; });
    ui.seek.addEventListener('change', () => {
        state.scrubbing = false;
        const status = linweb.video.status();
        if (status.loaded && status.duration > 0) linweb.video.seek(status.duration * Number(ui.seek.value) / SEEK_STEPS);
    });
    ui.volume.addEventListener('input', () => {
        if (state.card < 0) return;
        const result = linweb.audio.setVolume(state.card, Number(ui.volume.value));
        if (result.ok) state.volume = result;
        showControls();
    });

    // A file opened in the storage browser: the player starts it; leaving the player goes back to its folder
    // onExit (optional) is called on Esc instead of the player's own browser: the caller takes the screen back
    function playPath(path, onExit) {
        state.card = pickCard();
        state.onExit = onExit || null;
        state.path = parentPath(path);
        state.file = path;
        state.name = path.substring(path.lastIndexOf('/') + 1);
        state.mode = 'player';
        xmbSetPanel(ui.panel, false);
        ui.screen.classList.remove('fade-in', 'fade-out');
        void ui.screen.offsetWidth;
        ui.screen.classList.add('visible');
        // opened from the storage browser: the picture fades in over it
        if (onExit) ui.screen.classList.add('fade-in');
        refreshVolume();
        play(path);
        render();
        scheduleTick();
    }

    return {
        open: open,
        play: playPath,
        handleKey: handleKey,
        isOpen: () => state.mode !== null
    };
}
