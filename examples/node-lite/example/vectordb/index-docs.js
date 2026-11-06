// Indexes node-lite's own documentation so it can be searched by meaning.
//
//   node-lite index-docs.js [--files a.md,b.md] [--out docs.vec.json] [--dry-run]
//   node-lite search.js "how do I cancel a fetch?" --db docs.vec.json --k 3
//
// By default it reads the README of node-lite and the README of this example. Needs OPENAI_API_KEY unless --dry-run,
// which only prints the chunks.
const fs = require('fs');
const path = require('path');
const { VectorDB } = require('./vectordb');
const { chunkMarkdown } = require('./chunker');
const { embedAll, settings } = require('./openai');

function parseArgs(argv) {
    const args = {};
    for (let i = 0; i < argv.length; i++) {
        if (!argv[i].startsWith('--')) throw new Error(`unexpected argument "${argv[i]}"`);
        const name = argv[i].slice(2);
        args[name] = name === 'dry-run' ? true : argv[++i];
    }
    return args;
}

async function main() {
    const args = parseArgs(process.argv.slice(2));
    const files = args.files ? args.files.split(',') : [
        path.join(__dirname, '..', '..', 'README.md'),
        path.join(__dirname, 'README.md'),
    ];
    const chunks = [];
    for (const file of files) {
        const source = path.basename(path.dirname(path.resolve(file))) + '/' + path.basename(file);
        chunks.push(...chunkMarkdown(fs.readFileSync(file, 'utf8'), source));
    }
    const sizes = chunks.map(c => c.text.length);
    console.log(`${files.length} files -> ${chunks.length} chunks (${Math.min(...sizes)}-${Math.max(...sizes)} characters)`);

    if (args['dry-run']) {
        for (const c of chunks) console.log(`  ${c.id}  [${c.text.length}]  ${c.title}`);
        return;
    }

    const out = args.out || 'docs.vec.json';
    const { model } = settings(args.model ? { model: args.model } : {});
    const started = Date.now();
    // the heading path travels with the text, so a chunk that says only "Limits:" still knows what it limits
    const { vectors, tokens } = await embedAll(chunks.map(c => `${c.source} > ${c.title}\n\n${c.text}`), {
        model,
        batchSize: Number(args.batch || 64),
        onBatch: (done, total) => console.log(`embedded ${done}/${total}`),
    });
    const db = new VectorDB({ model });
    chunks.forEach((c, i) => db.add(c.id, vectors[i], { title: c.title, source: c.source, text: c.text }));
    db.save(out);
    console.log(`${db.size} chunks, ${db.dimensions} dimensions (${model}), ${tokens} tokens, ${((Date.now() - started) / 1000).toFixed(1)} s -> ${out}`);
}

main().catch(error => {
    console.error('failed:', error.message + (error.cause ? ' -> ' + error.cause.message : ''));
    process.exit(1);
});
