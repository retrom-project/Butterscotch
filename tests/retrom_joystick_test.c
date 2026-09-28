#include <assert.h>
#include "../src/vm_builtins.c"

void platformLog(MAYBE_UNUSED logType type, const char* format, va_list args) {
    vfprintf(stderr, format, args);
}

int main(void) {
    RunnerGamepadState pads = {0};
    Runner runner = {0};
    runner.gamepads = &pads;
    VMContext vm = {0};
    vm.runner = &runner;
    RValue first[] = {RValue_makeReal(1), RValue_makeReal(2)};
    RValue second[] = {RValue_makeReal(2), RValue_makeReal(2)};

    // Legacy games query joystick 1 and 2 even when the only physical pad is
    // in slot 2 or later. All legacy queries must resolve the same device.
    pads.slots[2].connected = true;
    pads.slots[2].axisValue[0] = 0.75f;
    pads.slots[2].axisValue[1] = -0.75f;
    pads.slots[2].buttonDown[0] = true;
    pads.slots[2].buttonDown[12] = true;
    strcpy(pads.slots[2].description, "Third-slot pad");
    assert(RValue_toBool(builtin_joystick_exists(&vm, first, 1)));
    assert(!RValue_toBool(builtin_joystick_exists(&vm, second, 1)));
    assert(RValue_toReal(builtin_joystick_xpos(&vm, first, 1)) == 0.75);
    assert(RValue_toReal(builtin_joystick_ypos(&vm, first, 1)) == -0.75);
    assert(RValue_toReal(builtin_joystick_direction(&vm, first, 1)) == 105);
    assert(RValue_toReal(builtin_joystick_pov(&vm, first, 1)) == 0);
    assert(RValue_toBool(builtin_joystick_check_button(&vm, first, 2)));
    assert(RValue_toBool(builtin_joystick_has_pov(&vm, first, 1)));
    assert(RValue_toReal(builtin_joystick_buttons(&vm, first, 1)) == GP_BUTTON_COUNT);
    assert(RValue_toReal(builtin_joystick_axes(&vm, first, 1)) == GP_AXIS_COUNT);
    RValue name = builtin_joystick_name(&vm, first, 1);
    assert(strcmp(name.string, "Third-slot pad") == 0);
    RValue_free(&name);

    // A lower-numbered device appearing later must not steal joystick 1.
    pads.slots[0].connected = true;
    pads.slots[0].axisValue[0] = -0.5f;
    assert(RValue_toReal(builtin_joystick_xpos(&vm, first, 1)) == 0.75);
    assert(RValue_toReal(builtin_joystick_xpos(&vm, second, 1)) == -0.5);
    assert(RunnerGamepad_axisValue(&pads, 0, GP_AXIS_LH) == -0.5f);
    assert(RunnerGamepad_axisValue(&pads, 2, GP_AXIS_LH) == 0.75f);

    // Disconnect releases the old channel; the other channel does not move.
    pads.slots[2].connected = false;
    assert(!RValue_toBool(builtin_joystick_exists(&vm, first, 1)));
    assert(!RValue_toBool(builtin_joystick_check_button(&vm, first, 2)));
    assert(RValue_toReal(builtin_joystick_ypos(&vm, first, 1)) == 0);
    assert(RValue_toReal(builtin_joystick_xpos(&vm, second, 1)) == -0.5);
    pads.slots[3].connected = true;
    pads.slots[3].axisValue[0] = 1;
    assert(RValue_toReal(builtin_joystick_xpos(&vm, first, 1)) == 1);
    assert(RValue_toReal(builtin_joystick_xpos(&vm, second, 1)) == -0.5);

    // Existing slot-0/slot-1 identities remain compatible with older saves.
    memset(&pads, 0, sizeof(pads));
    pads.slots[1].connected = true;
    assert(!RValue_toBool(builtin_joystick_exists(&vm, first, 1)));
    assert(RValue_toBool(builtin_joystick_exists(&vm, second, 1)));
    pads.slots[0].connected = true;
    assert(RValue_toBool(builtin_joystick_exists(&vm, first, 1)));
    assert(RValue_toBool(builtin_joystick_exists(&vm, second, 1)));
    first[0] = RValue_makeReal(0);
    assert(!RValue_toBool(builtin_joystick_exists(&vm, first, 1)));
    first[0] = RValue_makeReal(3);
    assert(!RValue_toBool(builtin_joystick_exists(&vm, first, 1)));
    return 0;
}
