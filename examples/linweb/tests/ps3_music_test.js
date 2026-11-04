// Drives ps3/js/music.js with a tiny fake DOM and in-memory linweb.audio / linweb.storage.
//   node tests/ps3_music_test.js
const fs = require('fs');
const path = require('path');
const vm = require('vm');

let failures = 0;
function check(cond, message) {
    if (!cond) { failures++; console.log('FAIL: ' + message); }
}

class El {
    constructor() { this.children = []; this._class = ''; this.innerText = ''; }
    get className() { return this._class; }
    set className(v) { this._class = v; }
    get classList() {
        const self = this;
        const names = () => self._class.split(/\s+/).filter(Boolean);
        return {
            add: (c) => { if (!names().includes(c)) self._class = names().concat(c).join(' '); },
            remove: (c) => { self._class = names().filter(x => x !== c).join(' '); },
            contains: (c) => names().includes(c)
        };
    }
    set innerHTML(v) { this.children = []; }
    appendChild(c) { this.children.push(c); return c; }
}

const ui = { panel: new El(), title: new El(), subtitle: new El(), list: new El(), hint: new El() };
let statusText = '';
ui.setStatus = (t) => { statusText = t; };

// ---- fake backends ------------------------------------------------------------
const files = {
    '/mnt': [{ name: 'usb', dir: true, size: 0 }],
    '/mnt/usb': [
        { name: 'notes.txt', dir: false, size: 10 },
        { name: 'b.MP3', dir: false, size: 100 },
        { name: 'a.mp3', dir: false, size: 100 },
        { name: 'sub', dir: true, size: 0 }
    ],
    '/mnt/usb/sub': []
};
const audio = {
    played: [], track: null, paused: false, volume: 0, muted: true, cards: [{ index: 0, id: 'HDMI', name: 'HDA Intel HDMI', playback: true, hdmi: true },
            { index: 1, id: 'PCH', name: 'HDA Intel PCH', playback: true, hdmi: false }],
    error: '', failPlay: '',
    cardsFn() { return this.cards; },
};
const linweb = {
    storage: {
        readDir: (p) => files[p] ? { ok: true, error: '', entries: files[p], truncated: false } : { ok: false, error: 'missing ' + p, entries: [] }
    },
    audio: {
        cards: () => audio.cards,
        getVolume: () => ({ ok: true, percent: audio.volume, muted: audio.muted, control: 'Master' }),
        setVolume: (card, p) => { audio.volume = p; audio.muted = false; return { ok: true, percent: p, muted: audio.muted, control: 'Master' }; },
        setMute: (card, m) => { audio.muted = !!m; return { ok: true, error: '' }; },
        play: (p, card) => {
            if (audio.failPlay) return { ok: false, error: audio.failPlay };
            audio.played.push([p, card]); audio.track = p; audio.paused = false; return { ok: true, error: '' };
        },
        stop: () => { audio.track = null; return { ok: true, error: '' }; },
        pause: () => { audio.paused = true; return { ok: true, error: '' }; },
        resume: () => { audio.paused = false; return { ok: true, error: '' }; },
        status: () => ({ playing: !!audio.track, paused: audio.paused, path: audio.track || '', position: 65, duration: 200,
                         sampleRate: 44100, channels: 2, error: audio.error })
    }
};

let timers = [];
const context = {
    document: { createElement: () => new El() },
    linweb, console: { log() {}, error: console.error },
    setTimeout: (cb) => { timers.push(cb); return timers.length; }
};
vm.createContext(context);
vm.runInContext(fs.readFileSync(path.join(__dirname, '..', 'ps3', 'js', 'music.js'), 'utf8'), context);
const music = vm.runInContext('createMusicUI', context)(ui);

