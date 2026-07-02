const fs = require("fs");

const file = process.argv[2];
if (!file) {
  console.error("usage: node strip-empty-tls-directory.js <pe-file>");
  process.exit(2);
}

const data = fs.readFileSync(file);

function u16(offset) {
  return data.readUInt16LE(offset);
}

function u32(offset) {
  return data.readUInt32LE(offset);
}

function u64(offset) {
  return data.readBigUInt64LE(offset);
}

const peOffset = u32(0x3c);
if (data.toString("ascii", peOffset, peOffset + 4) !== "PE\u0000\u0000") {
  throw new Error("not a PE file");
}

const optionalHeaderOffset = peOffset + 24;
const optionalMagic = u16(optionalHeaderOffset);
if (optionalMagic !== 0x20b) {
  throw new Error("expected PE32+ image");
}

const imageBase = u64(optionalHeaderOffset + 24);
const numberOfSections = u16(peOffset + 6);
const sectionTableOffset = optionalHeaderOffset + u16(peOffset + 20);
const dataDirectoryOffset = optionalHeaderOffset + 112;
const tlsDirectoryEntryOffset = dataDirectoryOffset + 9 * 8;
const tlsRva = u32(tlsDirectoryEntryOffset);
const tlsSize = u32(tlsDirectoryEntryOffset + 4);

function rvaToOffset(rva) {
  for (let i = 0; i < numberOfSections; ++i) {
    const sectionOffset = sectionTableOffset + i * 40;
    const virtualSize = u32(sectionOffset + 8);
    const virtualAddress = u32(sectionOffset + 12);
    const rawSize = u32(sectionOffset + 16);
    const rawPointer = u32(sectionOffset + 20);
    const span = Math.max(virtualSize, rawSize);
    if (rva >= virtualAddress && rva < virtualAddress + span) {
      return rawPointer + (rva - virtualAddress);
    }
  }
  return null;
}

if (tlsRva === 0 || tlsSize === 0) {
  console.log("TLS directory already absent");
  process.exit(0);
}

const tlsOffset = rvaToOffset(tlsRva);
if (tlsOffset == null || tlsSize < 0x28) {
  throw new Error("TLS directory could not be parsed");
}

const callbacksVa = u64(tlsOffset + 24);
if (callbacksVa !== 0n) {
  const callbacksRva = Number(callbacksVa - imageBase);
  const callbacksOffset = rvaToOffset(callbacksRva);
  if (callbacksOffset == null) {
    throw new Error("TLS callback table could not be parsed");
  }

  const firstCallback = u64(callbacksOffset);
  if (firstCallback !== 0n) {
    throw new Error(`refusing to strip non-empty TLS callback table: 0x${firstCallback.toString(16)}`);
  }
}

data.writeUInt32LE(0, tlsDirectoryEntryOffset);
data.writeUInt32LE(0, tlsDirectoryEntryOffset + 4);
fs.writeFileSync(file, data);
console.log(`stripped empty TLS directory from ${file}`);
