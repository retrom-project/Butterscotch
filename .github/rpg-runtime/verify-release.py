#!/usr/bin/env python3
"""Validate Butterscotch browser assets and emit release metadata."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


TAG = re.compile(r"^rpg-runtime-gae2602f1f83c-r[1-9][0-9]*(-rc\.[1-9][0-9]*)?$")
COMMIT = re.compile(r"^[0-9a-f]{40}$")
BASELINE = "ae2602f1f83ca70d69b1ca8e66eb336141fe2a16"


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            value.update(chunk)
    return value.hexdigest()


def require_file(path: Path, minimum: int) -> None:
    if path.is_symlink() or not path.is_file() or path.stat().st_size < minimum:
        raise SystemExit(f"RPG_RUNTIME_RELEASE_ASSET_INVALID:{path.name}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    if TAG.fullmatch(args.tag) is None or COMMIT.fullmatch(args.commit) is None:
        raise SystemExit("RPG_RUNTIME_RELEASE_IDENTITY_INVALID")
    if args.repository != "https://github.com/xxxsen/Butterscotch":
        raise SystemExit("RPG_RUNTIME_RELEASE_REPOSITORY_INVALID")

    names = [
        "butterscotch.mjs", "butterscotch.wasm",
        "butterscotch-meta.mjs", "butterscotch-meta.wasm", "LICENSE",
    ]
    minimum = [50_000, 1_000_000, 20_000, 50_000, 10_000]
    paths = [args.output / name for name in names]
    for path, size in zip(paths, minimum, strict=True):
        require_file(path, size)
    for path in (paths[1], paths[3]):
        if path.read_bytes()[:8] != b"\x00asm\x01\x00\x00\x00":
            raise SystemExit(f"RPG_RUNTIME_RELEASE_WASM_INVALID:{path.name}")
    javascript = paths[0].read_text(encoding="utf-8")
    for marker in (
        "_setRunnerPaused", "_setGamepadConnected", "_setGamepadButton",
        "_setGamepadAxis", "_isRunnerCheckpointAvailable",
        "_createRunnerCheckpoint", "_restoreRunnerCheckpoint",
        "runnerReady", "runnerExit",
    ):
        if marker not in javascript:
            raise SystemExit("RPG_RUNTIME_RELEASE_BRIDGE_INVALID")

    assets = [
        {"filename": path.name, "observedSha256": digest(path), "sizeBytes": path.stat().st_size}
        for path in paths
    ]
    metadata = {
        "adapterAbi": "butterscotch-checkpoint-v2",
        "assets": assets,
        "commit": args.commit,
        "digestPolicy": "OBSERVED_CACHE_INTEGRITY_ONLY",
        "repository": args.repository,
        "schemaVersion": 1,
        "sourceCommits": {"engine": BASELINE},
        "tag": args.tag,
    }
    (args.output / "rpg-runtime-release.json").write_text(
        json.dumps(metadata, ensure_ascii=True, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
