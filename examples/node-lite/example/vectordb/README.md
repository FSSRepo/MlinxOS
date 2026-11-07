# Vector database example

A small vector store plus a batch-embeddings script that uses `fetch` and the OpenAI API. Plain CommonJS, so it
runs on node-lite and on Node 18+.

```
export OPENAI_API_KEY=...                       # read from the environment, never written anywhere
node-lite embed-batch.js --out vectors.json     # embeds docs.json (20 short texts) in batches
node-lite search.js "how does a computer start up" --k 3
node-lite search.js --like space-mars --k 3     # neighbours of a stored item, no API call
```

| File | What it does |
|---|---|
| `vectordb.js` | `VectorDB`: `add`, `get`, `remove`, `search(vector, k, { filter })` (cosine, brute force), `save`/`load` as JSON |
| `openai.js` | `embedBatch(texts)` and `embedAll(texts, { batchSize })`: `POST /v1/embeddings`, answers re-ordered by `index`, retries on 429 and 5xx with backoff |
| `embed-batch.js` | Reads `--input` (array of strings or `{ id, text, ...metadata }`), embeds it in batches of `--batch` (default 64) and writes the database |
| `search.js` | Embeds the query with the model stored in the database and prints the closest items |

Options: `--model` (default `text-embedding-3-small`, or `OPENAI_EMBEDDING_MODEL`), `--dims` (shorter vectors for
the `text-embedding-3-*` models), `OPENAI_BASE_URL` for a compatible server.

## Searching node-lite's own documentation

`index-docs.js` is the practical use: it splits the README files of node-lite into one chunk per section
(`chunker.js`: by heading, long sections by paragraph, code blocks kept whole), embeds them in batches with the section
path in front of each text, and stores them in `docs.vec.json`. `search.js` then answers questions with the sections
that match, in any language:

```
node-lite index-docs.js --dry-run                       # prints the 16 chunks, no API call
node-lite index-docs.js                                 # embeds them (needs OPENAI_API_KEY) -> docs.vec.json
node-lite search.js "how do I cancel a fetch?" --db docs.vec.json --k 3
node-lite search.js "cómo valida los certificados https" --db docs.vec.json --k 3
```

Real run (Node 24, `text-embedding-3-small`):

```
> node index-docs.js
2 files -> 17 chunks (190-1432 characters)
embedded 17/17
17 chunks, 1536 dimensions (text-embedding-3-small), 2923 tokens, 1.5 s -> docs.vec.json

> node search.js "how do I cancel a fetch?" --db docs.vec.json --k 3
query: how do I cancel a fetch?
0.433  node-lite > fetch  (node-lite/README.md)
       ```js const res = await fetch('https://www.google.com', { headers: { 'accept-language': 'en' } }); ...
0.347  node-lite > fetch  (node-lite/README.md)
       HTTPS uses a statically linked copy of mbedTLS 3.6.4 (`examples/external/mbedtls`, TLS 1.2/1.3) and Mozilla's root ...
0.216  Vector database example > Searching node-lite's own documentation  (vectordb/README.md)
       `index-docs.js` is the practical use: it splits the README files of node-lite into one chunk per section ...
```

The question never says "abort", yet the best hit is the `fetch` section that documents `AbortController` and the
`timeout` option, and the second one is its continuation. The text printed under each hit is only the start of the
chunk, so the part that answers the question (here, `AbortController`) can be further down inside it. The third hit
is weaker (0.216) and only matches because this README talks about searching these same docs; a minimum score of
about 0.25 would drop it.

`--files a.md,b.md` indexes other Markdown files, so the same two commands work for any documentation folder. Run
`index-docs.js` again after editing the docs: it rebuilds the whole file (a few hundred tokens for these READMEs).

## A real run

Output of `node embed-batch.js --out vectors.json --batch 8` with Node 24 against the OpenAI API
(`text-embedding-3-small`, 20 texts, 3 requests):

```
embedded 8/20
embedded 16/20
embedded 20/20
20 vectors of 1536 dimensions (text-embedding-3-small), 417 tokens, 2.3 s -> vectors.json
```

`vectors.json` is 364 KB (JSON, 7 significant digits per component). Neighbours of stored items with
`search.js --like <id> --k 3` (score = cosine similarity, no API call needed):

```
items like linux-boot: The Linux kernel mounts an initramfs, starts init and then launches the system services...
0.506  linux-grub  [linux]  GRUB is the bootloader that loads the kernel image and the initial ramdisk from the disk.
0.282  linux-udev  [linux]  udev creates the device nodes in /dev and tags input devices so applications can find...
0.193  linux-wifi  [linux]  wpa_supplicant authenticates against the access point and udhcpc then requests...

items like space-moon: La Luna orbita la Tierra cada 27 días y su gravedad provoca las mareas de los océanos.
0.347  space-iss   [space]  The International Space Station circles the Earth every ninety minutes with astronauts...
0.237  space-mars  [space]  Mars is a cold desert planet with the tallest volcano in the solar system and two small moons.
0.169  music-piano [music]  El piano tiene ochenta y ocho teclas y un martillo golpea cada cuerda cuando se pulsa...

items like money-bank: A savings account pays a small interest while the bank lends your deposits to other customers.
0.327  money-stocks [finance]  Buying stocks means owning a small part of a company, and its price rises and falls with the market.
0.166  sport-tennis [sports]   Tennis players serve the ball over the net and win points when the opponent cannot return it.
0.120  cook-soup     [cooking]  A good vegetable soup simmers carrots, leeks and potatoes in broth with a little pepper.
```

What to read from it:

- The model groups by meaning, not by words: the Spanish text about the Moon lands next to the English ones about the
  space station and Mars, and `cook-pasta` finds `cook-soup`, `cook-bread` and the Spanish `cook-tortilla`.
- Across the 20 items the nearest neighbour always had the same topic (20/20), and 72 % of all top-3 neighbours did.
  The misses have low scores (0.12 to 0.17), so a minimum score (for example 0.25 for this model) is a reasonable cut-off.
- Scores between unrelated texts stay around 0.1 to 0.2, and related ones start around 0.25. Those numbers depend
  on the model and the corpus; pick the threshold from your own data.
- Cost: 417 tokens for the whole corpus. A key that has expired fails at once with
  `OpenAI API 401 ... expired_secret_key` and nothing is written.

## Limits

Brute force is fine up to some tens of thousands of vectors; beyond that it needs an index. Test (no key needed,
it talks to a local stand-in for the API): `node-lite tests/vectordb_test.js`.
