// CommonJS server: node-lite example.exe ... run with:  node-lite example/server.js [port]
const http = require('http');
const fs = require('node:fs');
const path = require('path');
const os = require('os');
const { URL } = require('url');
const greeting = require('./lib/greeting');   // local module
const banner = require('tiny-banner');        // package from example/node_modules

const port = Number(process.argv[2] || process.env.PORT || 8080);
const started = Date.now();

function json(res, status, value) {
    res.writeHead(status, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(value, null, 2) + '\n');
}

const server = http.createServer((req, res) => {
    const url = new URL(req.url, 'http://' + (req.headers.host || 'localhost'));

    if (req.method === 'GET' && url.pathname === '/') {
        res.setHeader('Content-Type', 'text/html; charset=utf-8');
        res.end(`<h1>${banner('node-lite')}</h1>
<p>${greeting(url.searchParams.get('name') || 'world')}</p>
<ul>
<li>${os.type()} ${os.release()} on ${os.hostname()}</li>
<li>up ${Math.round((Date.now() - started) / 1000)} s, free memory ${Math.round(os.freemem() / 1048576)} MB</li>
<li><a href="/api/time">/api/time</a> &middot; <a href="/api/files">/api/files</a> &middot; POST /api/echo</li>
</ul>`);
    } else if (req.method === 'GET' && url.pathname === '/api/time') {
        json(res, 200, { now: new Date().toISOString(), uptime: process.uptime() });
    } else if (req.method === 'GET' && url.pathname === '/api/files') {
        const dir = __dirname;
        json(res, 200, fs.readdirSync(dir).map(name => ({ name, size: fs.statSync(path.join(dir, name)).size })));
    } else if (req.method === 'POST' && url.pathname === '/api/echo') {
        const chunks = [];
        req.on('data', chunk => chunks.push(chunk));
        req.on('end', () => {
            const body = Buffer.concat(chunks).toString();
            try { json(res, 200, { received: JSON.parse(body) }); }
            catch (e) { json(res, 400, { error: 'invalid JSON', body }); }
        });
    } else {
        json(res, 404, { error: 'not found', path: url.pathname });
    }
});

server.listen(port, () => {
    // Show the first non-loopback IPv4 address; the hostname only works where the name resolves.
    const local = Object.values(os.networkInterfaces()).flat().find(a => a.family === 'IPv4' && !a.internal);
    const address = local ? local.address : os.hostname();
    console.log(`node-lite server on http://${address}:${server.address().port}/  (Ctrl+C to stop)`);
});
