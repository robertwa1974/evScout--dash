// Minimal Win32 shim for the tiny subset of POSIX <sys/mman.h> that
// Tile-Generator's src/mapped_store.hpp actually uses - mingw-w64 doesn't
// ship sys/mman.h since Windows has no native mmap(). Only implements the
// exact call pattern used there: mmap(nullptr, len, PROT_READ|PROT_WRITE,
// MAP_SHARED, fd, 0) over a CRT fd already opened via open()/_open(), plus
// munmap(). Not a general POSIX mmap replacement.
#pragma once
#include <io.h>
#include <windows.h>
#include <cstddef>

#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define MAP_SHARED 0x01
#define MAP_FAILED ((void*)-1)

inline void* mmap(void* /*addr*/, size_t length, int /*prot*/, int /*flags*/, int fd, long /*offset*/) {
    HANDLE fileHandle = (HANDLE)_get_osfhandle(fd);
    if (fileHandle == INVALID_HANDLE_VALUE) return MAP_FAILED;
    HANDLE mapHandle = CreateFileMappingA(fileHandle, NULL, PAGE_READWRITE, 0, 0, NULL);
    if (!mapHandle) return MAP_FAILED;
    void* addr = MapViewOfFile(mapHandle, FILE_MAP_ALL_ACCESS, 0, 0, length);
    CloseHandle(mapHandle);  // the view keeps the mapping alive once created
    if (!addr) return MAP_FAILED;
    return addr;
}

inline int munmap(void* addr, size_t /*length*/) {
    return UnmapViewOfFile(addr) ? 0 : -1;
}
