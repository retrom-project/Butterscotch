#include "stdio_compat.h"
#include "string_compat.h"
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/wasmfs.h>
#include <GLES3/gl3.h>
#include "data_win.h"
#include "noop_audio_system.h"
#include "web_audio_system.h"
#include "overlay_file_system.h"
#include "runner.h"
#include "gl/gl_renderer.h"
#include "gettime.h"

static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = 0;
static Runner* gRunner;
static WebAudioSystem* gWebAudio = nullptr;
static int32_t gAudioSampleRate = 48000;
static pthread_mutex_t gHostMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gHostCondition = PTHREAD_COND_INITIALIZER;
static bool gHostPaused = false;
static bool gGamepadConnected[MAX_GAMEPADS] = {false};
static float gGamepadButtons[MAX_GAMEPADS][GP_BUTTON_COUNT] = {{0}};
static float gGamepadAxes[MAX_GAMEPADS][GP_AXIS_COUNT] = {{0}};

uint8_t keyDown[GML_KEY_COUNT] = {0};
uint8_t keyUp[GML_KEY_COUNT] = {0};

void platformLog(const logType type, const char *format, va_list va) {
    FILE *out = stderr;
    switch (type) {
        case LOG_TYPE_NORMAL:
            out = stdout;
            break;
        case LOG_TYPE_WARNING:
            fputs("Warning: ", out);
            break;
        case LOG_TYPE_ERROR:
            fputs("Error: ", out);
            break;
		case LOG_TYPE_DEBUG:
            fputs("Debug: ", out);
            break;
    }
    vfprintf(out, format, va);
}

// Configures the sample rate that miniaudio will mix at. Must match the AudioContext's sampleRate
// on the JS side, and must be called BEFORE startRunner.
void setAudioSampleRate(int32_t rate) {
    if (rate > 0) gAudioSampleRate = rate;
}

// Pulls frameCount interleaved-stereo float32 frames into outPtr (which must point into wasm memory).
// Called from JS by the worker's audio pull loop. Safe to call before the runner starts (returns silence).
void pullAudioFrames(float* outPtr, int32_t frameCount) {
    if (gWebAudio == nullptr || frameCount <= 0) {
        if (outPtr != nullptr && frameCount > 0) {
            memset(outPtr, 0, (size_t) frameCount * 2 * sizeof(float));
        }
        return;
    }
    WebAudioSystem_pullFrames(gWebAudio, outPtr, frameCount);
}

uint8_t* getKeyDownPtr() {
    return keyDown;
}

uint8_t* getKeyUpPtr() {
    return keyUp;
}

int getKeyCount() {
    return GML_KEY_COUNT;
}

void setRunnerPaused(int32_t paused) {
    pthread_mutex_lock(&gHostMutex);
    gHostPaused = paused != 0;
    pthread_cond_broadcast(&gHostCondition);
    pthread_mutex_unlock(&gHostMutex);
}

void setGamepadConnected(int32_t device, int32_t connected) {
    if (device < 0 || device >= MAX_GAMEPADS) return;
    pthread_mutex_lock(&gHostMutex);
    gGamepadConnected[device] = connected != 0;
    if (!gGamepadConnected[device]) {
        memset(gGamepadButtons[device], 0, sizeof(gGamepadButtons[device]));
        memset(gGamepadAxes[device], 0, sizeof(gGamepadAxes[device]));
    }
    pthread_mutex_unlock(&gHostMutex);
}

void setGamepadButton(int32_t device, int32_t button, float value) {
    if (device < 0 || device >= MAX_GAMEPADS || button < 0 || button >= GP_BUTTON_COUNT) return;
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    pthread_mutex_lock(&gHostMutex);
    gGamepadButtons[device][button] = value;
    pthread_mutex_unlock(&gHostMutex);
}

void setGamepadAxis(int32_t device, int32_t axis, float value) {
    if (device < 0 || device >= MAX_GAMEPADS || axis < 0 || axis >= GP_AXIS_COUNT) return;
    if (value < -1.0f) value = -1.0f;
    if (value > 1.0f) value = 1.0f;
    pthread_mutex_lock(&gHostMutex);
    gGamepadAxes[device][axis] = value;
    pthread_mutex_unlock(&gHostMutex);
}

