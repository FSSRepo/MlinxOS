// Splits Markdown into chunks for embeddings: one per section (by heading), long sections split by paragraphs.
// Headings inside code fences are not headings, and a fenced block is never split at its blank lines.

/* chunkMarkdown(markdown, source) -> [{ id, source, title, text }]; title is the heading path ("fetch > Limits"). */
function chunkMarkdown(markdown, source, { maxChars = 1200, minChars = 40 } = {}) {
    const sections = [];
    const path = [];
    let current = { title: '', lines: [] };
    let fenced = false;
    for (const line of markdown.split(/\r?\n/)) {
        if (/^\s*```/.test(line)) fenced = !fenced;
        const heading = !fenced && /^(#{1,4})\s+(.*\S)\s*$/.exec(line);
        if (heading) {
            sections.push(current);
            path.length = heading[1].length - 1;
            path[heading[1].length - 1] = heading[2].replace(/`/g, '');
            current = { title: path.filter(Boolean).join(' > '), lines: [] };
        } else {
            current.lines.push(line);
        }
    }
    sections.push(current);

    const chunks = [];
    const used = new Map();
    for (const section of sections) {
        const text = section.lines.join('\n').trim();
        if (text.length < minChars) continue;
        const slug = (section.title || 'intro').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 'section';
        const pieces = packBlocks(blocksOf(section.lines), maxChars);
        for (const piece of pieces) {
            if (piece.length < minChars) continue;
            const base = `${source}#${slug}`;
            const n = (used.get(base) || 0) + 1;
            used.set(base, n);
            chunks.push({ id: n === 1 ? base : `${base}-${n}`, source, title: section.title || 'Introduction', text: piece });
        }
    }
    return chunks;
}

/* Paragraphs (separated by blank lines), keeping fenced code together. */
function blocksOf(lines) {
    const blocks = [];
    let block = [];
    let fenced = false;
    for (const line of lines) {
        if (/^\s*```/.test(line)) fenced = !fenced;
        if (!fenced && line.trim() === '' && !/^\s*```/.test(line)) {
            if (block.length) blocks.push(block.join('\n'));
            block = [];
        } else {
            block.push(line);
        }
    }
    if (block.length) blocks.push(block.join('\n'));
    return blocks;
}

/* Joins consecutive blocks up to maxChars; a block that is longer on its own is cut at line boundaries. */
function packBlocks(blocks, maxChars) {
    const pieces = [];
    let piece = '';
    const flush = () => { if (piece.trim()) pieces.push(piece.trim()); piece = ''; };
    for (const block of blocks) {
        if (block.length > maxChars * 2) {
            flush();
            for (const line of block.split('\n')) {
                if (piece.length + line.length + 1 > maxChars) flush();
                piece += line + '\n';
            }
            flush();
        } else if (piece.length + block.length + 2 > maxChars) {
            flush();
            piece = block;
        } else {
            piece += (piece ? '\n\n' : '') + block;
        }
    }
    flush();
    return pieces;
}

module.exports = { chunkMarkdown };
