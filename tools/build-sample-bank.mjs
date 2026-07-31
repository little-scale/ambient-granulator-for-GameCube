#!/usr/bin/env node

import { mkdir, readFile, readdir, writeFile } from "node:fs/promises";
import { basename, dirname, extname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import { buildBank, makeSample, TARGET_RATE } from "./lib/bank.mjs";
import { decodePcmWav } from "./lib/wav.mjs";

const projectDirectory = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const arguments_ = process.argv.slice(2);
const outputArgument = arguments_[0]?.startsWith("--output=")
  ? arguments_.shift().slice("--output=".length) : "data/sample_bank.bin";
const defaultInputs = (await readdir(resolve(projectDirectory, "samples"), {
  withFileTypes: true,
}))
  .filter((entry) => entry.isFile()
    && extname(entry.name).toLowerCase() === ".wav")
  .sort((left, right) => left.name.localeCompare(right.name, "en", {
    numeric: true,
    sensitivity: "base",
  }))
  .map((entry) => join("samples", entry.name));
const inputs = arguments_.length ? arguments_ : defaultInputs;
if (inputs.length === 0)
  throw new Error("No WAV files found in samples/.");

const samples = [];
for (const input of inputs) {
  const path = resolve(projectDirectory, input);
  const decoded = decodePcmWav(await readFile(path));
  const extension = extname(path);
  samples.push(makeSample({
    name: basename(path, extension),
    sourceRate: decoded.sourceRate,
    data: decoded.data,
    origin: "GAMECUBE BUILD",
  }));
}

const outputPath = resolve(projectDirectory, outputArgument);
const bank = buildBank(samples, undefined, TARGET_RATE);
await mkdir(dirname(outputPath), { recursive: true });
await writeFile(outputPath, bank);
console.log(`Wrote ${outputPath} (${samples.length} sample(s), ${bank.length} bytes).`);
