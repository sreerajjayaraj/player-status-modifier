const fs = require('node:fs');
const assert = require('node:assert/strict');
const b = fs.readFileSync(process.argv[2]);
const pe = b.readUInt32LE(0x3c);
assert.equal(b.toString('ascii', pe, pe + 4), 'PE\0\0');
const optional = pe + 24;
assert.equal(b.readUInt16LE(optional), 0x20b);
assert.equal(b.readUInt32LE(optional + 112 + 9 * 8), 0,
  'ASI must be built without TLS dependencies; do not strip live TLS storage');
assert.equal(b.readUInt32LE(optional + 112 + 9 * 8 + 4), 0);
console.log('PASS: built ASI has no TLS directory; no binary stripping needed');
