#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
readonly image="devkitpro/devkitppc:20260503"

"$project_dir/tools/test.sh"
"$project_dir/tools/build.sh"

test -s "$project_dir/gamecube_ambient_granulator.dol"
test -s "$project_dir/gamecube_ambient_granulator.elf"

docker run --rm \
    --volume "$project_dir:/work:ro" \
    "$image" \
    /opt/devkitpro/devkitPPC/bin/powerpc-eabi-readelf \
    -h /work/gamecube_ambient_granulator.elf

shasum -a 256 "$project_dir/gamecube_ambient_granulator.dol"
