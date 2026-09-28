#!/usr/bin/env python3
"""Validate the fixed Butterscotch fork baseline and Web host bridge."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BASELINE = "e8294c9070a4fb29a98e6d551fbf773c01214201"


def require(path: str, markers: tuple[str, ...]) -> None:
    text = (ROOT / path).read_text(encoding="utf-8")
    if any(marker not in text for marker in markers):
        raise SystemExit(f"RPG_RUNTIME_SOURCE_CONTRACT_INVALID:{path}")


def main() -> int:
    manifest = json.loads((ROOT / "retrom-fork.json").read_text(encoding="utf-8"))
    expected = {
        "schemaVersion": 1,
        "forkRepository": "https://github.com/retrom-project/Butterscotch",
        "defaultBranch": "retrom/ge8294c9070a4",
        "upstreamMirrorBranch": "main",
        "upstreams": [{
            "role": "engine",
            "repository": "https://github.com/ButterscotchRunner/Butterscotch",
            "refType": "COMMIT",
            "ref": BASELINE,
            "commit": BASELINE,
        }],
        "releaseTagPattern": (
            r"^retrom-core-ge8294c9070a4-r[1-9][0-9]*"
            r"(-rc\.[1-9][0-9]*)?$"
        ),
        "adapterAbi": "butterscotch-checkpoint-v2",
        "releaseAssets": [
            "butterscotch.mjs", "butterscotch.wasm",
            "butterscotch-meta.mjs", "butterscotch-meta.wasm",
            "LICENSE", "rpg-runtime-release.json",
        ],
    }
    if manifest != expected:
        raise SystemExit("RPG_RUNTIME_FORK_MANIFEST_INVALID")
    require("src/retrom-web/main.c", (
        "setRunnerPaused", "setGamepadConnected", "setGamepadButton",
        "setGamepadAxis", "setGamepads", "createRunnerCheckpoint", "restoreRunnerCheckpoint",
        "getRunnerCheckpointStatus",
        "isRunnerCheckpointAvailable", "runnerReady", "runnerExit",
    ))
    require("CMakeLists.txt", (
        "'_getContentReadSlot'", "'_registerContentFile'",
        "'_setRunnerPaused'", "'_setGamepadConnected'",
        "'_setGamepadButton'", "'_setGamepadAxis'", "'_setGamepads'",
        "'_createRunnerCheckpoint'", "'_restoreRunnerCheckpoint'",
        "'_getRunnerCheckpointStatus'", "-sEXIT_RUNTIME=1",
        "'GL'",
    ))
    require("src/retrom-web/content_fs.cpp", ("getContentReadSlot", "registerContentFile", "emscripten_futex_wait"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
