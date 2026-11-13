#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$root/.cache"
work=$(mktemp -d "$root/.cache/image-test.XXXXXX")
trap 'rm -rf "$work"' EXIT
python3 - "$work" <<'PY'
import bz2, struct, sys, zlib
from pathlib import Path
root = Path(sys.argv[1])
def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 8, 6, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(b'\0\xff\0\0\xff')) + chunk(b'IEND', b'')
(root / 'red.png').write_bytes(png)
qoi = b'fioq' + struct.pack('<HHI', 1, 1, 5) + b'\xff\xff\0\0\xff'
(root / 'red.qoi.bz2').write_bytes(b'2zoq' + struct.pack('<HH', 1, 1) + bz2.compress(qoi))
PY
cd "$root"
${CC:-cc} -std=gnu99 -O2 -Wall -Wextra -Werror -Wframe-larger-than=65536 \
  -DUSE_WUFFS_IMGDEC -Isrc -Isrc/image -Ivendor/wuffs -Ivendor/bzip2 \
  -c "${RETROM_IMAGE_SOURCE:-src/image/image_decoder.c}" -o "$work/image.o"
${CC:-cc} -std=gnu99 -O2 -Isrc -Ivendor/bzip2 \
  tests/retrom_image_test.c "$work/image.o" src/log.c \
  vendor/bzip2/{bz_internal_error,bzlib,crctable,decompress,huffman,randtable}.c \
  -lm -o "$work/test"
"$work/test" "$work/red.png" "$work/red.qoi.bz2"
