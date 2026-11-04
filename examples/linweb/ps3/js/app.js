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

    // Submenu data
    const subMenuData = {
        'System': ['System Info', 'Renderer Debug', 'System Settings', 'Network Settings', 'System Update', 'Users'],
        'Games': ['Store', 'Library', 'Last Played', 'Trophies'],
        'Multimedia': ['Music Player', 'Photo Gallery', 'Video Player', 'YouTube'],
        'DVD': ['Play Disc', 'Disc Info', 'Eject']
    };

    // Menu navigation handling
    const menuItems = document.querySelectorAll('.menu-item');
    const mainMenu = document.getElementById('main-menu');
    const subMenuContainer = document.getElementById('submenu-container');
    let currentIndex = 0;
    let subMenuIndex = -1; // -1 means we are in the main menu

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

    function loadSubMenu(category) {
        subMenuContainer.innerHTML = '';
        const items = subMenuData[category] || [];
        
        items.forEach((text, index) => {
            const div = document.createElement('div');
            div.className = 'submenu-item';
            div.innerText = text;
            subMenuContainer.appendChild(div); 
        });
        
        subMenuIndex = -1; // Reset when changing category
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
            const items = subMenuData[menuItems[currentIndex].getAttribute('data-name')];
            if (subMenuIndex > -1) {
                subMenuIndex--;
                updateSubMenuHighlight();
            }
        } else if (e.keyCode === 40) { // Down
            const items = subMenuData[menuItems[currentIndex].getAttribute('data-name')];
            if (subMenuIndex < items.length - 1) {
                subMenuIndex++;
                updateSubMenuHighlight();
            }
        } else if (e.key === 'Enter' || e.keyCode === 13 || e.keyCode === 10) { // OK / Enter
            if (subMenuIndex === -1) {
                subMenuIndex = 0;
                updateSubMenuHighlight();
            } else {
                const items = subMenuData[menuItems[currentIndex].getAttribute('data-name')];
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
