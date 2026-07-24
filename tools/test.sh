#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT

common=(-std=c11 -Wall -Wextra -Werror -pedantic -I"$project_dir/include")

cc "${common[@]}" \
    "$project_dir/source/granular_engine.c" \
    "$project_dir/tests/granular_engine_test.c" \
    -lm -o "$test_dir/granular_engine_test"
"$test_dir/granular_engine_test"

cc "${common[@]}" \
    "$project_dir/source/effects_chain.c" \
    "$project_dir/tests/effects_chain_test.c" \
    -lm -o "$test_dir/effects_chain_test"
"$test_dir/effects_chain_test"

cc "${common[@]}" \
    "$project_dir/source/output_meter.c" \
    "$project_dir/tests/output_meter_test.c" \
    -o "$test_dir/output_meter_test"
"$test_dir/output_meter_test"

cc "${common[@]}" \
    "$project_dir/source/sample_bank.c" \
    "$project_dir/tests/sample_bank_test.c" \
    -o "$test_dir/sample_bank_test"
"$test_dir/sample_bank_test"

cc "${common[@]}" \
    "$project_dir/source/edit_repeat.c" \
    "$project_dir/tests/edit_repeat_test.c" \
    -o "$test_dir/edit_repeat_test"
"$test_dir/edit_repeat_test"

cc "${common[@]}" \
    "$project_dir/source/sample_bank.c" \
    "$project_dir/source/transient_analysis.c" \
    "$project_dir/tests/transient_analysis_test.c" \
    -o "$test_dir/transient_analysis_test"
"$test_dir/transient_analysis_test" \
    "$project_dir/data/sample_bank.bin" \
    "$project_dir/sd-card/gamecube-ambient-granulator/sample_bank.bin"

printf '%s\n' "All host DSP and sample-bank tests passed."
