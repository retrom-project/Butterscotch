#!/usr/bin/env python3
"""Small source-boundary regressions for the Retrom Web host bridge."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class RetromWebHostTests(unittest.TestCase):
    def setUp(self) -> None:
        self.source = (ROOT / "src/retrom-web/main.c").read_text(encoding="utf-8")
        self.builtins = (ROOT / "src/vm_builtins.c").read_text(encoding="utf-8")
        self.cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.release_workflow = (ROOT / ".github/workflows/rpg-runtime-release.yml").read_text(encoding="utf-8")
        self.build_script = (ROOT / ".github/rpg-runtime/build-web.sh").read_text(encoding="utf-8")

    def test_web_gamepads_are_sampled_at_the_runner_frame_boundary(self) -> None:
        loop = self.source.index("while (!gRunner->shouldExit)")
        gamepads = self.source.index("updateGamepads(gRunner->gamepads);", loop)
        step = self.source.index("Runner_step(gRunner);", loop)
        self.assertLess(gamepads, step)
        self.assertIn("slot->buttonPressed[button] =", self.source)
        self.assertIn("slot->buttonReleased[button] =", self.source)
        self.assertIn("gamepads->connectedCount++;", self.source)

    def test_pause_and_exit_share_the_host_condition(self) -> None:
        self.assertIn("while (gHostPaused && gRunner != nullptr && !gRunner->shouldExit)", self.source)
        self.assertGreaterEqual(self.source.count("pthread_cond_broadcast(&gHostCondition);"), 2)
        self.assertIn("if (gRunner != nullptr) gRunner->shouldExit = true;", self.source)

    def test_ready_and_exit_are_explicit_host_events(self) -> None:
        self.assertEqual(self.source.count("type: 'runnerReady'"), 1)
        self.assertEqual(self.source.count("type: 'runnerExit'"), 1)

    def test_web_warnings_do_not_use_the_browser_error_stream(self) -> None:
        start = self.source.index("void platformLog")
        end = self.source.index("// Configures the sample rate", start)
        logger = self.source[start:end]
        warning = logger.index("case LOG_TYPE_WARNING:")
        error = logger.index("case LOG_TYPE_ERROR:")
        warning_body = logger[warning:error]
        self.assertIn("out = stdout;", warning_body)
        self.assertIn('fputs("Warning: ", out);', warning_body)

    def test_web_exports_are_stable(self) -> None:
        for name in (
            "_setRunnerPaused", "_setGamepadConnected",
            "_setGamepadButton", "_setGamepadAxis",
        ):
            self.assertIn(name, self.cmake)
        self.assertIn('"-sEXIT_RUNTIME=1"', self.cmake)

    def test_checkpoint_is_bounded_and_requires_a_paused_frame_boundary(self) -> None:
        self.assertIn("#define CHECKPOINT_MAX_BYTES (16 * 1024 * 1024)", self.source)
        self.assertIn("gLoopPaused && gCheckpointAvailable", self.source)
        self.assertIn('memcmp(bytes, "BSCP", 4) != 0', self.source)
        self.assertIn("Runner_restoreStateJson(gRunner, json)", self.source)

    def test_checkpoint_v2_preserves_supported_game_data_structures(self) -> None:
        runner = (ROOT / "src/runner.c").read_text(encoding="utf-8")
        self.assertIn('writeCheckpointDataStructures(&w, runner);', runner)
        self.assertIn(
            'restoreCheckpointDataStructures(runner, dataStructures, &remainingCells)',
            runner,
        )
        self.assertIn('"checkpointSchemaVersion", 2', runner)
        self.assertIn("clearCheckpointDataStructures(runner);", runner)
        self.assertNotIn(
            "runner->dsMapPool[i] != nullptr && shlen(runner->dsMapPool[i]) > 0) "
            "return RUNNER_CHECKPOINT_DATA_STRUCTURE_ACTIVE",
            runner,
        )
        self.assertIn("writeUint32LE(gCheckpointBytes + 4, 2);", self.source)
        self.assertIn("readUint32LE(bytes + 4) != 2", self.source)

    def test_checkpoint_v2_restores_empty_game_variable_names(self) -> None:
        runner = (ROOT / "src/runner.c").read_text(encoding="utf-8")
        start = runner.index("static bool restoreCheckpointVariables")
        end = runner.index("static void clearCheckpointInstances", start)
        restore = runner[start:end]
        self.assertIn("name == nullptr", restore)
        self.assertNotIn("name[0] == '\\0'", restore)
        self.assertIn("VM_getOrAllocateVarID(vm, name)", restore)

    def test_closing_a_dirty_ini_clears_the_transient_checkpoint_blocker(self) -> None:
        start = self.builtins.index("static RValue builtin_ini_close")
        end = self.builtins.index("static RValue builtin_ini_read_string", start)
        close_body = self.builtins[start:end]
        cache_move = close_body.index("runner->cachedIni = runner->currentIni;")
        self.assertLess(close_body.index("runner->currentIniDirty = false;"), cache_move)

    def test_release_workflow_supports_immutable_integration_candidates(self) -> None:
        self.assertIn("(-rc\\.[1-9][0-9]*)?", self.release_workflow)
        self.assertIn("prerelease+=(--prerelease)", self.release_workflow)

    def test_clean_checkout_build_creates_its_ignored_workspace(self) -> None:
        self.assertLess(self.build_script.index('mkdir -p "$root/.cache"'), self.build_script.index("mktemp -d"))


if __name__ == "__main__":
    unittest.main()
