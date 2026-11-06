/* Browser preview for the Steward OS example. Never replace a native bridge. */
(function () {
    if (typeof linweb !== 'undefined' || typeof window.linweb !== 'undefined') return;

    const unavailable = { ok: false, error: 'Unavailable in browser preview (simulated environment).' };
    const files = {
        '/mnt/demo/README.txt': 'Browser preview\n\nThis disk is simulated in memory. No real disks or files are modified.\n',
        '/mnt/demo/Documents/notes.txt': 'Example document from the simulated disk.\n'
    };
    // Simulated media: the players read these as regular files, while play()
    // only advances a clock so the transport UI can be reviewed. The layout
    // mirrors an image built with `make-iso.sh --music --video`.
    const media = {
        '/mnt/music/Neon Skyline.mp3': { size: 5234567, duration: 208 },
        '/mnt/music/Blue Horizon.mp3': { size: 4812345, duration: 186 },
        '/mnt/music/Slow Drive.mp3': { size: 6120400, duration: 241 },
        '/mnt/video/Demo Reel.mp4': { size: 18400000, duration: 96 }
    };
    const folders = ['/mnt', '/mnt/demo', '/mnt/music', '/mnt/video', '/mnt/demo/Documents', '/mnt/demo/Music', '/mnt/demo/Videos'];
    const disk = {
        name: 'Demo disk (simulated)', path: '/dev/demo', sizeBytes: 1024 * 1024 * 1024,
        model: 'Browser preview', fstype: 'ext4', label: 'DEMO', mounted: true,
        mountpoint: '/mnt/demo', optical: false, readOnly: false, partitions: []
    };
    let connected = '';
    const wifiNetworks = [
        { ssid: 'Demo Wi-Fi (simulated)', signal: 85, security: 'WPA2', secure: true },
        { ssid: 'Demo Guest (simulated)', signal: 60, security: 'Open', secure: false }
    ];
    const ok = () => ({ ok: true });
    let volume = 70, muted = false;
    const volumeStatus = () => ({ ok: true, percent: volume, muted: muted });

    // ---- simulated transports ------------------------------------------------
    // The players only read status(); the mock advances a clock so the progress
    // bar, the seek slider and the state messages behave like real playback.
    const audio = { path: '', playing: false, paused: false, base: 0, started: 0, duration: 0 };
    function audioPosition() {
        if (!audio.playing || audio.paused) return audio.base;
        return Math.min(audio.duration, audio.base + (Date.now() - audio.started) / 1000);
    }
    function audioStatus() {
        const position = audioPosition();
        return {
            playing: audio.playing && !audio.paused && position < audio.duration,
            paused: audio.playing && audio.paused,
            position: position, duration: audio.duration,
            sampleRate: audio.playing ? 44100 : 0, channels: audio.playing ? 2 : 0,
            path: audio.path, error: ''
        };
    }
    function audioPlay(path) {
        const info = media[path];
        if (!info) return unavailable;
        audio.path = path; audio.playing = true; audio.paused = false; audio.base = 0;
        audio.started = Date.now(); audio.duration = info.duration;
        return ok();
    }
    function audioPause() {
        if (audio.playing && !audio.paused) { audio.base = audioPosition(); audio.paused = true; }
        return ok();
    }
    function audioResume() {
        if (audio.playing && audio.paused) { audio.paused = false; audio.started = Date.now(); }
        return ok();
    }
    function audioStop() {
        audio.playing = false; audio.paused = false; audio.base = 0; audio.duration = 0;
        return ok();
    }

    const video = { path: '', loaded: false, paused: false, base: 0, started: 0, duration: 0, loop: false };
    function videoPosition() {
        if (!video.loaded || video.paused) return video.base;
        return Math.min(video.duration, video.base + (Date.now() - video.started) / 1000);
    }
    function videoStatus() {
        const position = videoPosition();
        return {
            loaded: video.loaded,
            playing: video.loaded && !video.paused && position < video.duration,
            paused: video.loaded && video.paused,
            ended: video.loaded && position >= video.duration,
            position: position, duration: video.duration, loop: video.loop,
            width: 1280, height: 720, audio: true, path: video.path, error: ''
        };
    }
    function paintVideoSlate(path) {
        if (typeof document.getElementById !== 'function') return;
        const screen = document.getElementById('vp-screen');
        if (!screen || typeof encodeURIComponent !== 'function') return;
        const name = String(path.substring(path.lastIndexOf('/') + 1));
        const svg = '<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="720">' +
            '<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1">' +
            '<stop offset="0" stop-color="#2c4a33"/><stop offset="1" stop-color="#0b140e"/></linearGradient></defs>' +
            '<rect width="1280" height="720" fill="url(#g)"/>' +
            '<circle cx="640" cy="320" r="56" fill="none" stroke="#ffffff" stroke-opacity="0.45" stroke-width="4"/>' +
            '<path d="M624 294l48 26-48 26z" fill="#ffffff" fill-opacity="0.8"/>' +
            '</svg>';
        // the caption is HTML text over the picture (#vp-slate), not text inside the SVG
        const caption = document.getElementById('vp-slate-name');
        if (caption) caption.innerText = name;
        screen.style.background = 'url("data:image/svg+xml;charset=utf-8,' + encodeURIComponent(svg) + '") center / cover no-repeat';
    }
    function videoPlay(path, options) {
        const info = media[path];
        if (!info) return unavailable;
        video.path = path; video.loaded = true; video.paused = false; video.base = 0;
        video.started = Date.now(); video.duration = info.duration;
        video.loop = !!(options && options.loop);
        paintVideoSlate(path);
        return ok();
    }
    function videoPause() {
        if (video.loaded && !video.paused) { video.base = videoPosition(); video.paused = true; }
        return ok();
    }
    function videoResume() {
        if (video.loaded && video.paused) { video.paused = false; video.started = Date.now(); }
        return ok();
    }
    function videoStop() {
        video.loaded = false; video.paused = false; video.base = 0; video.duration = 0;
        return ok();
    }
    function videoSeek(seconds) {
        if (!video.loaded) return unavailable;
        video.base = Math.max(0, Math.min(video.duration, Number(seconds) || 0));
        video.started = Date.now();
        return { ok: true, position: video.base };
    }
    function videoLoop(value) { video.loop = !!value; return ok(); }

    window.linweb = {
        renderer: { info: () => ({ session: 'Browser preview (simulated)', vendor: 'Browser', renderer: 'Browser', version: 'N/A', shadingLanguageVersion: 'N/A', software: false }) },
        debug: { glErrors: () => [], renderer: () => console.info('Browser preview: native renderer debugging unavailable.') },
        storage: {
            list: () => [disk],
            mount: path => path === disk.path ? (disk.mounted = true, { ok: true, mountpoint: disk.mountpoint }) : unavailable,
            unmount: path => path === disk.mountpoint ? (disk.mounted = false, ok()) : unavailable,
            format: () => ({ ok: false, error: 'Formatting is disabled in browser preview.' }),
            readDir: path => {
                // The image-level /mnt/music and /mnt/video folders stay available
                // even when the simulated disk is unmounted.
                const imageFolder = path === '/mnt' || path === '/mnt/music' || path === '/mnt/video';
                if (folders.indexOf(path) < 0 || (!imageFolder && !disk.mounted)) return { ok: false, error: 'Directory unavailable in the simulated disk.' };
                const prefix = path === '/' ? '/' : path + '/';
                const entries = [];
                folders.forEach(folder => {
                    const name = folder.slice(prefix.length);
                    if (folder.indexOf(prefix) === 0 && name && name.indexOf('/') < 0) entries.push({ name: name, dir: true, size: 0 });
                });
                Object.keys(files).forEach(file => {
                    const name = file.slice(prefix.length);
                    if (file.indexOf(prefix) === 0 && name && name.indexOf('/') < 0) entries.push({ name: name, dir: false, size: files[file].length });
                });
                Object.keys(media).forEach(file => {
                    const name = file.slice(prefix.length);
                    if (file.indexOf(prefix) === 0 && name && name.indexOf('/') < 0) entries.push({ name: name, dir: false, size: media[file].size });
                });
                return { ok: true, entries: entries, truncated: false };
            },
            readFile: (path, limit) => Object.prototype.hasOwnProperty.call(files, path) && disk.mounted
                ? { ok: true, data: files[path].slice(0, limit || files[path].length), truncated: !!limit && files[path].length > limit }
                : { ok: false, error: 'File not found on the simulated disk.' },
            writeFile: (path, data) => {
                if (!disk.mounted || path.indexOf('/mnt/demo/') !== 0 || path.indexOf('..') >= 0) return unavailable;
                files[path] = String(data);
                return { ok: true, bytes: files[path].length };
            }
        },
        wifi: {
            status: () => ({ available: true, state: connected ? 'CONNECTED' : 'DISCONNECTED', ssid: connected, ip: connected ? '192.0.2.1' : '', error: '' }),
            scan: ok,
            networks: () => wifiNetworks.map(n => Object.assign({}, n, { connected: n.ssid === connected })),
            connect: ssid => { if (!wifiNetworks.some(n => n.ssid === ssid)) return unavailable; connected = ssid; return ok(); },
            disconnect: () => { connected = ''; return ok(); }
        },
        network: { addresses: () => connected ? [{ iface: 'simulated', ip: '192.0.2.1' }] : [] },
        webserver: { status: () => ({ running: false, port: 0, addresses: [] }), start: () => unavailable, stop: () => unavailable },
        node: { available: () => false, start: () => unavailable, stop: () => unavailable,
            poll: () => ({ found: false, running: false, timedOut: false, exitCode: -1, output: '' }) },
        audio: {
            cards: () => [{ index: 0, playback: true, hdmi: false, name: 'Simulated audio' }],
            getVolume: volumeStatus,
            setVolume: (card, value) => { volume = Math.max(0, Math.min(100, Number(value) || 0)); return volumeStatus(); },
            setMute: (card, value) => { muted = !!value; return volumeStatus(); },
            status: audioStatus, play: audioPlay, pause: audioPause, resume: audioResume, stop: audioStop
        },
        video: { status: videoStatus, play: videoPlay, pause: videoPause, resume: videoResume,
            stop: videoStop, seek: videoSeek, setLoop: videoLoop },
        // the steward-os demo also reads these; the preview has no installer, power or hardware to report
        system: {
            installAvailable: () => false,
            runsFromDisk: () => false,
            installStatus: () => ({ running: false, done: false, exit_code: -1, log: '' }),
            install: () => unavailable,
            powerOff: () => unavailable,
            memory: () => ({ totalMB: 0, availableMB: 0 }),
            mounts: () => [],
            inputDevices: () => []
        }
    };

    document.addEventListener('DOMContentLoaded', function () {
        const label = document.createElement('div');
        label.id = 'browser-preview-label';
        label.textContent = 'BROWSER PREVIEW · SIMULATED SYSTEM';
        document.body.appendChild(label);
    });
}());
