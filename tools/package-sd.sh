#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
destination="$project_dir/sd-card/gamecube-ambient-granulator"

if [[ ! -s "$project_dir/gamecube_ambient_granulator.dol" ]]; then
    "$project_dir/tools/build.sh"
fi
if [[ ! -s "$project_dir/data/sample_bank.bin" ]]; then
    node "$project_dir/tools/build-sample-bank.mjs"
fi

mkdir -p "$destination"
cp "$project_dir/gamecube_ambient_granulator.dol" \
   "$destination/gamecube_ambient_granulator.dol"
cp "$project_dir/data/sample_bank.bin" "$destination/sample_bank.bin"
printf 'SD2SP2 files are ready in %s\n' "$destination"
