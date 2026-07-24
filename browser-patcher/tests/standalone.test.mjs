import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

const html = readFileSync(new URL(
  "../standalone/gamecube-granulator-patcher.html", import.meta.url), "utf8");

test("standalone artifact embeds all runtime code and styling", () => {
  assert.match(html, /^<!doctype html>/i);
  assert.match(html, /<style>[\s\S]+<\/style>/i);
  assert.match(html, /<script type="module">[\s\S]+<\/script>/i);
  assert.doesNotMatch(html, /__PATCHER_(CSS|JS)__/);
  assert.doesNotMatch(html, /<script[^>]+src=/i);
  assert.doesNotMatch(html, /<link[^>]+stylesheet/i);
  assert.doesNotMatch(html, /\bfetch\s*\(/);
  assert.match(html, /OPEN DOL \/ BANK/);
  assert.match(html, /DOWNLOAD SAMPLE_BANK\.BIN/);
  assert.match(html, /DOWNLOAD PATCHED DOL/);
  assert.match(html, /function extractDolBank/);
  assert.match(html, /function patchDolBank/);
  assert.ok(Buffer.byteLength(html) > 35000);
});

test("every JavaScript element binding exists in the standalone interface", () => {
  const selectors = [...html.matchAll(
    /document\.querySelector\("(#[A-Za-z0-9-]+)"\)/g)]
    .map(match => match[1]);
  assert.ok(selectors.length > 20);
  for (const selector of selectors)
    assert.match(html, new RegExp(`id=["']${selector.slice(1)}["']`));
});
