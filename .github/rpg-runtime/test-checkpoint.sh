#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$root/.cache"
work=$(mktemp -d "$root/.cache/checkpoint-test.XXXXXX")
trap 'rm -rf "$work"' EXIT
cd "$root"
sources=()
for source in src/*.c; do
  # The test includes runner.c to cover its bounded-value helpers as well as
  # the public restore entry point. No platform loop is needed headlessly.
  case "$source" in src/runner.c|src/loop.c) continue ;; esac
  sources+=("$source")
done
read -r -a cflags <<< "${CFLAGS:-}"
read -r -a ldflags <<< "${LDFLAGS:-}"
${CC:-cc} -std=gnu99 -DENABLE_WAD14 -DENABLE_WAD16 -DENABLE_WAD17 \
  -DENABLE_NOOP_RENDERER -DBUTTERSCOTCH_VIDEO_NULL "${cflags[@]}" \
  -ffunction-sections -fdata-sections -Isrc -Isrc/video -Ivendor/stb/ds \
  -Ivendor/md5 -Ivendor/sha1 -Ivendor/base64 -Ivendor/bzip2 \
  tests/retrom_checkpoint_test.c "${sources[@]}" src/video/null_video.c \
  vendor/md5/md5.c vendor/sha1/sha1.c vendor/base64/base64.c \
  vendor/bzip2/{bz_internal_error,bzlib,crctable,decompress,huffman,randtable}.c \
  -Wl,--gc-sections "${ldflags[@]}" -lm -o "$work/test"
"$work/test"
