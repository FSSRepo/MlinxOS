// Vector database + batch embeddings against a local stand-in for the OpenAI embeddings endpoint.
// Runs on node-lite and on Node:  node-lite tests/vectordb_test.js
const http = require('node:http');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const assert = require('node:assert');

const dir = path.join(__dirname, '..', 'example', 'vectordb');
const docs = JSON.parse(fs.readFileSync(path.join(dir, 'docs.json'), 'utf8'));
const { VectorDB } = require(path.join(dir, 'vectordb'));

// Stand-in embeddings: hashed bag of words, so texts that share words end up close together.
const DIMS = 256;
function fakeEmbedding(text) {
    const v = new Array(DIMS).fill(0);
    for (const word of text.toLowerCase().match(/[a-záéíóúñ]{3,}/g) || []) {
        let h = 0;
        for (const ch of word) h = (h * 31 + ch.charCodeAt(0)) % DIMS;
        v[h] += 1;
    }
    return v;
}

const requests = [];
let rateLimited = false;
const server = http.createServer((req, res) => {
    const chunks = [];
    req.on('data', c => chunks.push(c));
    req.on('end', () => {
        const body = JSON.parse(Buffer.concat(chunks).toString() || '{}');
        requests.push({ url: req.url, auth: req.headers.authorization, body });
        if (req.headers.authorization !== 'Bearer test-key') {
            res.writeHead(401, { 'content-type': 'application/json' });
            return res.end(JSON.stringify({ error: { message: 'bad key' } }));
        }
        if (!rateLimited) { // the first call is rate limited once
            rateLimited = true;
            res.writeHead(429, { 'content-type': 'application/json', 'retry-after': '1' });
            return res.end(JSON.stringify({ error: { message: 'slow down' } }));
        }
        // answer out of order, like the real API is allowed to, with the index of every input
        const data = body.input.map((text, index) => ({ object: 'embedding', index, embedding: fakeEmbedding(text) })).reverse();
        res.writeHead(200, { 'content-type': 'application/json' });
        res.end(JSON.stringify({ object: 'list', data, model: body.model, usage: { total_tokens: body.input.length * 10 } }));
    });
});

let failures = 0;
async function test(name, fn) {
    try { await fn(); console.log('ok   ' + name); }
    catch (e) { failures++; console.log('FAIL ' + name + ': ' + (e && e.message)); }
}

