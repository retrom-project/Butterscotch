#include <assert.h>
#include "../src/runner.c"

void platformLog(MAYBE_UNUSED logType type, const char* format, va_list args) {
    vfprintf(stderr, format, args);
}

int main(void) {
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
