#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
readonly image="devkitpro/devkitppc:20260503"

if [[ ! -s "$project_dir/data/sample_bank.bin" ]]; then
    node "$project_dir/tools/build-sample-bank.mjs"
fi

docker run --rm \
    --user "$(id -u):$(id -g)" \
    --volume "$project_dir:/work" \
    --workdir /work \
    "$image" \
    make "$@"
