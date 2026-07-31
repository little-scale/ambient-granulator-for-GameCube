#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
readonly image="devkitpro/devkitppc:20260503"
readonly bank="$project_dir/data/sample_bank.bin"
readonly samples="$project_dir/samples"

if [[ ! -s "$bank" || "$samples" -nt "$bank" ]] \
        || find "$samples" -type f -iname '*.wav' -newer "$bank" \
            -print -quit | grep -q .; then
    node "$project_dir/tools/build-sample-bank.mjs"
fi

docker run --rm \
    --user "$(id -u):$(id -g)" \
    --volume "$project_dir:/work" \
    --workdir /work \
    "$image" \
    make "$@"
