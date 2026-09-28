// Generic read-only file tree. The host supplies metadata and services bounded reads;
// the core knows nothing about URLs, project indexes, authentication or caching.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <climits>
#include <cstring>
#include <mutex>
#include <unistd.h>
#include <emscripten/threading.h>
#include <emscripten/wasmfs.h>
#include "memory_backend.h"
#include "wasmfs.h"

namespace {
constexpr size_t blockBytes = 256 * 1024;
// content-read-v1: 16 little-endian i32 controls followed by one payload slot.
// 0 state (idle/requested/ok/closed=0/1/2/4), 1 version, 2 sequence,
// 4 result length, 6 closed, 7 length, 9 file ID, 10/11 offset low/high.
struct ReadSlot {
    std::atomic<int32_t> control[16]{};
    uint8_t bytes[blockBytes]{};
};
static_assert(sizeof(std::atomic<int32_t>) == 4, "content-read-v1 word size");
ReadSlot slot;
std::mutex readMutex;

class ContentFile : public wasmfs::DataFile {
    int32_t id;
    off_t size;
    int open(wasmfs::oflags_t flags) override { return (flags & O_ACCMODE) == O_RDONLY ? 0 : -EROFS; }
    int close() override { return 0; }
    int flush() override { return 0; }
    off_t getSize() override { return size; }
    int setSize(off_t) override { return -EROFS; }
    ssize_t write(const uint8_t*, size_t, off_t) override { return -EROFS; }
    ssize_t read(uint8_t* out, size_t length, off_t offset) override {
        if (offset < 0) return -EINVAL;
        if (offset >= size) return 0;
        length = std::min(length, static_cast<size_t>(size - offset));
        std::lock_guard<std::mutex> lock(readMutex);
        size_t done = 0;
        while (done < length) {
            if (slot.control[6].load() || slot.control[0].load() != 0) return -EIO;
            size_t count = std::min(blockBytes, length - done);
            uint64_t at = static_cast<uint64_t>(offset) + done;
            slot.control[2].fetch_add(1);
            slot.control[4].store(0);
            slot.control[7].store(static_cast<int32_t>(count));
            slot.control[9].store(id);
            slot.control[10].store(static_cast<int32_t>(at));
            slot.control[11].store(static_cast<int32_t>(at >> 32));
            slot.control[0].store(1);
            emscripten_futex_wake(&slot.control[0], INT_MAX);
            double deadline = emscripten_get_now() + 15000;
            while (slot.control[0].load() == 1) {
                double remaining = deadline - emscripten_get_now();
                if (remaining <= 0) {
                    slot.control[6].store(1);
                    slot.control[0].store(4);
                    emscripten_futex_wake(&slot.control[0], INT_MAX);
                    return -EIO;
                }
                emscripten_futex_wait(&slot.control[0], 1, remaining);
            }
            if (slot.control[6].load() || slot.control[0].load() != 2 ||
                slot.control[4].load() != static_cast<int32_t>(count)) return -EIO;
            memcpy(out + done, slot.bytes, count);
            int32_t expected = 2;
            if (!slot.control[0].compare_exchange_strong(expected, 0)) return -EIO;
            emscripten_futex_wake(&slot.control[0], INT_MAX);
            done += count;
        }
        return static_cast<ssize_t>(done);
    }
public:
    ContentFile(mode_t mode, wasmfs::backend_t backend, int32_t fileId, off_t fileSize)
      : DataFile(mode, backend), id(fileId), size(fileSize) {}
};

class ContentBackend : public wasmfs::Backend {
public:
    int32_t nextId = 0;
    off_t nextSize = 0;
    std::shared_ptr<wasmfs::DataFile> createFile(mode_t mode) override {
        return std::make_shared<ContentFile>(mode, this, nextId, nextSize);
    }
    std::shared_ptr<wasmfs::Directory> createDirectory(mode_t mode) override {
        return std::make_shared<wasmfs::MemoryDirectory>(mode, this);
    }
    std::shared_ptr<wasmfs::Symlink> createSymlink(std::string target) override {
        return std::make_shared<wasmfs::MemorySymlink>(target, this);
    }
};
ContentBackend* backend = nullptr;
}

extern "C" {
// Call before starting any game threads, and service this slot until destruction.
void* getContentReadSlot() {
    slot.control[1].store(1);
    return &slot;
}

// Register absolute paths in the caller's memory filesystem before startRunner.
// Parent directories and saves remain ordinary writable memory/OPFS directories.
int registerContentFile(const char* path, int32_t id, double size) {
    if (!path || path[0] != '/' || id < 0 || !std::isfinite(size) || size < 0 ||
        size > 9007199254740991.0 || std::floor(size) != size) return -EINVAL;
    if (!backend) {
        auto owned = std::make_unique<ContentBackend>();
        backend = owned.get();
        wasmfs::wasmFS.addBackend(std::move(owned));
    }
    std::string name(path);
    for (size_t pos = name.find('/', 1); pos != std::string::npos; pos = name.find('/', pos + 1)) {
        if (mkdir(name.substr(0, pos).c_str(), 0777) && errno != EEXIST) return -errno;
    }
    backend->nextId = id;
    backend->nextSize = static_cast<off_t>(size);
    int fd = wasmfs_create_file(path, 0444, reinterpret_cast<backend_t>(backend));
    if (fd < 0) return -errno;
    return close(fd);
}
}
