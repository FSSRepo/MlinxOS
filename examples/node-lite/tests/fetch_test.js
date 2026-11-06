// fetch / dns against a local server (no internet needed); run with: node-lite tests/fetch_test.js
const http = require('node:http');
const dns = require('node:dns');
const assert = require('node:assert');

let failures = 0;
async function test(name, fn) {
    try { await fn(); console.log('ok   ' + name); }
    catch (e) { failures++; console.log('FAIL ' + name + ': ' + (e && e.message)); }
}

const server = http.createServer((req, res) => {
    const chunks = [];
    req.on('data', c => chunks.push(c));
    req.on('end', () => {
        const body = Buffer.concat(chunks).toString();
        if (req.url === '/json') {
            res.writeHead(200, { 'Content-Type': 'application/json', 'X-Multi': 'a' });
            res.end(JSON.stringify({ method: req.method, ua: req.headers['user-agent'], body }));
        } else if (req.url === '/old') {
            res.writeHead(302, { Location: '/new?x=1' }); res.end();
        } else if (req.url === '/new?x=1') {
            res.end('arrived');
        } else if (req.url === '/post-redirect') {
            res.writeHead(303, { Location: '/json' }); res.end();
        } else if (req.url === '/slow') {
            setTimeout(() => res.end('late'), 3000);
        } else if (req.url === '/big') {
            res.end('x'.repeat(300000));
        } else if (req.url === '/head') {
            res.setHeader('X-Length', '5'); res.end('hello');
        } else {
            res.writeHead(404); res.end('nope');
        }
    });
});

server.listen(0, '127.0.0.1', async () => {
    const base = 'http://127.0.0.1:' + server.address().port;

    await test('GET json', async () => {
        const r = await fetch(base + '/json');
        assert.strictEqual(r.status, 200);
        assert.ok(r.ok);
        assert.strictEqual(r.headers.get('Content-Type'), 'application/json');
        const data = await r.json();
        assert.strictEqual(data.method, 'GET');
        assert.ok(/^node-lite\//.test(data.ua), data.ua);
    });
    await test('POST string body', async () => {
        const data = await (await fetch(base + '/json', { method: 'POST', body: 'hello' })).json();
        assert.strictEqual(data.method, 'POST');
        assert.strictEqual(data.body, 'hello');
    });
    await test('POST JSON body with headers', async () => {
        const r = await fetch(base + '/json', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ a: 1 }) });
        assert.deepStrictEqual(JSON.parse((await r.json()).body), { a: 1 });
    });
    await test('404 is not ok but resolves', async () => {
        const r = await fetch(base + '/missing');
        assert.strictEqual(r.status, 404);
        assert.strictEqual(r.ok, false);
        assert.strictEqual(await r.text(), 'nope');
    });
    await test('redirect is followed', async () => {
        const r = await fetch(base + '/old');
        assert.strictEqual(await r.text(), 'arrived');
        assert.ok(r.redirected);
        assert.ok(r.url.endsWith('/new?x=1'), r.url);
    });
    await test('redirect: manual', async () => {
        const r = await fetch(base + '/old', { redirect: 'manual' });
        assert.strictEqual(r.status, 302);
        assert.strictEqual(r.headers.get('location'), '/new?x=1');
    });
    await test('303 turns POST into GET', async () => {
        const data = await (await fetch(base + '/post-redirect', { method: 'POST', body: 'x' })).json();
        assert.strictEqual(data.method, 'GET');
    });
    await test('300 KB body', async () => {
        assert.strictEqual((await (await fetch(base + '/big')).text()).length, 300000);
    });
    await test('HEAD has no body', async () => {
        const r = await fetch(base + '/head', { method: 'HEAD' });
        assert.strictEqual(r.headers.get('x-length'), '5');
        assert.strictEqual(await r.text(), '');
    });
    await test('body can be read once', async () => {
        const r = await fetch(base + '/json');
        await r.text();
        let threw = false;
        try { await r.text(); } catch (e) { threw = e instanceof TypeError; }
        assert.ok(threw);
    });
    await test('timeout option', async () => {
        let error;
        try { await fetch(base + '/slow', { timeout: 300 }); } catch (e) { error = e; }
        assert.strictEqual(error && error.name, 'TimeoutError');
    });
    await test('AbortController', async () => {
        const controller = new AbortController();
        setTimeout(() => controller.abort(), 100);
        let error;
        try { await fetch(base + '/slow', { signal: controller.signal }); } catch (e) { error = e; }
        assert.strictEqual(error && error.name, 'AbortError');
    });
    await test('connection refused -> fetch failed with cause', async () => {
        let error;
        try { await fetch('http://127.0.0.1:1/'); } catch (e) { error = e; }
        assert.ok(error instanceof TypeError);
        assert.strictEqual(error.message, 'fetch failed');
        assert.strictEqual(error.cause.code, 'ECONNREFUSED');
    });
    await test('unknown host -> ENOTFOUND', async () => {
        let error;
        try { await fetch('http://does-not-exist.invalid/'); } catch (e) { error = e; }
        assert.ok(error && error.cause && error.cause.code === 'ENOTFOUND', error && error.cause && error.cause.message);
    });
    await test('dns.promises.lookup localhost', async () => {
        const { address, family } = await dns.promises.lookup('localhost');
        assert.strictEqual(address, '127.0.0.1');
        assert.strictEqual(family, 4);
    });
    await test('dns.lookup callback + resolve4 of an IP', async () => {
        await new Promise((resolve, reject) => dns.lookup('127.0.0.1', (err, address) => err ? reject(err) : address === '127.0.0.1' ? resolve() : reject(new Error(address))));
    });
    await test('Headers is case-insensitive', async () => {
        const h = new Headers({ 'Content-Type': 'text/plain' });
        h.append('X-A', '1'); h.append('x-a', '2');
        assert.strictEqual(h.get('content-type'), 'text/plain');
        assert.strictEqual(h.get('X-A'), '1, 2');
        assert.ok(h.has('x-a'));
    });

    server.close();
    process.exit(failures ? 1 : 0);
});
