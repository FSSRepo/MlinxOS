// On-screen keyboard for the password field: drawn under the input and driven by the arrow keys and Enter, so the
// machine can be used without a physical keyboard. Typed characters still go to the input itself; this only adds
// the keys to press. "Go" calls onGo, which the caller uses to connect.
// It is only shown while a gamepad is connected (xmbHasGamepad() in xmb-view.js): with a physical keyboard the field is
// typed into directly. It appears when a gamepad is plugged in while the field is open, and goes away when it is lost.
function createOnScreenKeyboard(options) {
    const ui = options;           // { container, input, onGo }
    const LAYOUT = [
        ['1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 'Del'],
        ['q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'],
        ['a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';'],
        ['z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/'],
        ['Caps', 'Space', 'Clear', 'Go']
    ];
    const WIDE = { Space: true, Go: true, Caps: true, Clear: true };
    const state = { row: 0, col: 0, caps: false, open: false, wanted: false };   // wanted: the field is on screen

    // Caps Lock: letters stay upper case until Caps is pressed again. Digits and symbols are not affected.
    function label(key) {
        if (key.length === 1 && /[a-z]/.test(key) && state.caps) return key.toUpperCase();
        return key;
    }

    function render() {
        ui.container.innerHTML = '';
        LAYOUT.forEach((keys, r) => {
            const line = document.createElement('div');
            line.className = 'osk-row';
            keys.forEach((key, c) => {
                const cell = document.createElement('div');
                cell.className = 'osk-key' + (WIDE[key] ? ' wide' : '') +
                    (r === state.row && c === state.col ? ' active' : '') +
                    (key === 'Caps' && state.caps ? ' on' : '');
                cell.innerText = label(key);
                cell.addEventListener('click', () => { state.row = r; state.col = c; press(); });
                line.appendChild(cell);
            });
            ui.container.appendChild(line);
        });
    }

    function insert(text) {
        ui.input.value += text;
    }

    function press() {
        const key = LAYOUT[state.row][state.col];
        if (key === 'Caps') state.caps = !state.caps;
        else if (key === 'Del') ui.input.value = ui.input.value.slice(0, -1);
        else if (key === 'Space') insert(' ');
        else if (key === 'Clear') ui.input.value = '';
        else if (key === 'Go') { if (ui.onGo) ui.onGo(); return; }
        else insert(label(key));
        render();
    }

    function move(dr, dc) {
        state.row = Math.max(0, Math.min(LAYOUT.length - 1, state.row + dr));
        if (dr !== 0) state.col = Math.min(state.col, LAYOUT[state.row].length - 1);
        state.col = Math.max(0, Math.min(LAYOUT[state.row].length - 1, state.col + dc));
        render();
    }

    function usable() {
        return typeof xmbHasGamepad !== 'function' || xmbHasGamepad();
    }

    function open() {
        state.open = true;
        state.row = 0;
        state.col = 0;
        ui.container.classList.add('visible');
        render();
    }

    function close() {
        state.open = false;
        ui.container.classList.remove('visible');
        ui.container.innerHTML = '';
    }

    function show() {
        state.wanted = true;
        if (usable()) open();
    }

    function hide() {
        state.wanted = false;
        close();
    }

    window.addEventListener('gamepadconnected', () => {
        if (state.wanted && !state.open && usable()) open();
    });
    window.addEventListener('gamepaddisconnected', () => {
        if (state.open && !usable()) close();
    });

    // Returns true when the key was used by the keyboard (the caller must then ignore it).
    function handleKey(e) {
        if (!state.open) return false;
        const code = e.keyCode;
        if (code === 37) move(0, -1);
        else if (code === 39) move(0, 1);
        else if (code === 38) move(-1, 0);
        else if (code === 40) move(1, 0);
        else if (code === 13 || code === 10 || e.key === 'Enter') press();
        else return false;
        if (e.preventDefault) e.preventDefault();
        return true;
    }

    return {
        show: show,
        hide: hide,
        isOpen: () => state.open,
        handleKey: handleKey
    };
}
