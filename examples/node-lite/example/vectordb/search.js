// Semantic search in a vector database file.
//
//   node-lite search.js "text to look for" [--db vectors.json] [--k 5]     (embeds the text with the OpenAI API)
//   node-lite search.js --like doc-01 [--db vectors.json] [--k 5]           (neighbours of a stored item, offline)
const { VectorDB } = require('./vectordb');
const { embedBatch } = require('./openai');

async function main() {
    const argv = process.argv.slice(2);
    const args = {};
    let query = null;
    for (let i = 0; i < argv.length; i++) {
        if (argv[i].startsWith('--')) args[argv[i].slice(2)] = argv[++i];
        else query = argv[i];
    }
    if (!query && !args.like) throw new Error('usage: search.js "text" | --like <id>  [--db vectors.json] [--k 5]');

    const db = VectorDB.load(args.db || 'vectors.json');
    const k = Number(args.k || 5);
    let results;
    if (args.like) {
        const item = db.get(args.like);
        if (!item) throw new Error(`no item with id "${args.like}"`);
        console.log(`items like ${args.like}: ${item.metadata.text}`);
        results = db.search(item.vector, k + 1).filter(r => r.id !== args.like).slice(0, k);
    } else {
        const { vectors } = await embedBatch([query], { model: db.model, dimensions: db.dimensions });
        console.log(`query: ${query}`);
        results = db.search(vectors[0], k);
    }
    for (const r of results) {
        const m = r.metadata;
        if (m.title) { // documentation chunks: section, file and the start of the text
            console.log(`${r.score.toFixed(3)}  ${m.title}  (${m.source})\n       ${m.text.replace(/\s+/g, ' ').slice(0, 170)}`);
        } else {
            console.log(`${r.score.toFixed(3)}  ${r.id}  [${m.topic || '-'}]  ${m.text}`);
        }
    }
}

main().catch(error => {
    console.error('failed:', error.message + (error.cause ? ' -> ' + error.cause.message : ''));
    process.exit(1);
});
