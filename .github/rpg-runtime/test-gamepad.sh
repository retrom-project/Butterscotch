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
${CC:-cc} -std=gnu99 -DENABLE_WAD14 -DENABLE_WAD16 -DENABLE_WAD17 \
  -ffunction-sections -fdata-sections -Isrc -Isrc/video -Ivendor/stb/ds \
  -Ivendor/md5 -Ivendor/sha1 -Ivendor/base64 \
  tests/retrom_joystick_test.c src/runner_gamepad.c src/log.c \
  src/gml_array.c src/gml_method.c src/instance.c src/int_rvalue_hashmap.c \
  src/string_builder.c src/stb_ds.c \
  -Wl,--gc-sections -lm -o "$work/joystick"
"$work/joystick"
