#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$root/.cache"
work=$(mktemp -d "$root/.cache/checkpoint-test.XXXXXX")
trap 'rm -rf "$work"' EXIT
cd "$root"
${CC:-cc} -std=gnu99 -DENABLE_WAD14 -DENABLE_WAD16 -DENABLE_WAD17 \
  -ffunction-sections -fdata-sections -Isrc -Isrc/video -Ivendor/stb/ds \
  tests/retrom_checkpoint_test.c src/data_win.c src/gml_array.c src/gml_method.c \
  src/instance.c src/int_rvalue_hashmap.c src/json_reader.c src/json_writer.c \
  src/string_builder.c src/stb_ds.c src/log.c -Wl,--gc-sections -lm -o "$work/test"
"$work/test"
