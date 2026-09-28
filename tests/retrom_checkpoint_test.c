#include <assert.h>
#include "../src/runner.c"

void platformLog(MAYBE_UNUSED logType type, const char* format, va_list args) {
    vfprintf(stderr, format, args);
}

int main(void) {
    // A late lower physical slot must not change the saved legacy selection
    // when a new runner restores with both devices already connected.
    RunnerGamepadState originalPads = {0};
    originalPads.slots[2].connected = true;
    assert(RunnerGamepad_joystickDevice(&originalPads, 1) == 2);
    originalPads.slots[0].connected = true;
    assert(RunnerGamepad_joystickDevice(&originalPads, 2) == 0);
    JsonWriter padWriter = JsonWriter_create();
    JsonWriter_beginObject(&padWriter);
    writeCheckpointJoysticks(&padWriter, &originalPads);
    JsonWriter_endObject(&padWriter);
    char* padJson = JsonWriter_copyOutput(&padWriter);
    JsonValue* padDocument = JsonReader_parse(padJson);
    RunnerGamepadState restoredPads = {0};
    restoredPads.slots[0].connected = true;
    restoredPads.slots[2].connected = true;
    assert(readCheckpointJoysticks(padDocument, restoredPads.joystickDevices));
    assert(RunnerGamepad_joystickDevice(&restoredPads, 1) == 2);
    assert(RunnerGamepad_joystickDevice(&restoredPads, 2) == 0);
    JsonReader_free(padDocument);
    JsonWriter_free(&padWriter);
    free(padJson);
    const char* badPads[] = {
        "{\"joystickDevices\":[3,3]}", "{\"joystickDevices\":[-1,0]}",
        "{\"joystickDevices\":[17,0]}", "{\"joystickDevices\":[0.5,0]}",
        "{\"joystickDevices\":[0]}", "{\"joystickDevices\":null}",
    };
    for (int i = 0; i < (int)(sizeof(badPads) / sizeof(badPads[0])); i++) {
        padDocument = JsonReader_parse(badPads[i]);
        assert(!readCheckpointJoysticks(padDocument, restoredPads.joystickDevices));
        JsonReader_free(padDocument);
    }
    padDocument = JsonReader_parse("{}");
    assert(readCheckpointJoysticks(padDocument, restoredPads.joystickDevices));
    assert(restoredPads.joystickDevices[0] == 1 && restoredPads.joystickDevices[1] == 2);
    JsonReader_free(padDocument);

    DataWin data = {0};
    data.gen8.wadVersion = 17;
    data.detectedFormat.major = 2;
    data.detectedFormat.minor = 3;
    VMContext vm = {0};
    vm.dataWin = &data;
    Runner runner = {0};
    runner.vmContext = &vm;
    JsonValue* document = JsonReader_parse("[[{\"priority\":1.25,\"value\":[7]}]]");
    int32_t cells = 100;
    assert(restoreCheckpointPriorities(&runner, document, &cells));
    assert(arrlen(runner.dsPriorityPool) == 1);
    DsPriorityItem restored = runner.dsPriorityPool[0].items[0];
    assert(restored.depth == 1.25);
    assert(restored.item.type == RVALUE_ARRAY);
    assert(restored.item.array->type == GML_MODERN_ARRAY);
    assert(RValue_toReal(*GMLArray_slot(restored.item.array, 0)) == 7);
    JsonWriter writer = JsonWriter_create();
    JsonWriter_beginObject(&writer);
    writeCheckpointDataStructures(&writer, &runner);
    JsonWriter_endObject(&writer);
    char* encoded = JsonWriter_copyOutput(&writer);
    assert(strstr(encoded, "1.25") != NULL);
    free(encoded);
    JsonWriter_free(&writer);
    JsonReader_free(document);
    clearCheckpointDataStructures(&runner);
    Room room = {0};
    Instance globals = {0};
    runner.currentRoom = &room;
    runner.dataWin = &data;
    runner.pendingRoom = -1;
    vm.globalScopeInstance = &globals;
    assert(Runner_checkpointStatus(&runner) == RUNNER_CHECKPOINT_READY);
    ParticleType type = {0};
    type.used = true;
    arrput(runner.particleTypePool, type);
    assert(Runner_checkpointStatus(&runner) == RUNNER_CHECKPOINT_RUNTIME_RESOURCE_ACTIVE);
    arrfree(runner.particleTypePool);
    AudioEmitter emitter = {0};
    emitter.active = true;
    arrput(runner.audioEmitters, emitter);
    assert(Runner_checkpointStatus(&runner) == RUNNER_CHECKPOINT_RUNTIME_RESOURCE_ACTIVE);
    arrfree(runner.audioEmitters);
    runner.gameSpeedOverride = 60.0;
    assert(Runner_getEffectiveGameSpeed(&runner) == 60.0);
    assert(Runner_checkpointStatus(&runner) == RUNNER_CHECKPOINT_RUNTIME_RESOURCE_ACTIVE);
    runner.gameSpeedOverride = 0.0;
    room.speed = 45;
    assert(Runner_getEffectiveGameSpeed(&runner) == 45.0);
    assert(Runner_checkpointStatus(&runner) == RUNNER_CHECKPOINT_READY);
    return 0;
}
