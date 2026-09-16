// psram_allocator.h - ported verbatim from jgauchia/IceNav-v3
// (lib/utils/src/PsramAllocator.hpp, pinned tag v.0.2.9, commit
// d1819b771e12394e185cdf18e8a14875b0998912) as part of adopting
// IceNav-v3's actual routing code (see router.h/graph_loader.h/astar.h).
// Class names kept exactly as upstream for traceability; only the
// filename follows this project's snake_case file-naming convention.
// No adaptation needed - this is pure, hardware-agnostic C++ template
// code with no SD/storage dependency.
#pragma once

#include <cstddef>
#include <memory>
#include "esp_heap_caps.h"

// Allocator that forces memory into SPIRAM (PSRAM).
template <class T>
struct PsramAllocator {
    typedef T value_type;

    PsramAllocator() = default;

    template <class U>
    PsramAllocator(const PsramAllocator<U>&) {}

    T* allocate(std::size_t n) {
        void* ptr = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!ptr) throw std::bad_alloc();
        return static_cast<T*>(ptr);
    }

    void deallocate(T* p, std::size_t) {
        heap_caps_free(p);
    }
};

template <class T, class U>
bool operator==(const PsramAllocator<T>&, const PsramAllocator<U>&) { return true; }

template <class T, class U>
bool operator!=(const PsramAllocator<T>&, const PsramAllocator<U>&) { return false; }

// Allocator that forces memory into internal SRAM for performance.
template <class T>
struct InternalRamAllocator {
    typedef T value_type;

    InternalRamAllocator() = default;

    template <class U>
    InternalRamAllocator(const InternalRamAllocator<U>&) {}

    T* allocate(std::size_t n) {
        return static_cast<T*>(heap_caps_malloc(n * sizeof(T), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }

    void deallocate(T* p, std::size_t) {
        heap_caps_free(p);
    }
};

template <class T, class U>
bool operator==(const InternalRamAllocator<T>&, const InternalRamAllocator<U>&) { return true; }

template <class T, class U>
bool operator!=(const InternalRamAllocator<T>&, const InternalRamAllocator<U>&) { return false; }
