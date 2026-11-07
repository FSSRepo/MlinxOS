// Small pictograms for the XMB option rows. The icon is chosen from the row
// label so every list (category, storage, network, music, video) shares one
// vocabulary without duplicating icon names in the data.
(function () {
    var EXACT = {
        'system info': 'info',
        'renderer debug': 'monitor',
        'system settings': 'adjust',
        'network settings': 'wifi',
        'web server': 'server',
        'node lite': 'terminal',
        'system update': 'download',
        'users': 'users',
        'power off': 'power',
        'install to disk': 'disc',
        'input devices': 'gamepad',
        'store': 'bag',
        'library': 'grid',
        'last played': 'clock',
        'trophies': 'trophy',
        'music player': 'music',
        'photo gallery': 'photo',
        'video player': 'movie',
        'youtube': 'youtube',
        'play disc': 'play',
        'disc info': 'disc',
        'eject': 'eject',
        'refresh': 'refresh',
        'browse files': 'folder',
        'create test file': 'file-plus',
        'unmount': 'eject',
        'mount': 'plug',
        'format as ext4': 'eraser',
        'format as fat': 'eraser',
        'close': 'close',
        'cancel': 'close',
        'ok': 'check',
        'erase everything': 'trash',
        'disconnect wi-fi': 'wifi-off',
        'back': 'left',
        'scan again': 'refresh',
        'scanning...': 'refresh',
        'turn on': 'power',
        'turn off': 'power',
        'pause': 'pause',
        'resume': 'play',
        'play again': 'play',
        'stop': 'stop',
        'previous': 'prev',
        'next': 'next',
        'mute': 'mute',
        'unmute': 'mute',
        'storage unavailable': 'alert',
        'no disks detected': 'alert'
    };

    function iconName(label) {
        var text = String(label == null ? '' : label).replace(/^\s+|\s+$/g, '');
        var key = text.toLowerCase();
        if (key === '..') return 'up';
        if (/^\[dir\]/i.test(text)) return 'folder';
        if (EXACT[key]) return EXACT[key];
        if (/^disconnect wi-fi/i.test(text)) return 'wifi-off';
        if (/^fetch\b/i.test(text)) return 'world';
        if (/^start the example web server/i.test(text)) return 'server';
        if (/^web server/i.test(text)) return 'server';
        if (/^wi-fi|^wifi/i.test(text)) return 'wifi';
        if (/^turn (on|off)/i.test(text)) return 'power';
        if (/^port:/i.test(text)) return 'plug';
        if (/%\s*(wpa|wep|open)\b/i.test(text)) return 'wifi';
        if (/\.mp3\b/i.test(text)) return 'music';
        if (/\.(mp4|mkv|avi|mov|webm)\b/i.test(text)) return 'movie';
        if (/\.(txt|md|log|json|csv|conf|ini|sh|js)\b/i.test(text)) return 'file';
        if (/\(\d+(\.\d+)?\s*[kmgt]?b\)$/i.test(text)) return 'file';
        if (/^volume\b/i.test(text)) return 'volume';
        if (/disk|partition|volume|dev\/|sd[a-z]\b/i.test(text)) return 'database';
        if (/fail|error|unavailable/i.test(text)) return 'alert';
        return 'chevron';
    }

    function iconElement(name) {
        var img = document.createElement('img');
        img.className = 'item-icon';
        img.setAttribute('src', 'icons/' + (name || 'chevron') + '.svg');
        img.setAttribute('alt', '');
        return img;
    }

    // Replace the row contents with a pictogram plus the label. Callers that
    // already know the type (a directory, a device, an SSID) pass the icon and
    // do not need to spell it out in the label.
    function fill(row, label, icon, meta) {
        row.innerHTML = '';
        var span = document.createElement('span');
        span.className = 'item-label';
        span.innerText = String(label);
        row.appendChild(iconElement(icon || iconName(label)));
        row.appendChild(span);
        if (meta) {
            var extra = document.createElement('span');
            extra.className = 'item-meta';
            extra.innerText = String(meta);
            row.appendChild(extra);
        }
        return row;
    }

    window.xmbIconName = iconName;
    window.xmbIconElement = iconElement;
    window.xmbFillItem = fill;
})();
