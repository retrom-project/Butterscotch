#include <assert.h>
#include "runner_gamepad.h"

int main(void) {
    RunnerGamepadState state = {0};
    state.slots[1].connected = true;
    state.slots[1].axisValue[0] = 0.75f;
    state.connectedCount = 1;
    // Enumerating [0, device_count) must include pads after empty slots.
    int found = 0;
    for (int i = 0; i < RunnerGamepad_getDeviceCount(&state); i++) {
        if (RunnerGamepad_isConnected(&state, i)) found++;
    }
    assert(found == 1);
    assert(RunnerGamepad_axisValue(&state, 1, GP_AXIS_LH) == 0.75f);
    state.slots[0].connected = true;
    state.connectedCount = 2;
    assert(RunnerGamepad_getDeviceCount(&state) >= 2);
    state.slots[0].connected = false;
    state.connectedCount = 1;
    assert(RunnerGamepad_getDeviceCount(&state) >= 2);
    state.slots[MAX_GAMEPADS - 1].connected = true;
    assert(RunnerGamepad_getDeviceCount(&state) == MAX_GAMEPADS);
    return 0;
}
