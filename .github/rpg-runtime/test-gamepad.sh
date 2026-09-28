#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$root/.cache"
work=$(mktemp -d "$root/.cache/gamepad-test.XXXXXX")
trap 'rm -rf "$work"' EXIT
cd "$root"
${CC:-cc} -std=gnu99 -ffunction-sections -fdata-sections -Isrc -Ivendor/stb/ds \
  tests/retrom_gamepad_test.c src/runner_gamepad.c -Wl,--gc-sections -lm -o "$work/test"
"$work/test"
