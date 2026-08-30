#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
output=${1:?output directory is required}
mkdir -p "$output"
output=$(cd "$output" && pwd)
if find "$output" -mindepth 1 -print -quit | grep -q .; then
  echo "RPG_RUNTIME_OUTPUT_NOT_EMPTY" >&2
  exit 1
fi

mkdir -p "$root/.cache"
build_root=$(mktemp -d "$root/.cache/retrom-web-build.XXXXXX")
cleanup() { rm -rf "$build_root"; }
trap cleanup EXIT INT TERM

docker run --rm \
  --platform linux/amd64 \
  --hostname rpg-runtime-butterscotch \
  --user "$(id -u):$(id -g)" \
  --env HOME=/work/home \
  --volume "$root:/source:ro" \
  --volume "$build_root:/work" \
  --workdir /work \
  emscripten/emsdk:4.0.8 \
  bash -euo pipefail -c '
    mkdir -p "$HOME" full meta
    emcmake cmake -S /source -B full \
      -DWERROR=ON -DPLATFORM=web -DCMAKE_BUILD_TYPE=Release
    cmake --build full --target butterscotch
    emcmake cmake -S /source -B meta \
      -DWERROR=ON -DPLATFORM=web-meta -DCMAKE_BUILD_TYPE=Release
    cmake --build meta --target butterscotch
  '

install -m 0644 "$build_root/full/butterscotch.mjs" "$output/butterscotch.mjs"
install -m 0644 "$build_root/full/butterscotch.wasm" "$output/butterscotch.wasm"
install -m 0644 "$build_root/meta/butterscotch-meta.mjs" "$output/butterscotch-meta.mjs"
install -m 0644 "$build_root/meta/butterscotch-meta.wasm" "$output/butterscotch-meta.wasm"
install -m 0644 "$root/LICENSE" "$output/LICENSE"
