document.addEventListener('DOMContentLoaded', function() {
    console.log('App loaded');
    const statusElement = document.getElementById('status');
    const systemInfoPanel = document.getElementById('system-info');
    statusElement.innerText = 'Application ready for webOS';

    // Handle webOS events if available
    if (window.PalmSystem) {
        console.log('webOS detected');
        statusElement.innerText += ' (webOS detected)';
    }

    // Submenu data. "Storage" is built from the detected disks (see js/storage.js).
    const subMenuData = {
        'System': ['System Info', 'Renderer Debug', 'System Settings', 'Network Settings', 'System Update', 'Users'],
        'Games': ['Store', 'Library', 'Last Played', 'Trophies'],
        'Multimedia': ['Music Player', 'Photo Gallery', 'Video Player', 'YouTube'],
        'DVD': ['Play Disc', 'Disc Info', 'Eject'],
        'Storage': []
    };

    // Menu navigation handling
    const menuItems = document.querySelectorAll('.menu-item');
    const mainMenu = document.getElementById('main-menu');
    const subMenuContainer = document.getElementById('submenu-container');
    let currentIndex = 0;
    let subMenuIndex = -1; // -1 means we are in the main menu

    const storageUI = (typeof createStorageUI === 'function') ? createStorageUI({
        panel: document.getElementById('storage-panel'),
        title: document.getElementById('sp-title'),
        subtitle: document.getElementById('sp-subtitle'),
        list: document.getElementById('sp-list'),
        hint: document.getElementById('sp-hint'),
        setStatus: (text) => { statusElement.innerText = text; },
        onItemsChanged: () => { loadSubMenu('Storage', true); }
    }) : null;

    const musicUI = (typeof createMusicUI === 'function') ? createMusicUI({
        panel: document.getElementById('music-panel'),
        title: document.getElementById('mp-title'),
        subtitle: document.getElementById('mp-subtitle'),
        list: document.getElementById('mp-list'),
        hint: document.getElementById('mp-hint'),
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

    function loadSubMenu(category, keepIndex) {
        const previousIndex = subMenuIndex;
        subMenuContainer.innerHTML = '';
        if (category === 'Storage') {
            if (storageUI) storageUI.buildItems();
        }
        const items = (category === 'Storage') ? (storageUI ? storageUI.labels() : []) : (subMenuData[category] || []);

        items.forEach((text, index) => {
            const div = document.createElement('div');
            div.className = 'submenu-item';
            div.innerText = text;
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

            // Mark items above the active one for scrolling
            if (subMenuIndex >= 0 && i < subMenuIndex) {
                item.classList.add('above-active');
            } else {
                item.classList.remove('above-active');
            }
        });

        // Vertical offset of the submenu
        if (subMenuIndex >= 0) {
            const offset = -(subMenuIndex * 50); // 50px is the height of each submenu-item
            subMenuContainer.style.transform = `translateY(${offset}px)`;
        } else {
            subMenuContainer.style.transform = `translateY(0)`;
        }
    }

    // Initialize state
    updateActiveItem(currentIndex);

    // Example of key handling for the remote control
    document.addEventListener('keydown', function(e) {
        console.log('Key pressed:', e.keyCode);

        // The storage panel (actions, file browser, confirmations) owns the keys while open.
        if (storageUI && storageUI.isOpen()) {
            storageUI.handleKey(e);
            return;
        }
        if (musicUI && musicUI.isOpen()) {
            musicUI.handleKey(e);
            return;
        }

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
                if (storageUI) storageUI.activate(subMenuIndex);
            } else {
                const items = currentSubItems();
                const selected = items[subMenuIndex];
                const selectedElement = document.querySelectorAll('.submenu-item')[subMenuIndex];
                console.log('Selected:', selected);
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
                        'Press Esc to close'
                    ].join('\n');
                    systemInfoPanel.classList.add('visible');
                    statusElement.innerText = selected;
                    if (selected === 'Renderer Debug') linweb.debug.renderer();
                } else if (selected === 'Music Player' && musicUI) {
                    systemInfoPanel.classList.remove('visible');
                    musicUI.open();
                } else {
                    systemInfoPanel.classList.remove('visible');
                    statusElement.innerText = `Running ${selected}...`;
                }
            }
        } else if (e.keyCode === 461 || e.keyCode === 27) { // Back (webOS o ESC)
            systemInfoPanel.classList.remove('visible');
            if (subMenuIndex > -1) {
                subMenuIndex = -1;
                updateSubMenuHighlight();
            }
        }
    });

    // Also allow clicks
    menuItems.forEach((item, index) => {
        item.addEventListener('click', () => {
            currentIndex = index;
            updateActiveItem(currentIndex);
        });
    });
});