static void waitWhilePaused(void) {
    pthread_mutex_lock(&gHostMutex);
    while (gHostPaused && gRunner != nullptr && !gRunner->shouldExit) {
        pthread_cond_wait(&gHostCondition, &gHostMutex);
    }
    pthread_mutex_unlock(&gHostMutex);
}

static void updateGamepads(RunnerGamepadState* gamepads) {
    RunnerGamepad_beginFrame(gamepads);
    pthread_mutex_lock(&gHostMutex);
    for (int device = 0; device < MAX_GAMEPADS; device++) {
        GamepadSlot* slot = &gamepads->slots[device];
        memcpy(slot->buttonDownPrev, slot->buttonDown, sizeof(slot->buttonDown));
        slot->connected = gGamepadConnected[device];
        if (!slot->connected) {
            memset(slot->buttonDown, 0, sizeof(slot->buttonDown));
            memset(slot->buttonValue, 0, sizeof(slot->buttonValue));
            memset(slot->axisValue, 0, sizeof(slot->axisValue));
            continue;
        }
        gamepads->connectedCount++;
        for (int button = 0; button < GP_BUTTON_COUNT; button++) {
            float value = gGamepadButtons[device][button];
            slot->buttonValue[button] = value;
            slot->buttonDown[button] = value >= slot->triggerThreshold;
            slot->buttonPressed[button] = slot->buttonDown[button] && !slot->buttonDownPrev[button];
            slot->buttonReleased[button] = !slot->buttonDown[button] && slot->buttonDownPrev[button];
        }
        memcpy(slot->axisValue, gGamepadAxes[device], sizeof(slot->axisValue));
    }
    pthread_mutex_unlock(&gHostMutex);
}

int main() {
    logInfo("Howdy! Loritta is so cute! lol\n");
    emscripten_exit_with_live_runtime();
    return 0;
}

