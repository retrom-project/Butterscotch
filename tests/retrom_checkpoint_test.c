#include <assert.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../src/runner.c"
#include "../src/noop_audio_system.h"
#include "../src/noop_file_system.h"
#include "../src/noop_renderer.h"
#include "../src/binary_utils.h"

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
        "{\"variableNames\":{},\"nextDynamicVarID\":2147483648}",
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

static void testCheckpointVariableExhaustion(void) {
    VMContext vm = {0};
    JsonValue* document = JsonReader_parse(
        "{\"variableNames\":{\"existing\":1},\"nextDynamicVarID\":2147483646}");
    int32_t cells = 100;
    assert(restoreCheckpointVariableNames(&vm, document, &cells));
    JsonReader_free(document);
    assert(VM_getOrAllocateVarID(&vm, "last") == INT32_MAX - 1);
    assert(vm.nextDynamicVarID == INT32_MAX);
    // An exhausted cursor still preserves existing bindings on restore.
    document = JsonReader_parse(
        "{\"variableNames\":{\"existing\":1,\"last\":2147483646},\"nextDynamicVarID\":2147483647}");
    assert(restoreCheckpointVariableNames(&vm, document, &cells));
    JsonReader_free(document);
    assert(VM_getOrAllocateVarID(&vm, "existing") == 1);
    assert(VM_getOrAllocateVarID(&vm, "last") == INT32_MAX - 1);

    for (int compiled = 0; compiled < 2; compiled++) {
        int diagnostics[2];
        assert(pipe(diagnostics) == 0);
        pid_t child = fork();
        assert(child >= 0);
        if (child == 0) {
            struct rlimit noCoreDump = {0, 0};
            setrlimit(RLIMIT_CORE, &noCoreDump);
            close(diagnostics[0]);
            assert(dup2(diagnostics[1], STDERR_FILENO) >= 0);
            close(diagnostics[1]);
            if (compiled) {
                DataWin data = {0};
                data.gen8.wadVersion = 16;
                Variable variable = {.name = "compiled_max", .varID = INT32_MAX};
                data.vari.variableCount = 1;
                data.vari.variables = &variable;
                VM_free(VM_create(&data));
            } else {
                VM_getOrAllocateVarID(&vm, "exhausted");
            }
            _exit(0);
        }
        close(diagnostics[1]);
        char message[1024] = {0};
        ssize_t bytes = read(diagnostics[0], message, sizeof(message) - 1);
        assert(bytes > 0);
        close(diagnostics[0]);
        int status;
        assert(waitpid(child, &status, 0) == child);
        assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
        assert(strstr(message, "VM: Variable ID space exhausted") != NULL);
    }
    clearCheckpointVariableNames(&vm);
}

static Runner* createCheckpointRunner(DataWin* data) {
    return Runner_create(data, VM_create(data), NoopRenderer_create(),
        NoopFileSystem_create(), (AudioSystem*) NoopAudioSystem_create(), 1);
}

static void freeCheckpointRunner(Runner* runner) {
    VMContext* vm = runner->vmContext;
    Renderer* renderer = runner->renderer;
    AudioSystem* audio = runner->audioSystem;
    FileSystem* files = runner->fileSystem;
    Runner_free(runner);
    VM_free(vm);
    renderer->vtable->destroy(renderer);
    audio->vtable->destroy(audio);
    NoopFileSystem_destroy(files);
}

