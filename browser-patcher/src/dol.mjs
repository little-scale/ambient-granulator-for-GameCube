const DOL_HEADER_SIZE = 0x100;
const DOL_TEXT_SECTION_COUNT = 7;
const DOL_DATA_SECTION_COUNT = 11;
const DOL_TEXT_OFFSETS = 0x00;
const DOL_DATA_OFFSETS = 0x1c;
const DOL_TEXT_ADDRESSES = 0x48;
const DOL_DATA_ADDRESSES = 0x64;
const DOL_TEXT_SIZES = 0x90;
const DOL_DATA_SIZES = 0xac;
const DOL_ENTRY_POINT = 0xe0;
const BANK_DATA_OFFSET = 64 + 64 * 64;
const BANK_MAGIC_BYTES = new TextEncoder().encode("NDSGRN01");

function bytesOf(input) {
  return input instanceof Uint8Array ? input : new Uint8Array(input);
}

function readU32Be(bytes, offset) {
  if (offset < 0 || offset + 4 > bytes.length)
    throw new Error("The DOL header is truncated.");
  return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
    .getUint32(offset, false);
}

function readU32Le(bytes, offset) {
  if (offset < 0 || offset + 4 > bytes.length)
    throw new Error("The embedded sample-bank header is truncated.");
  return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
    .getUint32(offset, true);
}

function writeU32Le(bytes, offset, value) {
  new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
    .setUint32(offset, value >>> 0, true);
}

function hasMagic(bytes, offset) {
  if (offset < 0 || offset + BANK_MAGIC_BYTES.length > bytes.length)
    return false;
  for (let index = 0; index < BANK_MAGIC_BYTES.length; index += 1) {
    if (bytes[offset + index] !== BANK_MAGIC_BYTES[index]) return false;
  }
  return true;
}

function collectSections(bytes, offsetTable, addressTable, sizeTable,
                         count, type) {
  const sections = [];
  for (let index = 0; index < count; index += 1) {
    const fileOffset = readU32Be(bytes, offsetTable + index * 4);
    const address = readU32Be(bytes, addressTable + index * 4);
    const size = readU32Be(bytes, sizeTable + index * 4);
    if (size === 0) continue;
    const end = fileOffset + size;
    if (fileOffset < DOL_HEADER_SIZE || end < fileOffset
        || end > bytes.length || address < 0x80000000
        || address + size > 0x81800000) {
      throw new Error(`The DOL ${type} section table is invalid.`);
    }
    sections.push({ fileOffset, end, address, size, type, index });
  }
  return sections;
}

function dolSections(bytes) {
  if (bytes.length < DOL_HEADER_SIZE)
    throw new Error("This file is too small to be a GameCube DOL.");
  const entryPoint = readU32Be(bytes, DOL_ENTRY_POINT);
  if (entryPoint < 0x80000000 || entryPoint >= 0x81800000)
    throw new Error("This is not a supported GameCube DOL executable.");
  const text = collectSections(bytes, DOL_TEXT_OFFSETS,
    DOL_TEXT_ADDRESSES, DOL_TEXT_SIZES, DOL_TEXT_SECTION_COUNT, "text");
  const data = collectSections(bytes, DOL_DATA_OFFSETS,
    DOL_DATA_ADDRESSES, DOL_DATA_SIZES, DOL_DATA_SECTION_COUNT, "data");
  if (!text.length || !data.length)
    throw new Error("The DOL has no usable text or data sections.");
  return { text, data, entryPoint };
}

function validBankCandidate(bytes, offset, sectionEnd) {
  if (!hasMagic(bytes, offset) || offset + BANK_DATA_OFFSET > sectionEnd)
    return null;
  const version = readU32Le(bytes, offset + 8);
  const capacity = readU32Le(bytes, offset + 12);
  const count = readU32Le(bytes, offset + 16);
  const used = readU32Le(bytes, offset + 20);
  const sampleRate = readU32Le(bytes, offset + 24);
  if (version !== 1 || capacity < BANK_DATA_OFFSET
      || offset + capacity > sectionEnd || used < BANK_DATA_OFFSET
      || used > capacity || count < 1 || count > 64
      || (sampleRate !== 16384 && sampleRate !== 48000)
      || readU32Le(bytes, offset + 28) !== 64
      || readU32Le(bytes, offset + 32) !== 64
      || readU32Le(bytes, offset + 36) !== BANK_DATA_OFFSET)
    return null;
  return { bankOffset: offset, bankCapacity: capacity, bankUsed: used };
}

function locateDolBank(input) {
  const bytes = bytesOf(input);
  const sections = dolSections(bytes);
  const candidates = [];
  for (const section of sections.data) {
    for (let offset = section.fileOffset;
        offset + BANK_MAGIC_BYTES.length <= section.end; offset += 1) {
      if (bytes[offset] !== BANK_MAGIC_BYTES[0]) continue;
      const candidate = validBankCandidate(bytes, offset, section.end);
      if (candidate) candidates.push({ ...candidate, section });
    }
  }
  if (candidates.length === 0)
    throw new Error("No embedded NDSGRN01 sample bank was found in this DOL.");
  if (candidates.length > 1)
    throw new Error("The DOL contains multiple embedded sample banks and cannot be patched safely.");
  return { bytes, entryPoint: sections.entryPoint, ...candidates[0] };
}

function replacementUsedLength(bank) {
  if (!hasMagic(bank, 0) || bank.length < BANK_DATA_OFFSET)
    throw new Error("The replacement bank is not a complete NDSGRN01 bank.");
  const capacity = readU32Le(bank, 12);
  const used = readU32Le(bank, 20);
  if (capacity < BANK_DATA_OFFSET || capacity > bank.length
      || used < BANK_DATA_OFFSET || used > capacity)
    throw new Error("The replacement sample-bank header is invalid.");
  return used;
}

export function extractDolBank(input) {
  const located = locateDolBank(input);
  return {
    bank: located.bytes.slice(located.bankOffset,
      located.bankOffset + located.bankCapacity),
    bankOffset: located.bankOffset,
    bankCapacity: located.bankCapacity,
    bankUsed: located.bankUsed,
    entryPoint: located.entryPoint,
  };
}

export function patchDolBank(input, replacementBank) {
  const located = locateDolBank(input);
  const bank = bytesOf(replacementBank);
  const used = replacementUsedLength(bank);
  if (used > located.bankCapacity) {
    throw new Error(`The replacement bank needs ${used} bytes but this DOL reserves only ${located.bankCapacity} bytes.`);
  }

  const output = located.bytes.slice();
  output.fill(0, located.bankOffset,
    located.bankOffset + located.bankCapacity);
  output.set(bank.subarray(0, used), located.bankOffset);
  writeU32Le(output, located.bankOffset + 12, located.bankCapacity);
  return output;
}
