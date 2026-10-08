#include <assert.h>
#include "../src/runner.c"

void platformLog(MAYBE_UNUSED logType type, const char* format, va_list args) {
    vfprintf(stderr, format, args);
}

static void testCheckpointVariableIdentity(void) {
    DataWin data = {0};
    data.gen8.wadVersion = 16;
    Room room = {.name = "checkpoint fixture"};
    Instance globals = {.objectIndex = STRUCT_OBJECT_INDEX};
    VMContext vm = {.dataWin = &data, .globalScopeInstance = &globals, .nextDynamicVarID = 8};
    shput(vm.varNameMap, safeStrdup(""), 1);
    int32_t firstDynamic = VM_getOrAllocateVarID(&vm, "runtime_first");
    int32_t secondDynamic = VM_getOrAllocateVarID(&vm, "runtime_second");
    Runner runner = {.dataWin = &data, .vmContext = &vm, .currentRoom = &room};
    Instance actor = {.objectIndex = -1, .active = true};
    arrput(runner.instances, &actor);
    Instance_setSelfVar(&globals, 1, RValue_makeReal(11));
    Instance_setSelfVar(&globals, 2, RValue_makeString("player"));
    Instance_setSelfVar(&globals, 3, RValue_makeReal(33));
    Instance_setSelfVar(&actor, 2, RValue_makeReal(22));
    Instance_setSelfVar(&actor, 6, RValue_makeUndefined());
    Instance_setSelfVar(&actor, firstDynamic, RValue_makeString("first"));
    Instance_setSelfVar(&actor, secondDynamic, RValue_makeReal(99));
    char* encoded = Runner_dumpStateJson(&runner);
    JsonValue* document = JsonReader_parse(encoded);

    VMContext fresh = {.dataWin = &data, .nextDynamicVarID = 8};
    shput(fresh.varNameMap, safeStrdup(""), 1);
    // Cold room initialization can allocate variables in a different order.
    assert(VM_getOrAllocateVarID(&fresh, "runtime_second") == firstDynamic);
    assert(VM_getOrAllocateVarID(&fresh, "temporary_room_variable") == secondDynamic);
    int32_t cells = 100;
    assert(restoreCheckpointVariableNames(&fresh, document, &cells));
    Instance restoredGlobals = {.objectIndex = STRUCT_OBJECT_INDEX};
    assert(restoreCheckpointVariables(&fresh, &restoredGlobals,
        JsonReader_getJsonValueByKey(document, "globalVariables"), &cells));
    assert(IntRValueHashMap_get(&restoredGlobals.selfVars, 1).real == 11);
    RValue name = IntRValueHashMap_get(&restoredGlobals.selfVars, 2);
    assert(name.type == RVALUE_STRING && strcmp(name.string, "player") == 0);
    assert(IntRValueHashMap_get(&restoredGlobals.selfVars, 3).real == 33);

    JsonValue* instances = JsonReader_getJsonValueByKey(document, "instances");
    JsonValue* savedActor = JsonReader_getArrayElement(instances, 0);
    Instance restoredActor = {0};
    assert(restoreCheckpointVariables(&fresh, &restoredActor,
        JsonReader_getJsonValueByKey(savedActor, "selfVariables"), &cells));
    assert(IntRValueHashMap_get(&restoredActor.selfVars, 2).real == 22);
    assert(IntRValueHashMap_contains(&restoredActor.selfVars, 6));
    assert(IntRValueHashMap_get(&restoredActor.selfVars, 6).type == RVALUE_UNDEFINED);
    assert(VM_getOrAllocateVarID(&fresh, "runtime_first") == firstDynamic);
    assert(VM_getOrAllocateVarID(&fresh, "runtime_second") == secondDynamic);
    assert(strcmp(IntRValueHashMap_get(&restoredActor.selfVars, firstDynamic).string, "first") == 0);
    assert(IntRValueHashMap_get(&restoredActor.selfVars, secondDynamic).real == 99);
    assert(shgeti(fresh.varNameMap, "temporary_room_variable") < 0);
    assert(VM_getOrAllocateVarID(&fresh, "after_restore") == secondDynamic + 1);
    IntRValueHashMap_freeAllValues(&globals.selfVars);
    IntRValueHashMap_freeAllValues(&actor.selfVars);
    IntRValueHashMap_freeAllValues(&restoredGlobals.selfVars);
    IntRValueHashMap_freeAllValues(&restoredActor.selfVars);
    clearCheckpointVariableNames(&vm);
    clearCheckpointVariableNames(&fresh);
    arrfree(runner.instances);
    JsonReader_free(document);
    free(encoded);
}

static void testCheckpointVariableValidation(void) {
    VMContext vm = {.nextDynamicVarID = 10};
    const char* invalidVariables[] = {
        "[{\"id\":-1,\"value\":1}]", "[{\"id\":0.5,\"value\":1}]",
        "[{\"id\":2}]", "[{\"value\":1}]",
        "[{\"id\":10,\"value\":1}]",
        "[{\"id\":2,\"value\":1},{\"id\":2,\"value\":2}]",
    };
    for (size_t i = 0; i < sizeof(invalidVariables) / sizeof(invalidVariables[0]); i++) {
        JsonValue* document = JsonReader_parse(invalidVariables[i]);
        Instance target = {0};
        int32_t cells = 100;
        assert(!restoreCheckpointVariables(&vm, &target, document, &cells));
        IntRValueHashMap_freeAllValues(&target.selfVars);
        JsonReader_free(document);
    }
    JsonValue* document = JsonReader_parse("[{\"id\":1,\"value\":11},{\"id\":2,\"value\":22}]");
    Instance target = {0};
    int32_t cells = 1;
    assert(!restoreCheckpointVariables(&vm, &target, document, &cells));
    assert(target.selfVars.count == 0);
    JsonReader_free(document);

    const char* invalidNames[] = {
        "{\"variableNames\":{\"a\":1},\"nextDynamicVarID\":1}",
        "{\"variableNames\":{\"a\":-1},\"nextDynamicVarID\":5}",
        "{\"variableNames\":{\"a\":1.5},\"nextDynamicVarID\":5}",
        "{\"variableNames\":{\"a\":1,\"a\":2},\"nextDynamicVarID\":5}",
        "{\"variableNames\":{},\"nextDynamicVarID\":0}",
        "{\"variableNames\":{},\"nextDynamicVarID\":2147483647}",
    };
    shput(vm.varNameMap, safeStrdup("untouched"), 4);
    for (size_t i = 0; i < sizeof(invalidNames) / sizeof(invalidNames[0]); i++) {
        document = JsonReader_parse(invalidNames[i]);
        cells = 100;
        assert(!restoreCheckpointVariableNames(&vm, document, &cells));
        assert(vm.nextDynamicVarID == 10);
        assert(shlen(vm.varNameMap) == 1 && shget(vm.varNameMap, "untouched") == 4);
        JsonReader_free(document);
    }
    clearCheckpointVariableNames(&vm);
}

int main(void) {
    testCheckpointVariableIdentity();
    testCheckpointVariableValidation();
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
        "{\"joystickDevices\":[0]}", "{\"joystickDevices\":null}", "{}",
    };
    for (int i = 0; i < (int)(sizeof(badPads) / sizeof(badPads[0])); i++) {
        padDocument = JsonReader_parse(badPads[i]);
        assert(!readCheckpointJoysticks(padDocument, restoredPads.joystickDevices));
        JsonReader_free(padDocument);
    }

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
