// Photo gallery for the PS3-style menu: a grid of the pictures found on the mounted storage (under /mnt) and a viewer
// for the one that is chosen. app.js creates it with createPhotoUI() and forwards key events while it is open.
//
// Viewer:  keyboard  Left/Right/Up/Down pan (Left/Right change picture while it is not zoomed), A / D change picture,
//                    Q zoom in, E zoom out, Enter shows it whole again, Esc goes back to the grid.
//          gamepad   the left stick pans, D-pad Up / Down zoom in / out, D-pad Left / Right and L1 / R1 change picture,
//                    cross (Enter) shows it whole again, triangle (Escape) goes back.
function createPhotoUI(options) {
    const ROOT = options.root || '/mnt';
    const TILE_W = 196;
    const TILE_H = 134;
    const TILE_GAP = 16;
    const MAX_DEPTH = 4;           // folders below the mount root that are searched
    const MAX_PICTURES = 400;
    const IMAGE_NAME = /\.(jpe?g|png|bmp|gif|tga)$/i;
    const FADE_MS = 300;
    const PANEL_WAIT_MS = 120;     // the panel stays invisible this long before it fades in
    const ZOOM_MAX = 8;
    const ZOOM_STEP_KEY = 1.25;    // one press of Q / E
    const ZOOM_STEP_PAD = 1.12;    // one repeat of the D-pad
    const PAN_KEY_PX = 90;         // one press of an arrow
    const PAN_STICK_PX_S = 1100;   // the stick pushed all the way
    const INFO_MS = 2200;          // how long the name and the zoom stay over the picture

    const ui = options;            // { panel, title, subtitle, grid, hint, viewer, image, info, setStatus }
    const state = {
        returnTo: null,            // set when the viewer was opened from the storage browser: called when it closes
        mode: null,                // null | grid | viewer
        pictures: [],              // { name, path }
        index: 0,
        scrollRow: 0,
        message: ''
    };
    // the picture on screen: its size when it fits the window, the zoom on top of that and the offset of the picture
    const view = { w: 0, h: 0, zoom: 1, x: 0, y: 0, stickX: 0, stickY: 0, last: 0, timer: false, infoTimer: null };

    function joinPath(base, name) {
        return base.replace(/\/+$/, '') + '/' + name;
    }

    // ---- finding the pictures ----------------------------------------------------------------------------------
    function scan() {
        const found = [];
        const queue = [{ path: ROOT, depth: 0 }];
        while (queue.length && found.length < MAX_PICTURES) {
            const dir = queue.shift();
            let result;
            try {
                result = linweb.storage.readDir(dir.path);
            } catch (err) {
                continue;
            }
            if (!result || !result.ok) continue;
            result.entries.forEach((entry) => {
                if (entry.dir) {
                    if (dir.depth < MAX_DEPTH) queue.push({ path: joinPath(dir.path, entry.name), depth: dir.depth + 1 });
                } else if (IMAGE_NAME.test(entry.name) && found.length < MAX_PICTURES) {
                    found.push({ name: entry.name, path: joinPath(dir.path, entry.name) });
                }
            });
        }
        found.sort((a, b) => (a.path < b.path ? -1 : 1));
        state.pictures = found;
    }

    // ---- the grid ------------------------------------------------------------------------------------------------
    function columns() {
        return Math.max(2, Math.floor((window.innerWidth - 160 + TILE_GAP) / (TILE_W + TILE_GAP)));
    }

    function visibleRows() {
        return Math.max(1, Math.floor((window.innerHeight - 230 + TILE_GAP) / (TILE_H + TILE_GAP)));
    }

    function renderGrid() {
        xmbSetPanel(ui.panel, true);
        const cols = columns();
        const rows = visibleRows();
        const total = state.pictures.length;
        const selectedRow = Math.floor(state.index / cols);
        if (selectedRow < state.scrollRow) state.scrollRow = selectedRow;
        if (selectedRow >= state.scrollRow + rows) state.scrollRow = selectedRow - rows + 1;
        state.scrollRow = Math.max(0, state.scrollRow);

        ui.title.innerText = 'Photo Gallery';
        if (!total) ui.subtitle.innerText = state.message || 'No pictures found. Mount a disk from Storage and put JPG, PNG, BMP or GIF files on it.';
        else ui.subtitle.innerText = (state.pictures[state.index] ? state.pictures[state.index].name : '') + '    ' + (state.index + 1) + ' / ' + total;

        // only the tiles of the visible rows exist: a folder of large pictures is not loaded as a whole
        ui.grid.innerHTML = '';
        for (let r = state.scrollRow; r < state.scrollRow + rows; r++) {
            const line = document.createElement('div');
            line.className = 'ph-row';
            let any = false;
            for (let c = 0; c < cols; c++) {
                const i = r * cols + c;
                if (i >= total) break;
                any = true;
                const tile = document.createElement('div');
                tile.className = 'ph-tile' + (i === state.index ? ' active' : '');
                const img = document.createElement('img');
                img.className = 'ph-thumb';
                img.setAttribute('src', state.pictures[i].path);
                img.setAttribute('alt', '');
                tile.appendChild(img);
                line.appendChild(tile);
            }
            if (any) ui.grid.appendChild(line);
        }
        ui.hint.innerText = total ? 'Arrows: select   Enter: view   Esc: back' : 'Esc: back';
    }

    // ---- the viewer ----------------------------------------------------------------------------------------------
    function applyView() {
        ui.image.style.transform = 'translate(' + view.x.toFixed(1) + 'px, ' + view.y.toFixed(1) + 'px) scale(' + view.zoom.toFixed(3) + ')';
    }

    function clampPan() {
        const maxX = Math.max(0, (view.w * view.zoom - window.innerWidth) / 2);
        const maxY = Math.max(0, (view.h * view.zoom - window.innerHeight) / 2);
        view.x = Math.max(-maxX, Math.min(maxX, view.x));
        view.y = Math.max(-maxY, Math.min(maxY, view.y));
    }

    function showInfo() {
        const picture = state.pictures[state.index];
        ui.info.innerText = (picture ? picture.name : '') + '    ' + (state.index + 1) + ' / ' + state.pictures.length +
            '    ' + Math.round(view.zoom * 100) + '%';
        ui.info.classList.add('show');
        clearTimeout(view.infoTimer);
        view.infoTimer = setTimeout(() => ui.info.classList.remove('show'), INFO_MS);
    }

    // The picture is laid out at its own size, measured, and then given the size that fits the window; the zoom is a
    // scale on top of that, so zoom 1 is "the whole picture" whatever its size.
    function fitPicture() {
        const img = ui.image;
        img.style.transform = 'none';
        img.style.width = 'auto';
        img.style.height = 'auto';
        const naturalW = img.offsetWidth;
        const naturalH = img.offsetHeight;
        if (!naturalW || !naturalH) {
            view.w = window.innerWidth;
            view.h = window.innerHeight;
        } else {
            const fit = Math.min(1, window.innerWidth / naturalW, window.innerHeight / naturalH);
            view.w = Math.round(naturalW * fit);
            view.h = Math.round(naturalH * fit);
            img.style.width = view.w + 'px';
            img.style.height = view.h + 'px';
        }
        view.zoom = 1;
        view.x = 0;
        view.y = 0;
        applyView();
    }

    function loadPicture(index) {
        if (!state.pictures.length) return;
        state.index = (index + state.pictures.length) % state.pictures.length;
        ui.image.setAttribute('src', state.pictures[state.index].path);
        fitPicture();
        showInfo();
    }

    // The old picture slides out the way the change goes, then the new one slides in from the other side. Changes made
    // while the slide out is running add up and are applied together.
    const SLIDE_OUT_MS = 160;
    const slide = { busy: false, timer: null, pending: 0 };
    function playSlide(name) {
        const stage = ui.image.parentNode;
        stage.classList.remove('pv-out-left', 'pv-out-right', 'pv-in-left', 'pv-in-right');
        void stage.offsetWidth;
        if (name) stage.classList.add(name);
    }

    function changePicture(step) {
        if (state.pictures.length < 2) return;
        slide.pending += step;
        if (slide.busy) return;
        slide.busy = true;
        playSlide(step > 0 ? 'pv-out-left' : 'pv-out-right');
        slide.timer = setTimeout(() => {
            const pending = slide.pending;
            slide.busy = false;
            slide.pending = 0;
            if (state.mode !== 'viewer') { playSlide(null); return; }
            loadPicture(state.index + pending);
            playSlide(pending >= 0 ? 'pv-in-right' : 'pv-in-left');
        }, SLIDE_OUT_MS);
    }

    function openViewer() {
        if (!state.pictures.length) return;
        state.mode = 'viewer';
        ui.viewer.classList.add('visible');
        ui.viewer.classList.remove('fade-out');
        view.stickX = 0;
        view.stickY = 0;
        clearTimeout(slide.timer);
        slide.busy = false;
        slide.pending = 0;
        playSlide(null);
        loadPicture(state.index);
        ui.setStatus('Viewing ' + state.pictures[state.index].name);
    }

    function closeViewer() {
        const back = state.returnTo;
        state.returnTo = null;
        state.mode = back ? null : 'grid';
        view.stickX = 0;
        view.stickY = 0;
        ui.info.classList.remove('show');
        ui.viewer.classList.add('fade-out');
        setTimeout(() => {
            if (state.mode === 'viewer') return;       // opened again during the fade
            ui.viewer.classList.remove('visible', 'fade-out');
        }, FADE_MS);
        if (back) back();                              // opened from the storage browser: back to its folder and row
        else renderGrid();
    }

    // A picture chosen in the storage browser: the viewer opens on it directly and cycles through the pictures of its
    // folder. Esc calls `back`, which returns to the browser.
    function openFile(path, back) {
        const slash = path.lastIndexOf('/');
        const dir = slash > 0 ? path.substring(0, slash) : '/';
        const found = [];
        try {
            const result = linweb.storage.readDir(dir);
            if (result && result.ok) {
                result.entries.forEach((entry) => {
                    if (!entry.dir && IMAGE_NAME.test(entry.name)) found.push({ name: entry.name, path: joinPath(dir, entry.name) });
                });
            }
        } catch (err) { /* only the chosen picture then */ }
        found.sort((a, b) => (a.path < b.path ? -1 : 1));
        let index = found.findIndex((p) => p.path === path);
        if (index < 0) { found.push({ name: path.substring(slash + 1), path: path }); index = found.length - 1; }
        state.pictures = found;
        state.index = index;
        state.returnTo = back;
        openViewer();
    }

    function zoomBy(factor) {
        const next = Math.max(1, Math.min(ZOOM_MAX, view.zoom * factor));
        if (next === view.zoom) { showInfo(); return; }
        view.zoom = next;
        clampPan();
        applyView();
        showInfo();
    }

    function panBy(dx, dy) {
        if (view.zoom <= 1.001) return;
        view.x -= dx;           // the view moves one way, the picture the other
        view.y -= dy;
        clampPan();
        applyView();
    }

    // The stick: the page gets "gamepadaxis" events while it is pushed; each one moves the picture by the time since the
    // last, so the speed does not depend on how often they arrive.
    function onAxis(e) {
        if (state.mode !== 'viewer') return;
        const now = Date.now();
        const dt = view.last ? Math.min(0.05, (now - view.last) / 1000) : 0;
        view.last = (e.x !== 0 || e.y !== 0) ? now : 0;
        if (dt > 0 && (e.x !== 0 || e.y !== 0)) panBy(e.x * PAN_STICK_PX_S * dt, e.y * PAN_STICK_PX_S * dt);
        else if (e.x !== 0 || e.y !== 0) panBy(e.x * 8, e.y * 8);
    }

    function handleViewerKey(e) {
        const code = e.keyCode;
        const key = (e.key || '').toLowerCase();
        const pad = e.source === 'hat';
        const stick = e.source === 'stick';
        const back = code === 27 || code === 461 || code === 8;
        const left = code === 37, up = code === 38, right = code === 39, down = code === 40;
        if (back) { closeViewer(); return; }
        if (key === 'q' || key === '+' || key === '=' || code === 107) { zoomBy(ZOOM_STEP_KEY); return; }
        if (key === 'e' || key === '-' || code === 109) { zoomBy(1 / ZOOM_STEP_KEY); return; }
        // A / D and the shoulder buttons L1 / R1 change picture
        if (key === 'a' || key === 'l1' || code === 1000) { changePicture(-1); return; }
        if (key === 'd' || key === 'r1' || code === 1001) { changePicture(1); return; }
        if (code === 13 || code === 10 || key === 'enter') { view.zoom = 1; view.x = 0; view.y = 0; applyView(); showInfo(); return; }
        if (stick) return;                                   // the stick pans through "gamepadaxis", not through keys
        if (pad && up) { zoomBy(ZOOM_STEP_PAD); return; }
        if (pad && down) { zoomBy(1 / ZOOM_STEP_PAD); return; }
        if (pad && left) { changePicture(-1); return; }
        if (pad && right) { changePicture(1); return; }
        // the keyboard: the arrows pan, or change picture while it is not zoomed
        if (left) { if (view.zoom > 1.001) panBy(-PAN_KEY_PX, 0); else changePicture(-1); }
        else if (right) { if (view.zoom > 1.001) panBy(PAN_KEY_PX, 0); else changePicture(1); }
        else if (up) panBy(0, -PAN_KEY_PX);
        else if (down) panBy(0, PAN_KEY_PX);
    }

    // ---- input ---------------------------------------------------------------------------------------------------
    function open() {
        state.message = '';
        scan();
        state.index = Math.min(state.index, Math.max(0, state.pictures.length - 1));
        state.scrollRow = 0;
        state.mode = 'grid';
        // the first frame of the full-screen panel is slow: it is drawn invisible (fade-wait) and the fade starts a
        // moment later, so the fade is not spent on that frame
        ui.panel.classList.remove('fade-in', 'fade-out');
        ui.panel.classList.add('fade-wait');
        renderGrid();
        setTimeout(() => {
            if (!ui.panel.classList.contains('fade-wait')) return;       // closed meanwhile
            ui.panel.classList.remove('fade-wait');
            ui.panel.classList.add('fade-in');
        }, PANEL_WAIT_MS);
    }

    function close() {
        state.mode = null;
        ui.panel.classList.remove('fade-in', 'fade-wait');
        ui.panel.classList.add('fade-out');
        setTimeout(() => {
            if (!ui.panel.classList.contains('fade-out')) return;        // opened again during the fade
            ui.panel.classList.remove('fade-out');
            xmbSetPanel(ui.panel, false);
        }, FADE_MS);
    }

    function handleKey(e) {
        if (state.mode === 'viewer') { handleViewerKey(e); return; }
        const code = e.keyCode;
        const cols = columns();
        const total = state.pictures.length;
        const back = code === 27 || code === 461 || code === 8;
        if (back) { close(); return; }
        if (!total) return;
        let next = state.index;
        if (code === 37) next = Math.max(0, state.index - 1);
        else if (code === 39) next = Math.min(total - 1, state.index + 1);
        else if (code === 38) next = state.index - cols >= 0 ? state.index - cols : state.index;
        else if (code === 40) next = state.index + cols < total ? state.index + cols : total - 1;
        else if (code === 13 || code === 10 || e.key === 'Enter') { openViewer(); return; }
        else return;
        if (next !== state.index) {
            state.index = next;
            renderGrid();
        }
    }

    window.addEventListener('gamepadaxis', onAxis);

    return {
        open: open,
        openFile: openFile,
        handleKey: handleKey,
        isOpen: () => state.mode !== null
    };
}
