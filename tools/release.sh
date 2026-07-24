#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
version="${1:-v0.13}"
release_dir="$project_dir/build/release"

"$project_dir/tools/verify-build.sh"
npm --prefix "$project_dir/browser-patcher" test
"$project_dir/tools/package-sd.sh"

mkdir -p "$release_dir"
cp "$project_dir/gamecube_ambient_granulator.dol" \
   "$release_dir/gamecube-ambient-granulator-$version.dol"
cp "$project_dir/data/sample_bank.bin" \
   "$release_dir/gamecube-ambient-granulator-sample-bank-$version.bin"
cp "$project_dir/browser-patcher/standalone/gamecube-granulator-patcher.html" \
   "$release_dir/gamecube-granulator-patcher-$version.html"
cp "$project_dir/LICENSE" "$release_dir/"
cp "$project_dir/samples/README.md" "$release_dir/SAMPLE_LICENSE.md"

(
    cd "$release_dir"
    shasum -a 256 \
        "gamecube-ambient-granulator-$version.dol" \
        "gamecube-ambient-granulator-sample-bank-$version.bin" \
        "gamecube-granulator-patcher-$version.html" \
        > SHA256SUMS.txt
    shasum -a 256 -c SHA256SUMS.txt
)

cp "$release_dir/SHA256SUMS.txt" "$project_dir/SHA256SUMS.txt"
printf 'Release artifacts: %s\n' "$release_dir"