// Mounts the browser's OPFS at "/butterscotch" in the WASMFS virtual filesystem.
int mountOpfs(void) {
    backend_t opfs = wasmfs_create_opfs_backend();
    if (!opfs) {
        logWarn("Failed to create OPFS backend\n");
        return -1;
    }
    int rc = wasmfs_create_directory("/butterscotch", 0777, opfs);
    if (rc != 0) {
        logWarn("Failed to mount OPFS at /butterscotch: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

// mkdir -p for WASMFS paths. Used to ensure the saves directory exists before the runner tries to write into it.
static int mkdirP(const char* path) {
    char buf[512];
    size_t len = strlen(path);
    if (len >= sizeof(buf)) return -1;
    memcpy(buf, path, len + 1);
    for (size_t i = 1; len > i; i++) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            if (mkdir(buf, 0777) != 0 && errno != EEXIST) return -1;
            buf[i] = '/';
        }
    }
    if (mkdir(buf, 0777) != 0 && errno != EEXIST) return -1;
    return 0;
}

void* loop() {
    double lastFrameStartMs = emscripten_get_now(); // for delta_time and frame pacing

    gRunner->gameStartTime = nowNanos();
    while (!gRunner->shouldExit) {
        waitWhilePaused();
        if (gRunner->shouldExit) break;
        double frameStartMs = emscripten_get_now();
        gRunner->deltaTime = (frameStartMs - lastFrameStartMs) * 1000.0;
        lastFrameStartMs = frameStartMs;

        RunnerKeyboard_beginFrame(gRunner->keyboard);
        updateGamepads(gRunner->gamepads);

        // Process inputs
        repeat(GML_KEY_COUNT, i) {
            if (keyDown[i]) {
                RunnerKeyboard_onKeyDown(gRunner->keyboard, i);
                keyDown[i] = 0;
            }
            if (keyUp[i]) {
                RunnerKeyboard_onKeyUp(gRunner->keyboard, i);
                keyUp[i] = 0;
            }
        }

        emscripten_webgl_make_context_current(ctx);

        float audioDt = (float) (gRunner->deltaTime / 1000000.0);
        if (0.0f > audioDt) audioDt = 0.0f;
        if (audioDt > 0.1f) audioDt = 0.1f;
        gRunner->audioSystem->vtable->update(gRunner->audioSystem, audioDt);

        // Run one game step (Begin Step, Keyboard, Alarms, Step, End Step, room transitions)
        Runner_step(gRunner);

        int32_t gameW = (int32_t) gRunner->dataWin->gen8.defaultWindowWidth;
        int32_t gameH = (int32_t) gRunner->dataWin->gen8.defaultWindowHeight;

        Runner_drawPre(gRunner, 640, 480);

        Runner_beginFrame(gRunner, gameW, gameH, 640, 480, 640, 480);

        Runner_drawViews(gRunner, gameW, gameH, false);
        gRunner->renderer->vtable->endFrameInit(gRunner->renderer);
        Runner_drawPost(gRunner, 640, 480);
        gRunner->renderer->vtable->endFrameEnd(gRunner->renderer);
        Runner_drawGUI(gRunner, 640, 480, gameW, gameH);

        // Just like glfwSwapBuffers.
        // Only swap when there isn't a room change to match the original runner.
        if (gRunner->pendingRoom == -1) {
            emscripten_webgl_commit_frame();
        }
        Runner_handlePendingRoomChange(gRunner);

        // Frame pacing: sleep until the next frame is due, based on the room's speed.
        // emscripten_get_now() returns milliseconds (performance.now()) and works in workers.
        if (gRunner->currentRoom != nullptr && gRunner->currentRoom->speed > 0) {
            double targetFrameTimeMs = 1000.0 / (double) gRunner->currentRoom->speed;
            double nextFrameTimeMs = lastFrameStartMs + targetFrameTimeMs;
            double remainingMs = nextFrameTimeMs - emscripten_get_now();
            // Sleep for most of the remaining time, then spin-wait for precision.
            if (remainingMs > 2.0) {
                struct timespec ts;
                ts.tv_sec = 0;
                ts.tv_nsec = (long) ((remainingMs - 1.0) * 1000000.0);
                nanosleep(&ts, nullptr);
            }
            while (emscripten_get_now() < nextFrameTimeMs) {
                // Spin-wait for the remaining sub-millisecond
            }
        }
    }

    // Cleanup
    logInfo("Cleaning up runner!\n");

    gRunner->audioSystem->vtable->destroy(gRunner->audioSystem);
    gRunner->audioSystem = nullptr;
    gWebAudio = nullptr;
    gRunner->renderer->vtable->destroy(gRunner->renderer);

    DataWin* dataWin = gRunner->dataWin;
    VMContext* vm = gRunner->vmContext;
    Runner_free(gRunner);
    pthread_mutex_lock(&gHostMutex);
    gRunner = nullptr;
    gHostPaused = false;
    memset(gGamepadConnected, 0, sizeof(gGamepadConnected));
    memset(gGamepadButtons, 0, sizeof(gGamepadButtons));
    memset(gGamepadAxes, 0, sizeof(gGamepadAxes));
    pthread_mutex_unlock(&gHostMutex);
    VM_free(vm);
    DataWin_free(dataWin);

    // We want to *know* when the runner has actually exited, because we also need to track things like "Leave Game" buttons/actions in the game
    MAIN_THREAD_EM_ASM({ postMessage({ type: 'runnerExit' }); });

    return nullptr;
}

void setWindowTitle(const char* title) {
    MAIN_THREAD_EM_ASM({ postMessage({ type: 'windowTitle', title: UTF8ToString($0) }); }, title);
}

// gamePath: WASMFS path to the data.win to load (example: "/butterscotch/games/undertale/data.win").
// savesPath: WASMFS directory where saves should live (example: "/butterscotch/saves/undertale" - Created if it does not exist).
void startRunner(const char* gamePath, const char* savesPath) {
    logInfo("Starting runner! gamePath=%s savesPath=%s\n", gamePath, savesPath);

    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);

    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.alpha = 0;
    attrs.antialias = 0; // Required to avoid "WebGL warning: blitFramebuffer: DRAW_FRAMEBUFFER may not have multiple samples."
    // Both of these are required to allow us to use emscripten_webgl_commit_frame
    attrs.explicitSwapControl = true;
    attrs.renderViaOffscreenBackBuffer = true;

    // Yes, "#canvas" feels nasty as HELL
    // But that's how Emscripten works for SOME REASON
    ctx = emscripten_webgl_create_context("#canvas", &attrs);
    if (0 >= ctx) {
        logError("Failed to create WebGL context: %d\n", (int)ctx);
        abort();
    }

    emscripten_webgl_make_context_current(ctx);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Make sure the saves directory exists. The FileSystem impl will write into it.
    if (savesPath != nullptr && savesPath[0] != '\0') {
        if (mkdirP(savesPath) != 0) {
            logWarn("failed to ensure saves dir exists at %s: %s\n", savesPath, strerror(errno));
        }
    }

    DataWinParserOptions options = {0};
    options.parseGen8 = true;
    options.parseOptn = true;
    options.parseLang = true;
    options.parseExtn = true;
    options.parseSond = true;
    options.parseAgrp = true;
    options.parseSprt = true;
    options.parseBgnd = true;
    options.parsePath = true;
    options.parseScpt = true;
    options.parseGlob = true;
    options.parseShdr = true;
    options.parseFont = true;
    options.parseTmln = true;
    options.parseObjt = true;
    options.parseRoom = true;
    options.parseTpag = true;
    options.parseCode = true;
    options.parseVari = true;
    options.parseFunc = true;
    options.parseStrg = true;
    options.parseTxtr = true;
    options.parseAudo = true;
    options.skipLoadingPreciseMasksForNonPreciseSprites = true;
    options.lazyLoadRooms = false;
    options.eagerlyLoadedRooms = nullptr;
    DataWin* dataWin = DataWin_parse(gamePath, options);

    // return strdup(dataWin->gen8.name);

    // Initialize VM
    VMContext* vm = VM_create(dataWin);

    Renderer* renderer = GLRenderer_create();

    // Bundle path = directory containing data.win, e.g. "/butterscotch/games/undertale/".
    // Save path = whatever the worker passed in, e.g. "/butterscotch/saves/undertale/".
    char* bundleDir = nullptr;
    const char* lastSlash = strrchr(gamePath, '/');
    if (lastSlash != nullptr) {
        size_t len = (size_t) (lastSlash - gamePath + 1);
        bundleDir = (char *)safeMalloc(len + 1);
        memcpy(bundleDir, gamePath, len);
        bundleDir[len] = '\0';
    } else {
        bundleDir = safeStrdup("./");
    }
    OverlayFileSystem* overlayFs = OverlayFileSystem_create(bundleDir, savesPath);
    free(bundleDir);

    gWebAudio = WebAudioSystem_create(dataWin, gAudioSampleRate);
    AudioSystem* audioSystem = (AudioSystem*) gWebAudio;

    // Initialize the runner
    Runner* runner = Runner_create(dataWin, vm, renderer, (FileSystem*) overlayFs, audioSystem, 0);
    runner->setWindowTitle = setWindowTitle;
    runner->windowHasFocus = nullptr;

    setWindowTitle(dataWin->gen8.name);

    gRunner = runner;

    // Initialize the first room and fire Game Start / Room Start events
    Runner_initFirstRoom(runner);

    MAIN_THREAD_EM_ASM({ postMessage({ type: 'runnerReady' }); });

    // Start a new thread
    pthread_t tid;
    pthread_create(&tid, NULL, loop, NULL);
    pthread_detach(tid);
}

void stopRunner() {
    logInfo("Marked runner to exit!\n");
    pthread_mutex_lock(&gHostMutex);
    if (gRunner != nullptr) gRunner->shouldExit = true;
    gHostPaused = false;
    pthread_cond_broadcast(&gHostCondition);
    pthread_mutex_unlock(&gHostMutex);
}
