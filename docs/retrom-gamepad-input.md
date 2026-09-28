# Web gamepad snapshots

The Web bridge accepts a complete input frame with
`setGamepads(const float* values, int32_t count)`. Each device occupies 22 float32
values: connected, 17 standard buttons, then four axes. `count` is bounded by
`MAX_GAMEPADS`; slots beyond it and disconnected slots have zero input. Buttons
and axes are clamped to their respective ranges. The caller retains the buffer;
the core copies it synchronously under the runner's input lock.

Hosts must preserve device indices and empty slots. A complete snapshot is
published under one lock, so the runner cannot sample partial discovery or
partially updated controls between individual host calls. Existing single-field
exports remain available for old callers, but cannot provide this guarantee.

`gamepad_get_device_count()` returns the highest occupied slot plus one (zero
when none are connected). GML can enumerate that range and use
`gamepad_is_connected()` to find controllers after holes. Connected count remains
separate from this enumeration bound.

The core keeps devices independent. The game determines which controller(s)
control which player; there are no game-specific selection rules in this bridge.
Checkpoint v2 is unchanged. Pair the snapshot-capable Web core with its updated
host adapter; this export is checked by the release verifier.

Run `python3 tests/test_retrom_web_host.py` and
`bash .github/rpg-runtime/test-gamepad.sh` for bridge/slot regressions, followed by
the Web build and real browser input, hotplug and fresh-instance restore checks.