server.listen(0, '127.0.0.1', async () => {
    process.env.OPENAI_BASE_URL = `http://127.0.0.1:${server.address().port}/v1`;
    process.env.OPENAI_API_KEY = 'test-key';
    const { embedAll, embedBatch } = require(path.join(dir, 'openai'));

    await test('VectorDB: cosine search, replace, remove, filter', async () => {
        const db = new VectorDB();
        db.add('x', [1, 0, 0]); db.add('y', [0, 1, 0], { tag: 'b' }); db.add('xy', [1, 1, 0], { tag: 'b' });
        assert.strictEqual(db.dimensions, 3);
        const [first, second] = db.search([2, 0.1, 0], 2);
        assert.strictEqual(first.id, 'x');
        assert.strictEqual(second.id, 'xy');
        assert.ok(Math.abs(db.search([5, 0, 0], 1)[0].score - 1) < 1e-6, 'scores are cosines');
        assert.deepStrictEqual(db.search([1, 0, 0], 5, { filter: m => m.tag === 'b' }).map(r => r.id), ['xy', 'y']);
        db.add('x', [0, 0, 1]);
        assert.strictEqual(db.size, 3);
        assert.strictEqual(db.search([0, 0, 1], 1)[0].id, 'x');
        assert.ok(db.remove('y') && !db.has('y'));
        assert.throws(() => db.add('bad', [1, 2]), /dimensions/);
    });

    await test('VectorDB: save and load keep the same results', async () => {
        const file = path.join(os.tmpdir(), `vectordb-test-${process.pid}.json`);
        const db = new VectorDB({ model: 'm' });
        db.add('a', [0.3, 0.1, 0.9], { text: 'alpha' }); db.add('b', [0.9, 0.2, 0.1], { text: 'beta' });
        db.save(file);
        const loaded = VectorDB.load(file);
        fs.unlinkSync(file);
        assert.strictEqual(loaded.model, 'm');
        assert.strictEqual(loaded.search([0.3, 0.1, 0.9], 1)[0].id, 'a');
        assert.strictEqual(loaded.get('b').metadata.text, 'beta');
    });

    let vectors;
    await test('embedAll: batches, order and retry after a 429', async () => {
        const texts = docs.map(d => d.text);
        const progress = [];
        const result = await embedAll(texts, { batchSize: 8, onBatch: (done, total) => progress.push(`${done}/${total}`) });
        vectors = result.vectors;
        assert.strictEqual(vectors.length, 20);
        assert.deepStrictEqual(progress, ['8/20', '16/20', '20/20']);
        assert.strictEqual(result.tokens, 200);
        assert.strictEqual(requests.length, 4, 'three batches plus one retry');
        assert.ok(requests.every(r => r.url === '/v1/embeddings' && r.auth === 'Bearer test-key'));
        assert.strictEqual(requests[0].body.model, 'text-embedding-3-small');
        assert.strictEqual(requests[0].body.input.length, 8);
        for (let i = 0; i < texts.length; i++) assert.deepStrictEqual(vectors[i], fakeEmbedding(texts[i]), 'doc ' + i + ' is in order');
    });

    await test('semantic search over the embedded corpus', async () => {
        const db = new VectorDB({ model: 'text-embedding-3-small' });
        docs.forEach(({ id, ...metadata }, i) => db.add(id, vectors[i], metadata));
        assert.strictEqual(db.size, 20);
        const ask = async text => db.search((await embedBatch([text])).vectors[0], 3).map(r => r.id);
        assert.ok((await ask('linux kernel boot with grub')).includes('linux-boot'));
        assert.ok((await ask('planet mars volcano')).includes('space-mars'));
        assert.ok((await ask('tomate pasta basil sauce'))[0] === 'cook-pasta');
        const like = db.search(db.get('sport-tennis').vector, 3).filter(r => r.id !== 'sport-tennis');
        assert.ok(like.length === 2);
    });

    await test('chunker: headings, fences and long sections', async () => {
        const { chunkMarkdown } = require(path.join(dir, 'chunker'));
        const md = ['# Title', 'Intro text that is long enough to be kept as a chunk of its own.', '',
            '## Code', '```sh', '# not a heading', 'run --it', '', 'still the same block', '```', '',
            '## Long', ...Array.from({ length: 6 }, (_, i) => `Paragraph ${i} ` + 'word '.repeat(40) + '\n')].join('\n');
        const chunks = chunkMarkdown(md, 'x.md', { maxChars: 500 });
        assert.deepStrictEqual(chunks.filter(c => c.title === 'Title > Code').length, 1);
        const code = chunks.find(c => c.title === 'Title > Code');
        assert.ok(code.text.includes('# not a heading') && code.text.includes('still the same block'));
        const long = chunks.filter(c => c.title === 'Title > Long');
        assert.ok(long.length >= 3 && long.every(c => c.text.length <= 520), 'long section is split: ' + long.length);
        assert.strictEqual(new Set(chunks.map(c => c.id)).size, chunks.length, 'ids are unique');
    });

    await test('documentation search: node-lite README chunks answer questions', async () => {
        const { chunkMarkdown } = require(path.join(dir, 'chunker'));
        const readme = path.join(__dirname, '..', 'README.md');
        const chunks = chunkMarkdown(fs.readFileSync(readme, 'utf8'), 'node-lite/README.md');
        assert.ok(chunks.length >= 8, 'chunks: ' + chunks.length);
        const { vectors } = await embedAll(chunks.map(c => `${c.source} > ${c.title}\n\n${c.text}`), { batchSize: 5 });
        const db = new VectorDB();
        chunks.forEach((c, i) => db.add(c.id, vectors[i], { title: c.title, text: c.text }));
        const ask = async q => db.search((await embedBatch([q])).vectors[0], 2).map(r => r.metadata.title);
        assert.ok((await ask('abort a fetch request with a timeout and redirects')).some(t => /fetch/.test(t)));
        assert.ok((await ask('getaddrinfo resolv.conf dns lookup')).some(t => /dns/.test(t)));
        assert.ok((await ask('chunked request bodies and keep-alive limits of the server')).some(t => /http/.test(t)));
    });

    await test('options reach the API: model and dimensions', async () => {
        requests.length = 0;
        await embedBatch(['hello'], { model: 'text-embedding-3-large', dimensions: 256 });
        assert.strictEqual(requests[0].body.model, 'text-embedding-3-large');
        assert.strictEqual(requests[0].body.dimensions, 256);
    });

    await test('an invalid key is reported with the API message', async () => {
        const saved = process.env.OPENAI_API_KEY;
        process.env.OPENAI_API_KEY = 'wrong';
        let error;
        try { await embedBatch(['hello']); } catch (e) { error = e; }
        process.env.OPENAI_API_KEY = saved;
        assert.ok(error && /401/.test(error.message) && /bad key/.test(error.message), error && error.message);
    });

    await test('a missing key fails before any request', async () => {
        const saved = process.env.OPENAI_API_KEY;
        delete process.env.OPENAI_API_KEY;
        const before = requests.length;
        let error;
        try { await embedBatch(['hello']); } catch (e) { error = e; }
        process.env.OPENAI_API_KEY = saved;
        assert.ok(error && /OPENAI_API_KEY is not set/.test(error.message));
        assert.strictEqual(requests.length, before);
    });

    server.close();
    process.exit(failures ? 1 : 0);
});
