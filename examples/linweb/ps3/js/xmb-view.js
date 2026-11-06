// Panel content replaces the active XMB column while the category stays visible.
function xmbSetPanel(panel, visible) {
    // A panel opens at the top and leaves its scroll behind when it closes
    if (visible !== panel.classList.contains('visible')) panel.scrollTop = 0;
    if (visible) panel.classList.add('visible');
    else panel.classList.remove('visible');
    var panels = ['system-info', 'storage-panel', 'music-panel', 'net-panel', 'nl-panel', 'vb-panel', 'in-panel'];
    var open = false;
    for (var i = 0; i < panels.length; i++) {
        var element = document.getElementById(panels[i]);
        if (element && element.classList.contains('visible')) open = true;
    }
    if (open) document.body.classList.add('xmb-panel-open');
    else document.body.classList.remove('xmb-panel-open');
}

// Opening a panel from the submenu: the same zoom and fade as the video player (vpOpen in style.css)
function xmbZoomIn(panel) {
    panel.classList.remove('zoom-in');
    void panel.offsetWidth;
    panel.classList.add('zoom-in');
}

// Going up a folder slides the list back in from the left. Removing the class and reading a layout
// property restarts the keyframes when the class is added again.
function xmbSlide(list, name) {
    list.classList.remove('slide-back', 'slide-forward', 'slide-out-left', 'slide-in-left');
    void list.offsetWidth;
    list.classList.add(name);
}

function xmbSlideBack(list) {
    xmbSlide(list, 'slide-back');
}

// Entering a folder slides the list in from the right.
function xmbSlideForward(list) {
    xmbSlide(list, 'slide-forward');
}

// Result of an operation: a small notice that slides in from the right, stays a few seconds and leaves by itself.
// A new toast replaces the one on screen.
// Failures ("Mount failed", "Cannot connect", ...) get the warning colour; everything else is a success.
const TOAST_MS = 3000;

function xmbToast(title, text) {
    let toast = document.getElementById('toast');
    if (!toast) {
        toast = document.createElement('div');
        toast.id = 'toast';
        toast.setAttribute('role', 'status');
        document.body.appendChild(toast);
    }
    const warn = /fail|cannot|could not|error|no /i.test(title);
    const icon = document.createElement('span');
    icon.className = 'toast-icon';
    icon.innerText = warn ? '!' : '✓';
    const head = document.createElement('div');
    head.className = 'toast-title';
    head.innerText = title;
    const body = document.createElement('div');
    body.className = 'toast-text';
    body.innerText = text || '';
    const content = document.createElement('div');
    content.className = 'toast-content';
    content.appendChild(head);
    content.appendChild(body);
    const bar = document.createElement('div');
    bar.className = 'toast-bar';
    toast.innerHTML = '';
    toast.appendChild(icon);
    toast.appendChild(content);
    toast.appendChild(bar);
    toast.classList.toggle('warn', warn);
    clearTimeout(toast.hideTimer);
    toast.classList.remove('show');
    void toast.offsetWidth;                  // restart the entrance and the countdown bar
    toast.classList.add('show');
    toast.hideTimer = setTimeout(() => toast.classList.remove('show'), TOAST_MS);
}

// Rows of a list are reused while their number stays the same: moving the selection then only changes
// classes, so the CSS transition on the selected row can run instead of the row being rebuilt.
function xmbListRows(list, count) {
    if (list.children.length !== count) list.innerHTML = '';
    const rows = [];
    for (let i = 0; i < count; i++) {
        let row = list.children[i];
        if (!row) {
            row = document.createElement('div');
            list.appendChild(row);
        }
        rows.push(row);
    }
    return rows;
}

// Keep the selected row visible on short screens without scrolling the page.
function xmbRevealSelection(panel) {
    if (typeof panel.querySelector !== 'function' || typeof panel.getBoundingClientRect !== 'function') return;
    var selected = panel.querySelector('.sp-item.active');
    if (!selected || typeof selected.getBoundingClientRect !== 'function') return;
    var bounds = panel.getBoundingClientRect();
    var row = selected.getBoundingClientRect();
    if (row.bottom > bounds.bottom) panel.scrollTop += row.bottom - bounds.bottom;
    else if (row.top < bounds.top) panel.scrollTop -= bounds.top - row.top;
}
