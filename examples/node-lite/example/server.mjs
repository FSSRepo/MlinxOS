// Same idea with ES module syntax: node-lite example/server.mjs [port]
import http from 'node:http';
import { readFileSync } from 'fs';
import { join, dirname } from 'node:path';
import { EventEmitter } from 'events';

const port = Number(process.argv[2] || 8081);
const bus = new EventEmitter();
let hits = 0;
bus.on('hit', () => { hits++; });

const server = http.createServer((req, res) => {
    bus.emit('hit');
    if (req.url === '/readme') {
        res.setHeader('Content-Type', 'text/plain; charset=utf-8');
        res.end(readFileSync(join(dirname(process.argv[1]), 'lib', 'greeting.js'), 'utf8'));
    } else {
        res.setHeader('Content-Type', 'text/plain; charset=utf-8');
        res.end(`ES module server, request number ${hits}\n`);
    }
});
server.listen(port, () => console.log(`ES module server on port ${server.address().port}`));
