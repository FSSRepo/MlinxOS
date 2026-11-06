// DNS + fetch:  node-lite example/fetch.js [url]     (default https://www.google.com)
const dns = require('dns').promises;

const url = process.argv[2] || 'https://www.google.com';

(async () => {
    const host = new URL(url).hostname;
    const { address } = await dns.lookup(host);
    console.log(`dns.lookup   ${host} -> ${address}`);
    console.log(`dns.resolve4 ${host} -> ${(await dns.resolve4(host)).join(', ')}`);

    const response = await fetch(url, { headers: { 'accept-language': 'en' } });
    console.log(`fetch        ${response.status} ${response.statusText}  ${response.url}${response.redirected ? '  (redirected)' : ''}`);
    console.log(`content-type ${response.headers.get('content-type')}`);
    const text = await response.text();
    console.log(`body         ${text.length} chars: ${text.slice(0, 120).replace(/\s+/g, ' ')}...`);
})().catch(error => {
    console.error('failed:', error.message + (error.cause ? ' -> ' + error.cause.message : ''));
    process.exit(1);
});
