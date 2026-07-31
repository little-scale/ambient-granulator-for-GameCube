import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import test from "node:test";

import { buildBank, decodeBank } from "../src/bank.mjs";
import { extractDolBank, patchDolBank } from "../src/dol.mjs";

const realBank = new Uint8Array(readFileSync(
  new URL("../../data/sample_bank.bin", import.meta.url)));
const decodedRealBank = decodeBank(realBank);

function writeU32Be(view, offset, value) {
  view.setUint32(offset, value >>> 0, false);
}

function makeDol(bank) {
  const textOffset = 0x100;
  const textSize = 0x20;
  const dataOffset = textOffset + textSize;
  const bankOffsetInData = 0x40;
  const dataSize = bankOffsetInData + bank.length + 0x20;
  const output = new Uint8Array(dataOffset + dataSize);
  const view = new DataView(output.buffer);

  writeU32Be(view, 0x00, textOffset);
  writeU32Be(view, 0x48, 0x80003100);
  writeU32Be(view, 0x90, textSize);
  writeU32Be(view, 0x1c, dataOffset);
  writeU32Be(view, 0x64, 0x80004000);
  writeU32Be(view, 0xac, dataSize);
  writeU32Be(view, 0xe0, 0x80003100);

  output.fill(0xa5, textOffset, textOffset + textSize);
  output.set(new TextEncoder().encode("NDSGRN01"), dataOffset + 8);
  output.set(bank, dataOffset + bankOffsetInData);
  output.fill(0x5a, dataOffset + bankOffsetInData + bank.length);
  return output;
}

test("extracts the only structurally valid bank from a DOL data section", () => {
  const fixture = makeDol(realBank);
  const extracted = extractDolBank(fixture);
  assert.deepEqual(extracted.bank, realBank);
  assert.equal(extracted.bankCapacity, realBank.length);
  assert.equal(extracted.bankUsed, realBank.length);
  assert.equal(extracted.entryPoint, 0x80003100);
});

test("patches a smaller bank without changing DOL size or surrounding bytes", () => {
  const fixture = makeDol(realBank);
  const before = extractDolBank(fixture);
  const replacement = buildBank(decodedRealBank.samples.slice(0, 2));
  const patched = patchDolBank(fixture, replacement);
  const reopened = extractDolBank(patched);

  assert.equal(patched.length, fixture.length);
  assert.equal(reopened.bankCapacity, realBank.length);
  assert.equal(decodeBank(reopened.bank).samples.length, 2);
  assert.deepEqual(patched.subarray(0, before.bankOffset),
    fixture.subarray(0, before.bankOffset));
  assert.deepEqual(patched.subarray(before.bankOffset + before.bankCapacity),
    fixture.subarray(before.bankOffset + before.bankCapacity));
});

test("rejects invalid DOLs, invalid banks, and a bank larger than its slot", () => {
  assert.throws(() => extractDolBank(new Uint8Array(256)), /supported GameCube DOL/);
  const oneSample = buildBank(decodedRealBank.samples.slice(0, 1));
  const fixture = makeDol(oneSample);
  const twoSamples = buildBank(decodedRealBank.samples.slice(0, 2));
  assert.throws(() => patchDolBank(fixture, twoSamples), /reserves only/);
  assert.throws(() => patchDolBank(fixture, new Uint8Array(5000)),
    /complete NDSGRN01/);
});

const nativeDolUrl = new URL("../../gamecube_ambient_granulator.dol",
  import.meta.url);
test("opens and safely patches the current native v0.14 DOL", {
  skip: !existsSync(nativeDolUrl),
}, () => {
  const nativeDol = new Uint8Array(readFileSync(nativeDolUrl));
  const extracted = extractDolBank(nativeDol);
  assert.equal(decodeBank(extracted.bank).samples.length,
    decodedRealBank.samples.length);
  assert.deepEqual(extracted.bank, realBank);

  const replacement = buildBank(decodedRealBank.samples.slice(0, 2));
  const patched = patchDolBank(nativeDol, replacement);
  const reopened = extractDolBank(patched);
  assert.equal(patched.length, nativeDol.length);
  assert.equal(reopened.bankCapacity, extracted.bankCapacity);
  assert.equal(decodeBank(reopened.bank).samples.length, 2);
  assert.deepEqual(patched.subarray(0, extracted.bankOffset),
    nativeDol.subarray(0, extracted.bankOffset));
  assert.deepEqual(patched.subarray(extracted.bankOffset + extracted.bankCapacity),
    nativeDol.subarray(extracted.bankOffset + extracted.bankCapacity));
});
