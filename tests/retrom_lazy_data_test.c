#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include "data_win.h"
#include "log.h"

static size_t readBytes;
static long furthestRead;
size_t __real_fread(void*, size_t, size_t, FILE*);
size_t __wrap_fread(void* out, size_t size, size_t count, FILE* file) {
    long at = ftell(file);
    size_t result = __real_fread(out, size, count, file);
    readBytes += result * size;
    if (at > furthestRead) furthestRead = at;
    return result;
}
void platformLog(logType type, const char* format, va_list args) {
    (void)type;
    vfprintf(stderr, format, args);
}
static void number(FILE* file, uint32_t value) {
    unsigned char bytes[4] = {value, value >> 8, value >> 16, value >> 24};
    assert(fwrite(bytes, 1, 4, file) == 4);
}
int main(int argc, char** argv) {
    assert(argc == 2);
    char* path = argv[1];
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE* file = fdopen(fd, "wb");
    const uint32_t length = 8 * 1024 * 1024;
    const uint32_t first = 2 * 1024 * 1024, second = 6 * 1024 * 1024;
    fwrite("FORM", 1, 4, file); number(file, length - 8);
    fwrite("AUDO", 1, 4, file); number(file, length - 16);
    number(file, 2); number(file, first); number(file, second);
    fseek(file, first, SEEK_SET); number(file, 4); fwrite("wave", 1, 4, file);
    fseek(file, second, SEEK_SET); number(file, 0);
    assert(ftruncate(fd, length) == 0);
    fclose(file);
    DataWinParserOptions options = {0};
    options.parseAudo = true;
    options.lazyLoadAudio = true;
    DataWin* data = DataWin_parse(path, options);
    assert(data->audo.count == 2);
    assert(readBytes < 1024);
    assert(furthestRead < 1024);
    DataWin_loadAudoIfNeeded(data, 0);
    assert(data->audo.entries[0].dataSize == 4);
    assert(memcmp(data->audo.entries[0].data, "wave", 4) == 0);
    size_t loaded = readBytes;
    DataWin_loadAudoIfNeeded(data, 0);
    assert(readBytes == loaded);
    DataWin_loadAudoIfNeeded(data, 1);
    assert(data->audo.entries[1].dataSize == 0);
    loaded = readBytes;
    DataWin_loadAudoIfNeeded(data, 1);
    assert(readBytes == loaded);
    DataWin_free(data);
    file = fopen(path, "wb");
    fwrite("FORM", 1, 4, file); number(file, length - 8);
    fwrite("TXTR", 1, 4, file); number(file, length - 16);
    number(file, 2); number(file, 28); number(file, 36);
    number(file, 0); number(file, first); number(file, 0); number(file, second);
    fseek(file, first, SEEK_SET); fwrite("page", 1, 4, file);
    fflush(file); assert(ftruncate(fileno(file), length) == 0); fclose(file);
    readBytes = 0; furthestRead = 0;
    options.parseAudo = false; options.parseTxtr = true; options.lazyLoadTextures = true;
    data = DataWin_parse(path, options);
    assert(data->txtr.count == 2 && readBytes < 1024 && furthestRead < 1024);
    assert(data->txtr.textures[0].blobData == NULL && data->txtr.textures[1].blobData == NULL);
    DataWin_loadTxtrIfNeeded(data, 0);
    assert(memcmp(data->txtr.textures[0].blobData, "page", 4) == 0);
    assert(data->txtr.textures[1].blobData == NULL);
    loaded = readBytes; DataWin_loadTxtrIfNeeded(data, 0); assert(readBytes == loaded);
    DataWin_free(data);
    unlink(path);
    puts("lazy audio and texture indexes and random reads passed");
    return 0;
}
