# Retrom Web candidate

The fixed upstream baseline is `0c94bfb05e3cb4b8946a38f8085747035dd37f94`.
`retrom-web` adds the host-controlled worker, lazy Content I/O, input and
native-save bridge. The independent upstream `web` backend remains available.
`retrom-web-meta` supplies the metadata parser in the release contract.

The runner uses upstream miniaudio with host-driven stereo PCM and upstream
frame pacing. `butterscotch-native-save-v1` exposes WasmFS and
`flushRunnerSaves`: the host pauses at a frame boundary, flushes native writes,
then exports only the session save directory. Import native files before
`startRunner` so game initialization sees them. The runtime owns the bounded
file bundle and declares `GAME_SAVE`; players use the game's own save/load UI.
The core does not serialize VM state.

Run the source and Web host checks in `AGENTS.md`, the lazy-content and gamepad
regressions, then `.github/rpg-runtime/build-candidate.sh <empty-output>`.
Run `node .github/rpg-runtime/test-web-render.mjs <output>` with
`RETROM_PLAYWRIGHT_ROOT` pointing to Playwright 1.61.1 and optional
`RETROM_CHROME_EXECUTABLE` selecting an installed Chrome. This exercises the
non-commercial Loritta fixture, actual WebGL textures and WasmFS native files.
Candidates do not promote maintenance branches or released Provider pins.