static void testCheckpointRunnerRestore(void) {
    // Synthetic WAD16 room creation bytecode executes the real builtin:
    // variable_global_set("room_bootstrap", 73). No game files are needed.
    uint32_t instructions[] = {
        ((uint32_t) OP_PUSHI << 24) | (GML_TYPE_INT16 << 16) | 73,
        ((uint32_t) OP_PUSH << 24) | (GML_TYPE_STRING << 16), 0,
        ((uint32_t) OP_CALL << 24) | (GML_TYPE_VARIABLE << 16) | 2, 0,
        (uint32_t) OP_POPZ << 24, (uint32_t) OP_EXIT << 24,
    };
    uint8_t bytecode[sizeof(instructions)];
    for (size_t i = 0; i < sizeof(instructions) / sizeof(instructions[0]); i++) {
        BinaryUtils_writeUint32(bytecode + i * 4, instructions[i]);
    }
    CodeEntry code = {.name = "checkpoint_room_init", .length = sizeof(bytecode), .present = true};
    Function function = {.name = "variable_global_set"};
    const char* strings[] = {"room_bootstrap"};
    Variable variables[] = {
        {.name = "", .varID = 1}, {.name = "", .varID = 2},
        {.name = "", .varID = 3}, {.name = "compiled", .varID = 7},
    };
    GameObject object = {.name = "checkpoint_actor", .present = true,
        .parentId = -1, .spriteId = -1, .textureMaskId = -1};
    RoomGameObject roomActor = {.objectDefinition = 0, .instanceID = 100001,
        .creationCode = -1, .preCreateCode = -1, .scaleX = 1, .scaleY = 1, .color = 0xffffffff};
    RoomView views[MAX_VIEWS] = {0};
    RoomBackground backgrounds[8] = {0};
    Room rooms[2] = {
        {.name = "checkpoint_entry", .present = true, .payloadLoaded = true,
         .eagerlyLoaded = true, .views = views, .backgrounds = backgrounds,
         .width = 320, .height = 240, .speed = 60, .creationCodeId = 0,
         .gameObjectCount = 1, .gameObjects = &roomActor},
        {.name = "checkpoint_saved_room", .present = true, .payloadLoaded = true,
         .eagerlyLoaded = true, .views = views, .backgrounds = backgrounds,
         .width = 640, .height = 480, .speed = 30, .creationCodeId = -1},
    };
    uint32_t roomOrder[] = {0, 1};
    DataWin data = {0};
    data.gen8.wadVersion = 16;
    data.gen8.roomOrderCount = 2;
    data.gen8.roomOrder = roomOrder;
    data.gen8.lastObj = 100010;
    data.room.count = 2;
    data.room.rooms = rooms;
    data.objt.count = 1;
    data.objt.objects = &object;
    data.vari.variableCount = 4;
    data.vari.variables = variables;
    data.code.count = 1;
    data.code.entries = &code;
    data.func.functionCount = 1;
    data.func.functions = &function;
    data.strg.count = 1;
    data.strg.strings = strings;
    data.bytecodeBuffer = bytecode;

    Runner* original = createCheckpointRunner(&data);
    VMContext* vm = original->vmContext;
    int32_t first = VM_getOrAllocateVarID(vm, "runtime_first");
    Runner_initFirstRoom(original);
    int32_t bootstrap = VM_getOrAllocateVarID(vm, "room_bootstrap");
    assert(first == 8 && bootstrap == 9);
    assert(RValue_toReal(IntRValueHashMap_get(&vm->globalScopeInstance->selfVars, bootstrap)) == 73);
    assert(arrlen(original->instances) == 1 && original->instances[0]->instanceId == 100001);
    original->pendingRoom = 1;
    Runner_handlePendingRoomChange(original);
    int32_t second = VM_getOrAllocateVarID(vm, "runtime_second");
    assert(second == 10);
    Instance_setSelfVar(vm->globalScopeInstance, 1, RValue_makeReal(11));
    Instance_setSelfVar(vm->globalScopeInstance, 2, RValue_makeString("player"));
    Instance_setSelfVar(vm->globalScopeInstance, 3, RValue_makeReal(33));
    Instance* actor = Runner_createInstance(original, 42, 84, 0);
    int32_t actorId = actor->instanceId;
    Instance_setSelfVar(actor, 2, RValue_makeReal(22));
    Instance_setSelfVar(actor, 6, RValue_makeUndefined());
    Instance_setSelfVar(actor, first, RValue_makeString("first"));
    Instance_setSelfVar(actor, second, RValue_makeReal(99));
    actor->alarm[0] = 19;
    actor->activeAlarmMask = 1;
    Instance* later = Runner_createInstance(original, 21, 63, 0);
    int32_t laterId = later->instanceId;
    Instance_setSelfVar(later, first, RValue_makeString("late-owned-string"));
    Instance_setSelfVar(later, second, RValue_makeString("late-valid"));
    JsonValue* lists = JsonReader_parse("[[[7,\"nested-owned-string\"]]]");
    int32_t cells = 100;
    assert(restoreCheckpointLists(original, lists, &cells));
    JsonReader_free(lists);
    original->frameCount = 123;
    original->gamepads->joystickDevices[0] = 3;
    assert(Runner_checkpointStatus(original) == RUNNER_CHECKPOINT_READY);
    char* encoded = Runner_dumpStateJson(original);
    freeCheckpointRunner(original);

    Runner* fresh = createCheckpointRunner(&data);
    vm = fresh->vmContext;
    // The fresh VM has a different allocation order and transient state which
    // public restore must reset before rebuilding the saved room and instances.
    assert(VM_getOrAllocateVarID(vm, "runtime_second") == first);
    assert(VM_getOrAllocateVarID(vm, "cold_only") == bootstrap);
    assert(Runner_restoreStateJson(fresh, encoded));
    assert(fresh->gameStartFired && fresh->currentRoomIndex == 1 && fresh->currentRoom == &rooms[1]);
    assert(fresh->frameCount == 123 && fresh->gamepads->joystickDevices[0] == 3);
    assert(arrlen(fresh->instances) == 2 && hmget(fresh->instancesById, roomActor.instanceID) == NULL);
    assert(arrlen(fresh->instancesByObject[0]) == 2 && arrlen(fresh->instancesByExactObject[0]) == 2);
    actor = hmget(fresh->instancesById, actorId);
    assert(actor != NULL && actor->objectIndex == 0 && actor->createEventFired);
    assert(actor->x == 42 && actor->y == 84 && actor->alarm[0] == 19 && actor->activeAlarmMask == 1);
    assert(fresh->nextInstanceId == (uint32_t) laterId + 1);
    assert(IntRValueHashMap_get(&vm->globalScopeInstance->selfVars, 1).real == 11);
    assert(strcmp(IntRValueHashMap_get(&vm->globalScopeInstance->selfVars, 2).string, "player") == 0);
    assert(IntRValueHashMap_get(&vm->globalScopeInstance->selfVars, 3).real == 33);
    assert(IntRValueHashMap_get(&actor->selfVars, 2).real == 22);
    assert(IntRValueHashMap_contains(&actor->selfVars, 6));
    assert(IntRValueHashMap_get(&actor->selfVars, 6).type == RVALUE_UNDEFINED);
    assert(strcmp(IntRValueHashMap_get(&actor->selfVars, first).string, "first") == 0);
    assert(IntRValueHashMap_get(&actor->selfVars, second).real == 99);
    assert(VM_getOrAllocateVarID(vm, "runtime_first") == first);
    assert(VM_getOrAllocateVarID(vm, "runtime_second") == second);
    assert(VM_getOrAllocateVarID(vm, "room_bootstrap") == bootstrap);
    assert(RValue_toReal(IntRValueHashMap_get(&vm->globalScopeInstance->selfVars, bootstrap)) == 73);
    assert(shgeti(vm->varNameMap, "cold_only") < 0);
    assert(VM_getOrAllocateVarID(vm, "after_restore") == second + 1);
    assert(arrlen(fresh->dsListPool) == 1 && !fresh->dsListPool[0].freed);
    RValue nested = fresh->dsListPool[0].items[0];
    assert(nested.type == RVALUE_ARRAY);
    assert(RValue_toReal(*GMLArray_slot(nested.array, 0)) == 7);
    assert(strcmp(GMLArray_slot(nested.array, 1)->string, "nested-owned-string") == 0);
    freeCheckpointRunner(fresh);

    // Fail in the second instance after restoring DS values, globals, and the
    // first instance. Both teardown and a later retry must release partial state.
    char* malformed = safeStrdup(encoded);
    char* badValue = strstr(malformed, "\"late-valid\"");
    assert(badValue != NULL);
    memcpy(badValue, "{\"bad\":true}", strlen("\"late-valid\""));
    for (int retry = 0; retry < 2; retry++) {
        fresh = createCheckpointRunner(&data);
        assert(!Runner_restoreStateJson(fresh, malformed));
        assert(arrlen(fresh->instances) == 2 && arrlen(fresh->dsListPool) == 1);
        assert(strcmp(IntRValueHashMap_get(&fresh->vmContext->globalScopeInstance->selfVars, 2).string, "player") == 0);
        assert(hmget(fresh->instancesById, actorId)->createEventFired);
        assert(!hmget(fresh->instancesById, laterId)->createEventFired);
        assert(strcmp(IntRValueHashMap_get(&hmget(fresh->instancesById, laterId)->selfVars, first).string,
            "late-owned-string") == 0);
        if (retry) {
            assert(Runner_restoreStateJson(fresh, encoded));
            assert(arrlen(fresh->instances) == 2 && fresh->frameCount == 123);
            assert(strcmp(IntRValueHashMap_get(&hmget(fresh->instancesById, laterId)->selfVars, second).string, "late-valid") == 0);
        }
        freeCheckpointRunner(fresh);
    }
    free(malformed);
    free(encoded);
}

int main(void) {
    testCheckpointVariableIdentity();
    testCheckpointVariableValidation();
    testCheckpointVariableExhaustion();
    testCheckpointRunnerRestore();
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
