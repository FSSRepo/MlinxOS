// A tiny in-memory vector database: cosine similarity by brute force, persisted as JSON.
// Plain CommonJS, so it runs on node-lite and on Node.
const fs = require('fs');

function normalized(values) {
    const v = Float32Array.from(values);
    let sum = 0;
    for (let i = 0; i < v.length; i++) sum += v[i] * v[i];
    const norm = Math.sqrt(sum) || 1;
    for (let i = 0; i < v.length; i++) v[i] /= norm;
    return v;
}

class VectorDB {
    constructor({ dimensions = 0, model = '' } = {}) {
        this.dimensions = dimensions;
        this.model = model;
        this.items = new Map(); // id -> { vector: Float32Array (unit length), metadata }
    }

    get size() { return this.items.size; }

    /* Adds or replaces a vector. The first vector fixes the dimensions of the database. */
    add(id, vector, metadata = {}) {
        if (!this.dimensions) this.dimensions = vector.length;
        if (vector.length !== this.dimensions) {
            throw new Error(`vector "${id}" has ${vector.length} dimensions, the database has ${this.dimensions}`);
        }
        this.items.set(id, { vector: normalized(vector), metadata });
    }

    has(id) { return this.items.has(id); }
    get(id) { return this.items.get(id); }
    remove(id) { return this.items.delete(id); }

    /* Nearest neighbours by cosine similarity: [{ id, score, metadata }], best first.
       `filter(metadata, id)` can exclude items. */
    search(query, k = 5, { filter } = {}) {
        if (query.length !== this.dimensions) {
            throw new Error(`query has ${query.length} dimensions, the database has ${this.dimensions}`);
        }
        const q = normalized(query);
        const results = [];
        for (const [id, item] of this.items) {
            if (filter && !filter(item.metadata, id)) continue;
            let dot = 0;
            for (let i = 0; i < q.length; i++) dot += q[i] * item.vector[i];
            results.push({ id, score: dot, metadata: item.metadata });
        }
        results.sort((a, b) => b.score - a.score);
        return results.slice(0, k);
    }

    save(file) {
        const items = [];
        for (const [id, item] of this.items) {
            items.push({ id, metadata: item.metadata, vector: Array.from(item.vector, x => Number(x.toPrecision(7))) });
        }
        fs.writeFileSync(file, JSON.stringify({ version: 1, model: this.model, dimensions: this.dimensions, items }));
    }

    static load(file) {
        const data = JSON.parse(fs.readFileSync(file, 'utf8'));
        const db = new VectorDB({ dimensions: data.dimensions, model: data.model });
        for (const item of data.items) db.add(item.id, item.vector, item.metadata);
        return db;
    }
}

module.exports = { VectorDB };
