#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
artifact="$project_dir/gamecube_ambient_granulator.dol"

if [[ ! -s "$artifact" ]]; then
    "$project_dir/tools/build.sh"
fi

if [[ -n "${DOLPHIN_BIN:-}" && -x "$DOLPHIN_BIN" ]]; then
    emulator="$DOLPHIN_BIN"
elif [[ -x /Applications/Dolphin.app/Contents/MacOS/Dolphin ]]; then
    emulator=/Applications/Dolphin.app/Contents/MacOS/Dolphin
elif command -v dolphin-emu >/dev/null 2>&1; then
    emulator="$(command -v dolphin-emu)"
else
    printf '%s\n' "Dolphin was not found. Install it or set DOLPHIN_BIN." >&2
    exit 1
fi

printf '%s\n' \
    "Default Dolphin keys: Z = burst, X = hold gate, Return = switch view." \
    "Use D + Return to exit (GameCube Z + START)."
exec "$emulator" --exec="$artifact"