const KEY = { up: 38, down: 40, left: 37, right: 39, enter: 13, esc: 27 };
function press(code) { music.handleKey({ keyCode: code, key: code === 13 ? 'Enter' : '' }); }
function rows() { return ui.list.children.map(c => c.innerText); }
function activeRow() { const c = ui.list.children.find(x => x.classList.contains('active')); return c ? c.innerText : null; }
function runTimers() { const t = timers; timers = []; t.forEach(cb => cb()); }

// ---- browse and play ------------------------------------------------------------
music.open();
check(music.isOpen() && ui.panel.classList.contains('visible'), 'panel opens');
check(ui.title.innerText === 'Music: /mnt' && rows().join() === '[DIR] usb', 'root lists mounts: ' + rows());
press(KEY.enter);
check(ui.title.innerText === 'Music: /mnt/usb', 'entered usb');
check(rows().join('|') === '..|[DIR] sub|a.mp3|b.MP3', 'only folders and mp3, sorted: ' + rows().join('|'));
press(KEY.down); press(KEY.down);                         // a.mp3
check(activeRow() === 'a.mp3', 'a.mp3 selected');
press(KEY.enter);
check(audio.volume === 70 && !audio.muted, 'a muted 0% mixer is raised before playing');
check(audio.played.length === 1 && audio.played[0][0] === '/mnt/usb/a.mp3' && audio.played[0][1] === 1, 'plays a.mp3 on the analog card, not the HDMI one');
check(ui.title.innerText === 'Music Player' && ui.subtitle.innerText.includes('a.mp3') && ui.subtitle.innerText.includes('1:05 / 3:20'), 'player shows progress: ' + ui.subtitle.innerText);
check(statusText === 'Playing a.mp3', 'status line');

// ---- controls ----------------------------------------------------------------------
check(activeRow() === 'Pause', 'first row is Pause');
press(KEY.enter);
check(audio.paused && activeRow() === 'Resume', 'pause');
press(KEY.enter);
check(!audio.paused, 'resume');
press(KEY.right);
check(audio.volume === 75, 'volume +5: ' + audio.volume);
press(KEY.left); press(KEY.left);
check(audio.volume === 65, 'volume -10');
press(KEY.down); press(KEY.down); press(KEY.down);       // Next
check(activeRow() === 'Next', 'Next selected');
press(KEY.enter);
check(audio.played[1][0] === '/mnt/usb/b.MP3', 'next track');
press(KEY.enter);                                         // no track after b.MP3: stays
check(audio.played.length === 2, 'no wrap past the last file');

// ---- auto advance and errors -----------------------------------------------------------
press(KEY.up); press(KEY.up);                              // Stop
check(activeRow() === 'Stop', 'Stop selected');
press(KEY.enter);
check(audio.track === null, 'stopped');
runTimers();
check(audio.played.length === 2, 'no auto-advance after Stop');

press(KEY.up); press(KEY.up); press(KEY.up); press(KEY.up);
check(activeRow() === 'Play again', 'Play again row');
press(KEY.enter);                                         // starts a track and arms the timer
audio.track = null; audio.error = 'aplay failed (no sound device?)';   // the helper dies
runTimers();
check(ui.subtitle.innerText.startsWith('Playback error'), 'playback error shown: ' + ui.subtitle.innerText);
audio.error = '';

press(KEY.esc);                                            // back to the browser
check(ui.title.innerText === 'Music: /mnt/usb', 'Esc returns to browser');
press(KEY.esc); press(KEY.esc);
check(ui.title.innerText === 'Music: /mnt', 'Esc goes up');
press(KEY.esc);
check(!music.isOpen() && !ui.panel.classList.contains('visible'), 'Esc at the root closes');

const playedBefore = audio.played.length;
audio.cards = [];
music.open();
check(ui.subtitle.innerText.includes('No sound card'), 'warns when there is no card');
press(KEY.enter); press(KEY.down); press(KEY.down); press(KEY.enter);
check(audio.played.length === playedBefore, 'does not try to play without a card');

console.log(failures ? failures + ' check(s) failed' : 'all music checks passed');
process.exit(failures ? 1 : 0);
