// Creates the embeddings of a set of texts in batches and stores them in a vector database file.
//
//   node-lite embed-batch.js [--input docs.json] [--out vectors.json] [--batch 64] [--model text-embedding-3-small] [--dims 256]
//
// --input is a JSON array of strings or of { id, text, ...metadata }. Needs OPENAI_API_KEY in the environment.
const path = require('path');
const { VectorDB } = require('./vectordb');
const { embedAll, settings } = require('./openai');

function parseArgs(argv) {
    const args = {};
    for (let i = 0; i < argv.length; i += 2) {
        if (!argv[i].startsWith('--') || argv[i + 1] === undefined) throw new Error(`unexpected argument "${argv[i]}"`);
        args[argv[i].slice(2)] = argv[i + 1];
    }
    return args;
}

async function main() {
    const args = parseArgs(process.argv.slice(2));
    const input = args.input || path.join(__dirname, 'docs.json');
    const out = args.out || 'vectors.json';
    const options = { model: args.model, dimensions: args.dims ? Number(args.dims) : undefined };
    if (!options.model) delete options.model;
    const { model } = settings(options);

    const docs = JSON.parse(require('fs').readFileSync(input, 'utf8')).map((doc, i) =>
        typeof doc === 'string' ? { id: `item-${i + 1}`, text: doc } : doc);
    if (!docs.length) throw new Error(`${input} has no texts`);

    const started = Date.now();
    const { vectors, tokens } = await embedAll(docs.map(d => d.text), {
        ...options,
        batchSize: Number(args.batch || 64),
        onBatch: (done, total) => console.log(`embedded ${done}/${total}`),
    });

    const db = new VectorDB({ model });
    docs.forEach((doc, i) => {
        const { id, ...metadata } = doc;
        db.add(id, vectors[i], metadata);
    });
    db.save(out);
    console.log(`${db.size} vectors of ${db.dimensions} dimensions (${model}), ${tokens} tokens, ${((Date.now() - started) / 1000).toFixed(1)} s -> ${out}`);
}

main().catch(error => {
    console.error('failed:', error.message + (error.cause ? ' -> ' + error.cause.message : ''));
    process.exit(1);
});
