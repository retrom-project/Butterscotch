#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$root/.cache"
work=$(mktemp -d "$root/.cache/lazy-data-test.XXXXXX")
trap 'rm -rf "$work"' EXIT
cd "$root"
${CC:-cc} -std=gnu99 -DENABLE_WAD14 -DENABLE_WAD16 -DENABLE_WAD17 \
  -ffunction-sections -fdata-sections -Isrc -Ivendor/stb/ds \
  tests/retrom_lazy_data_test.c src/data_win.c src/binary_reader.c src/stb_ds.c src/log.c \
  -Wl,--gc-sections -Wl,--wrap=fread -lm -o "$work/test"
"$work/test" "$work/data-XXXXXX"
