document.addEventListener('DOMContentLoaded', function() {
    console.log('App loaded');
    const statusElement = document.getElementById('status');
    const systemInfoPanel = document.getElementById('system-info');

    // Power Off: the engine's shutdown when it has one; until then, a "safe to turn off" screen (Enter turns it on again)
    const powerScreen = document.getElementById('power-off');
    // The screen fades to black for two seconds, then the real shutdown is called. Keys do nothing meanwhile.
    const SHUTDOWN_FADE_MS = 2000;
    const shutdownFade = document.getElementById('shutdown-fade');
    let shuttingDown = false;
    function powerOff() {
        if (shuttingDown) return;
        shuttingDown = true;
        statusElement.innerText = 'Shutting down...';
        shutdownFade.classList.remove('visible');
        void shutdownFade.offsetWidth;
        shutdownFade.classList.add('visible');
        setTimeout(() => {
            if (typeof linweb.system !== 'undefined' && linweb.system && typeof linweb.system.powerOff === 'function') {
                const result = linweb.system.powerOff();
                if (!result || result.ok !== false) return;        // the system is going down; the screen stays black
                statusElement.innerText = 'Shutdown failed';
                xmbToast('Shutdown failed', result.error || '');
                restoreColumn();
            } else {
                powerScreen.classList.add('visible');              // no shutdown in this engine: the "safe to turn off" screen
            }
            shutdownFade.classList.remove('visible');
            shuttingDown = false;
        }, SHUTDOWN_FADE_MS);
    }

    document.addEventListener('keydown', (e) => {
        if (shuttingDown) e.stopImmediatePropagation();
    }, true);

    document.addEventListener('keydown', (e) => {
        if (!powerScreen.classList.contains('visible')) return;
        e.stopImmediatePropagation();          // the screen is off: only Enter turns it on again
        if (e.key === 'Enter' || e.keyCode === 13) {
            powerScreen.classList.remove('visible');
            restoreColumn();
        }
    }, true);

    // System information takes the whole screen like the storage panel, and fades in and out
    const FADE_MS = 300;
    function restartFade(element, name) {
        element.classList.remove('fade-in', 'fade-out');
        void element.offsetWidth;
        element.classList.add(name);
    }

    // A fade along the bottom edge tells that the system information goes on below; it is hidden at the end.
    const moreMarker = document.getElementById('system-info-more');
    function updateMoreMarker() {
        if (!moreMarker) return;
        let more = false;
        const open = systemInfoPanel.classList.contains('visible') && !systemInfoPanel.classList.contains('fade-out');
        const kids = systemInfoPanel.children;
        if (open && kids && kids.length) {
            const panelBox = systemInfoPanel.getBoundingClientRect();
            const lastBox = kids[kids.length - 1].getBoundingClientRect();
            more = lastBox.bottom > panelBox.bottom + 1;
        }
        if (more) moreMarker.classList.add('visible');
        else moreMarker.classList.remove('visible');
    }

    function showSystemInfo() {
        const wasVisible = systemInfoPanel.classList.contains('visible') && !systemInfoPanel.classList.contains('fade-out');
        systemInfoPanel.classList.add('fullscreen');
        xmbSetPanel(systemInfoPanel, true);
        if (!wasVisible) restartFade(systemInfoPanel, 'fade-in');
        updateMoreMarker();
    }

    function hideSystemInfo() {
        if (!systemInfoPanel.classList.contains('visible') || systemInfoPanel.classList.contains('fade-out')) return;
        restartFade(systemInfoPanel, 'fade-out');
        updateMoreMarker();
        setTimeout(() => {
            // opened again during the fade: keep it
            if (!systemInfoPanel.classList.contains('fade-out')) return;
            systemInfoPanel.classList.remove('fade-out', 'fullscreen');
            xmbSetPanel(systemInfoPanel, false);
        }, FADE_MS);
    }
    statusElement.innerText = 'Application ready for webOS';

    // Handle webOS events if available
    if (window.PalmSystem) {
        console.log('webOS detected');
        statusElement.innerText += ' (webOS detected)';
    }

    // Submenu data. "Storage" is built from the detected disks (see js/storage.js).
    // Memory and mounts for System Info: in the live system the root is a squashfs with an overlay on
    // top, so the RAM used stays small and the medium shows as read-only.
    function infoDiv(className, text) {
        const div = document.createElement('div');
        div.className = className;
        div.innerText = text;
        return div;
    }

    // Input devices as a list like the mounts: the name large, the handlers and the axes under it in a smaller size.
    function appendInputDevices(panel, devices) {
        panel.appendChild(infoDiv('si-gap', ''));
        if (!devices.length) panel.appendChild(infoDiv('si-line', 'none found'));
        for (const device of devices) {
            const row = document.createElement('div');
            row.className = 'si-mount';
            const text = document.createElement('div');
            text.className = 'si-mount-text';
            text.appendChild(infoDiv('si-mount-point', device.name));
            text.appendChild(infoDiv('si-mount-detail', 'handlers: ' + device.handlers + '  ·  axes: ' + (device.axes ? 'yes' : 'no')));
            row.appendChild(typeof xmbIconElement === 'function' ? xmbIconElement(device.axes ? 'gamepad' : 'plug') : document.createElement('span'));
            row.appendChild(text);
            panel.appendChild(row);
        }
    }

    // Memory is a bar split into used and free (with the percentage used); each mount is a row with its mount point
    // large and its details (type, device, access) in a smaller size under it.
    function appendMemoryAndMounts(panel) {
        const m = linweb.system.memory();
        const used = Math.max(0, m.totalMB - m.availableMB);
        const percent = m.totalMB > 0 ? Math.round(used * 100 / m.totalMB) : 0;
        panel.appendChild(infoDiv('si-gap', ''));
        panel.appendChild(infoDiv('si-heading', 'Memory'));
        const bar = document.createElement('div');
        bar.className = 'si-bar';
        const fill = document.createElement('div');
        fill.className = 'si-bar-used';
        fill.style.width = percent + '%';
        fill.appendChild(infoDiv('si-bar-percent', percent + '%'));
        bar.appendChild(fill);
        panel.appendChild(bar);
        const legend = document.createElement('div');
        legend.className = 'si-legend';
        const entries = [['used', 'Used  ' + used + ' MB'], ['free', 'Free  ' + m.availableMB + ' MB'], ['total', 'Total  ' + m.totalMB + ' MB  (' + m.cachedMB + ' MB cached)']];
        entries.forEach(entry => {
            const item = document.createElement('div');
            item.className = 'si-legend-item';
            const swatch = document.createElement('span');
            swatch.className = 'si-swatch ' + entry[0];
            item.appendChild(swatch);
            item.appendChild(infoDiv('si-legend-text', entry[1]));
            legend.appendChild(item);
        });
        panel.appendChild(legend);
        panel.appendChild(infoDiv('si-gap', ''));
        panel.appendChild(infoDiv('si-heading', 'Mounts'));
        for (const mount of linweb.system.mounts()) {
            const row = document.createElement('div');
            row.className = 'si-mount';
            const text = document.createElement('div');
            text.className = 'si-mount-text';
            text.appendChild(infoDiv('si-mount-point', mount.mountpoint));
            text.appendChild(infoDiv('si-mount-detail', mount.type + '  ·  ' + mount.device + '  ·  ' + (mount.readOnly ? 'read-only' : 'read-write')));
            row.appendChild(typeof xmbIconElement === 'function' ? xmbIconElement('database') : document.createElement('span'));
            row.appendChild(text);
            panel.appendChild(row);
        }
    }

    // The installer is offered from the live medium only: a system already installed has nothing to install.
    function systemItems() {
        const items = ['System Info', 'Renderer Debug', 'System Settings', 'Network Settings', 'Web Server', 'Node Lite', 'System Update', 'Users', 'Input Devices', 'Power Off'];
        if (linweb.system.installAvailable() && !linweb.system.runsFromDisk()) {
            items.splice(items.indexOf('Power Off'), 0, 'Install to Disk');
        }
        return items;
    }

    const subMenuData = {
        'System': systemItems(),
        'Games': ['Store', 'Library', 'Last Played', 'Trophies'],
        'Multimedia': ['Music Player', 'Photo Gallery', 'Video Player', 'YouTube'],
        'DVD': ['Play Disc', 'Disc Info', 'Eject'],
        'Storage': []
    };

    // Menu navigation handling
    const menuItems = document.querySelectorAll('.menu-item');
    const mainMenu = document.getElementById('main-menu');
    const subMenuContainer = document.getElementById('submenu-container');
    const SLIDE_OUT_MS = 320;                 // matches the slide-out-left animation in style.css
    let currentIndex = 0;
    let subMenuIndex = -1; // -1 means we are in the main menu

    const storageUI = (typeof createStorageUI === 'function') ? createStorageUI({
        panel: document.getElementById('storage-panel'),
        title: document.getElementById('sp-title'),
        subtitle: document.getElementById('sp-subtitle'),
        list: document.getElementById('sp-list'),
        hint: document.getElementById('sp-hint'),
        setStatus: (text) => { statusElement.innerText = text; },
        onItemsChanged: () => { loadSubMenu('Storage', true); },
        // MP3 files play in the music player and MP4 files in the video player; other files open in the viewer
        // Esc in the player goes back to the same folder and row of the storage browser (back)
        openMedia: (path, back) => {
            const name = path.substring(path.lastIndexOf('/') + 1);
            const leave = () => { xmbSlide(subMenuContainer, 'slide-out-left'); back(); };
            if (/\.mp3$/i.test(name) && musicUI) { musicUI.play(path, leave); return true; }
            if (/\.(mp4|m4v)$/i.test(name) && videoUI) { videoUI.play(path, leave); return true; }
            return false;
        }
    }) : null;

    const musicUI = (typeof createMusicUI === 'function') ? createMusicUI({
        panel: document.getElementById('music-panel'),
        title: document.getElementById('mp-title'),
        subtitle: document.getElementById('mp-subtitle'),
        list: document.getElementById('mp-list'),
        hint: document.getElementById('mp-hint'),
        progress: document.getElementById('mp-progress'),
        setStatus: (text) => { statusElement.innerText = text; }
    }) : null;

    const networkUI = (typeof createNetworkUI === 'function') ? createNetworkUI({
        panel: document.getElementById('net-panel'),
        title: document.getElementById('np-title'),
        subtitle: document.getElementById('np-subtitle'),
        list: document.getElementById('np-list'),
        hint: document.getElementById('np-hint'),
        input: document.getElementById('np-input'),
        keyboard: document.getElementById('np-osk'),
        badge: document.getElementById('net-badge'),
        setStatus: (text) => { statusElement.innerText = text; }
    }) : null;
    if (networkUI) networkUI.startBadge();

    const videoUI = (typeof createVideoUI === 'function') ? createVideoUI({
        panel: document.getElementById('vb-panel'),
        title: document.getElementById('vb-title'),
        subtitle: document.getElementById('vb-subtitle'),
        list: document.getElementById('vb-list'),
        hint: document.getElementById('vb-hint'),
        screen: document.getElementById('vp-screen'),
        controls: document.getElementById('vp-controls'),
        info: document.getElementById('vp-info'),
        time: document.getElementById('vp-time'),
        seek: document.getElementById('vp-seek'),
        duration: document.getElementById('vp-duration'),
        help: document.getElementById('vp-help'),
        volumeLabel: document.getElementById('vp-volume-label'),
        volume: document.getElementById('vp-volume'),
        beforePlay: () => { if (musicUI) musicUI.stop(); },
        setStatus: (text) => { statusElement.innerText = text; }
    }) : null;

    const nodeLiteUI = (typeof createNodeLiteUI === 'function') ? createNodeLiteUI({
        panel: document.getElementById('nl-panel'),
        title: document.getElementById('nl-title'),
        subtitle: document.getElementById('nl-subtitle'),
        list: document.getElementById('nl-list'),
        hint: document.getElementById('nl-hint'),
        input: document.getElementById('nl-input'),
        setStatus: (text) => { statusElement.innerText = text; }
    }) : null;

    function currentCategory() {
        return menuItems[currentIndex].getAttribute('data-name');
    }

    function currentSubItems() {
        if (currentCategory() === 'Storage') return storageUI ? storageUI.labels() : [];
        return subMenuData[currentCategory()] || [];
    }

    function updateActiveItem(index) {
        // In the XMB, the active item stays at a fixed position
        // and the container scrolls.
        // We calculate the offset based on the index
        const itemWidth = 150; // Approximate width of each item + margin
        const offset = 300 - (index * itemWidth);
        mainMenu.style.transform = `translateX(${offset}px)`;

        menuItems.forEach((item, i) => {
            if (i === index) {
                item.classList.add('active');
                statusElement.innerText = `${item.getAttribute('data-name').toUpperCase()}`;
                loadSubMenu(item.getAttribute('data-name'));
            } else {
                item.classList.remove('active');
            }
        });
    }

    // Reverse of slide-out-left: the column comes back from the left, if it had left
    function restoreColumn() {
        if (subMenuContainer.classList.contains('slide-out-left')) xmbSlide(subMenuContainer, 'slide-in-left');
        // the option that was chosen stays selected when the column comes back
        updateSubMenuHighlight();
    }

    function loadSubMenu(category, keepIndex) {
        const previousIndex = subMenuIndex;
        subMenuContainer.classList.remove('slide-out-left');
        subMenuContainer.innerHTML = '';
        if (category === 'Storage') {
            if (storageUI) storageUI.buildItems();
        }
        const items = (category === 'Storage')
            ? (storageUI ? storageUI.rows() : [])
            : (subMenuData[category] || []).map(label => ({ label: label }));

        items.forEach((item, index) => {
            const div = document.createElement('div');
            div.className = 'submenu-item';
            div.style.animationDelay = (index * 45) + 'ms';
            if (typeof xmbFillItem === 'function') xmbFillItem(div, item.label, item.icon);
            else div.innerText = item.label;
            subMenuContainer.appendChild(div);
        });

        // Reset when changing category; keep the position when only refreshing the list
        subMenuIndex = keepIndex ? Math.min(previousIndex, items.length - 1) : -1;
        updateSubMenuHighlight();
    }

    function updateSubMenuHighlight() {
        const subItems = document.querySelectorAll('.submenu-item');
        subItems.forEach((item, i) => {
            item.classList.remove('debug-open');
            if (i === subMenuIndex) {
                item.classList.add('active');
            } else {
                item.classList.remove('active');
            }
        });

        // Keep the selected row at the top of the visible column; the clipped
        // viewport hides the rows that scroll above it. Row heights change with
        // the active font size, so measure the offset instead of assuming one.
        const active = subItems[subMenuIndex];
        if (subMenuIndex >= 0 && active && subItems.length) {
            let offset = 0;
            if (typeof active.getBoundingClientRect === 'function' && typeof subItems[0].getBoundingClientRect === 'function') {
                offset = active.getBoundingClientRect().top - subItems[0].getBoundingClientRect().top;
            }
            // Lightweight engines may not report geometry; keep the old estimate.
            if (!offset && subMenuIndex > 0) offset = subMenuIndex * 50;
            subMenuContainer.style.transform = `translateY(${-offset}px)`;
        } else {
            subMenuContainer.style.transform = `translateY(0)`;
        }
    }

    // Initialize state
    updateActiveItem(currentIndex);

    // Example of key handling for the remote control
    document.addEventListener('keydown', function(e) {

        // The storage panel (actions, file browser, confirmations) owns the keys while open.
        // When a panel closes itself on this key, the column slides back in (the reverse of slide-out-left)
        if (storageUI && storageUI.isOpen()) {
            storageUI.handleKey(e);
            if (!storageUI.isOpen()) restoreColumn();
            return;
        }
        if (musicUI && musicUI.isOpen()) {
            musicUI.handleKey(e);
            if (!musicUI.isOpen()) restoreColumn();
            return;
        }
        if (installUI.isOpen()) {
            installUI.handleKey(e);
            if (!installUI.isOpen()) restoreColumn();
            return;
        }
        if (networkUI && networkUI.isOpen()) {
            networkUI.handleKey(e);
            if (!networkUI.isOpen()) restoreColumn();
            return;
        }
        if (nodeLiteUI && nodeLiteUI.isOpen()) {
            nodeLiteUI.handleKey(e);
            if (!nodeLiteUI.isOpen()) restoreColumn();
            return;
        }
        if (videoUI && videoUI.isOpen()) {
            videoUI.handleKey(e);
            if (!videoUI.isOpen()) restoreColumn();
            return;
        }

        // A system information screen taller than the display scrolls with Up/Down (or the D-pad)
        if ((e.keyCode === 38 || e.keyCode === 40) && systemInfoPanel.classList.contains('visible') &&
            !systemInfoPanel.classList.contains('fade-out')) {
            systemInfoPanel.scrollTop += e.keyCode === 40 ? 80 : -80;
            updateMoreMarker();
            return;
        }

        // Not logged while a panel owns the keyboard: the network panel receives passwords.
        console.log('Key pressed:', e.keyCode);

        if (e.keyCode === 37) { // Left
            if (subMenuIndex === -1) {
                currentIndex = (currentIndex > 0) ? currentIndex - 1 : menuItems.length - 1;
                updateActiveItem(currentIndex);
            }
        } else if (e.keyCode === 39) { // Right
            if (subMenuIndex === -1) {
                currentIndex = (currentIndex < menuItems.length - 1) ? currentIndex + 1 : 0;
                updateActiveItem(currentIndex);
            }
        } else if (e.keyCode === 38) { // Up
            if (subMenuIndex > -1) {
                subMenuIndex--;
                updateSubMenuHighlight();
            }
        } else if (e.keyCode === 40) { // Down
            const items = currentSubItems();
            if (subMenuIndex < items.length - 1) {
                subMenuIndex++;
                updateSubMenuHighlight();
            }
        } else if (e.key === 'Enter' || e.keyCode === 13 || e.keyCode === 10) { // OK / Enter
            if (subMenuIndex === -1) {
                subMenuIndex = 0;
                updateSubMenuHighlight();
            } else if (currentCategory() === 'Storage') {
                xmbSlide(subMenuContainer, 'slide-out-left');
                if (storageUI) storageUI.activate(subMenuIndex);
            } else {
                const items = currentSubItems();
                const selected = items[subMenuIndex];
                const selectedElement = document.querySelectorAll('.submenu-item')[subMenuIndex];
                console.log('Selected:', selected);
                // Only options that open something slide out; the "Running ..." placeholders keep the column
                const hasFunction = selected === 'System Info' || selected === 'Renderer Debug' || selected === 'Input Devices' ||
                    selected === 'Install to Disk' ||
                    ((selected === 'Network Settings' || selected === 'Web Server') && networkUI) ||
                    (selected === 'Node Lite' && nodeLiteUI) ||
                    (selected === 'Video Player' && videoUI) ||
                    (selected === 'Music Player' && musicUI);
                if (hasFunction || selected === 'Power Off') xmbSlide(subMenuContainer, 'slide-out-left');
                if (selected === 'System Info' || selected === 'Renderer Debug') {
                    selectedElement.classList.remove('active');
                    selectedElement.classList.add('debug-open');
                    const gpu = linweb.renderer.info();
                    const glErrors = selected === 'Renderer Debug' ? linweb.debug.glErrors() : [];
                    systemInfoPanel.innerText = [
                        'MLinxOS system information',
                        `Session: ${gpu.session || 'unknown'}`,
                        `GPU vendor: ${gpu.vendor}`,
                        `Renderer: ${gpu.renderer}`,
                        `OpenGL: ${gpu.version}`,
                        `GLSL: ${gpu.shadingLanguageVersion}`,
                        `Rendering: ${gpu.software ? 'SOFTWARE (llvmpipe/softpipe)' : (gpu.renderer === 'unavailable' ? 'renderer unavailable' : 'hardware renderer reported')}`,
                        `OpenGL errors: ${glErrors.length ? glErrors.join(', ') : 'none'}`,
                        ...(selected === 'System Info' ? [] : ['Press Esc to close'])
                    ].join('\n');
                    if (selected === 'System Info') {
                        appendMemoryAndMounts(systemInfoPanel);
                        systemInfoPanel.appendChild(infoDiv('si-gap', ''));
                        systemInfoPanel.appendChild(infoDiv('si-line', 'Press Esc to close'));
                    }
                    showSystemInfo();
                    statusElement.innerText = selected;
                    if (selected === 'Renderer Debug') linweb.debug.renderer();
                } else if ((selected === 'Network Settings' || selected === 'Web Server') && networkUI) {
                    xmbSetPanel(systemInfoPanel, false);
                    networkUI.open(selected === 'Web Server' ? 'webserver' : 'menu');
                } else if (selected === 'Node Lite' && nodeLiteUI) {
                    xmbSetPanel(systemInfoPanel, false);
                    nodeLiteUI.open();
                } else if (selected === 'Video Player' && videoUI) {
                    xmbSetPanel(systemInfoPanel, false);
                    // The full-screen player would cover the column before it has slid out: open it after the slide
                    setTimeout(() => videoUI.open(), SLIDE_OUT_MS);
                } else if (selected === 'Music Player' && musicUI) {
                    xmbSetPanel(systemInfoPanel, false);
                    setTimeout(() => musicUI.open(), SLIDE_OUT_MS);
                } else if (selected === 'Input Devices') {
                    selectedElement.classList.remove('active');
                    selectedElement.classList.add('debug-open');
                    const devices = linweb.system.inputDevices();
                    systemInfoPanel.innerText = ['Input Devices', 'axes: yes = sticks/triggers, likely a gamepad'].join('\n');
                    appendInputDevices(systemInfoPanel, devices);
                    systemInfoPanel.appendChild(infoDiv('si-gap', ''));
                    systemInfoPanel.appendChild(infoDiv('si-line', 'Press Esc to close'));
                    showSystemInfo();
                    statusElement.innerText = selected;
                } else if (selected === 'Install to Disk') {
                    xmbSetPanel(systemInfoPanel, false);
                    installUI.open();
                } else if (selected === 'Power Off') {
                    xmbSetPanel(systemInfoPanel, false);
                    powerOff();
                } else {
                    xmbSetPanel(systemInfoPanel, false);
                    statusElement.innerText = `Running ${selected}...`;
                }
            }
        } else if (e.keyCode === 461 || e.keyCode === 27) { // Back (webOS o ESC)
            // Esc closes an open panel and leaves the option that opened it selected
            const panelWasOpen = systemInfoPanel.classList.contains('visible') && !systemInfoPanel.classList.contains('fade-out');
            hideSystemInfo();
            restoreColumn();
            if (panelWasOpen) {
                document.querySelectorAll('.submenu-item.debug-open').forEach(item => item.classList.remove('debug-open'));
                updateSubMenuHighlight();
                return;
            }
            if (subMenuIndex > -1) {
                subMenuIndex = -1;
                updateSubMenuHighlight();
            }
        }
    });

    // Also allow clicks
    menuItems.forEach((item, index) => {
        item.addEventListener('click', () => {
            // The open column owns navigation until it is closed with Escape.
            if (document.body.classList.contains('xmb-panel-open')) return;
            currentIndex = index;
            updateActiveItem(currentIndex);
        });
    });
});
