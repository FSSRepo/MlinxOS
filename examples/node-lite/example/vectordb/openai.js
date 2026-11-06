// Embeddings through the OpenAI API with fetch. The key comes from the OPENAI_API_KEY environment variable
// and is only sent in the Authorization header.
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

function settings(overrides = {}) {
    return {
        apiKey: process.env.OPENAI_API_KEY,
        baseUrl: (process.env.OPENAI_BASE_URL || 'https://api.openai.com/v1').replace(/\/$/, ''),
        model: process.env.OPENAI_EMBEDDING_MODEL || 'text-embedding-3-small',
        dimensions: undefined,
        ...overrides,
    };
}

/* One request with up to ~2000 texts. Returns { vectors, tokens } in the order of `texts`.
   Rate limits (429) and server errors (5xx) are retried with exponential backoff. */
async function embedBatch(texts, options = {}) {
    const { apiKey, baseUrl, model, dimensions } = settings(options);
    if (!apiKey) throw new Error('OPENAI_API_KEY is not set');

    const body = JSON.stringify({ model, input: texts, encoding_format: 'float', ...(dimensions ? { dimensions } : {}) });
    for (let attempt = 0; ; attempt++) {
        const response = await fetch(`${baseUrl}/embeddings`, {
            method: 'POST',
            headers: { authorization: `Bearer ${apiKey}`, 'content-type': 'application/json' },
            body,
        });
        if (response.ok) {
            const json = await response.json();
            const ordered = json.data.slice().sort((a, b) => a.index - b.index);
            return { vectors: ordered.map(d => d.embedding), tokens: json.usage ? json.usage.total_tokens : 0 };
        }
        const detail = await response.text();
        const retryable = response.status === 429 || response.status >= 500;
        if (!retryable || attempt >= 5) throw new Error(`OpenAI API ${response.status}: ${detail.slice(0, 300)}`);
        const wait = Number(response.headers.get('retry-after')) * 1000 || Math.min(30000, 1000 * 2 ** attempt);
        console.error(`OpenAI API ${response.status}, retrying in ${Math.round(wait / 1000)} s (attempt ${attempt + 1}/5)`);
        await sleep(wait);
    }
}

/* Any number of texts, sent in batches. onBatch(done, total) reports progress. */
async function embedAll(texts, { batchSize = 64, onBatch, ...options } = {}) {
    const vectors = [];
    let tokens = 0;
    for (let start = 0; start < texts.length; start += batchSize) {
        const batch = await embedBatch(texts.slice(start, start + batchSize), options);
        vectors.push(...batch.vectors);
        tokens += batch.tokens;
        if (onBatch) onBatch(Math.min(start + batchSize, texts.length), texts.length);
    }
    return { vectors, tokens };
}

module.exports = { embedBatch, embedAll, settings };
