#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "image/image_decoder.h"
#include "log.h"

void platformLog(logType type, const char* format, va_list args) {
    (void)type;
    vfprintf(stderr, format, args);
}

int main(int argc, char** argv) {
    assert(argc == 3);
    for (int i = 1; i < argc; i++) {
        FILE* file = fopen(argv[i], "rb");
        assert(file != NULL);
        assert(fseek(file, 0, SEEK_END) == 0);
        long size = ftell(file);
        assert(size > 0 && fseek(file, 0, SEEK_SET) == 0);
        unsigned char* source = malloc((size_t)size);
        assert(source != NULL && fread(source, 1, (size_t)size, file) == (size_t)size);
        fclose(file);
        int width = 0, height = 0;
        unsigned char* rgba = ImageDecoder_decodeToRgba(source, (size_t)size, false, &width, &height);
        assert(rgba != NULL && width == 1 && height == 1);
        assert(rgba[0] == 255 && rgba[1] == 0 && rgba[2] == 0 && rgba[3] == 255);
        free(rgba);
        free(source);
    }
    puts("PNG and BZip2/QOI decoded within the Web stack budget");
    return 0;
}
