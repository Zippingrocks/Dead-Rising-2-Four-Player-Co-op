#pragma once
#include <cstddef>

namespace coop_clothing {
constexpr unsigned kBackingBytes = 0x1A4000;
inline int RequiredFreeHeapSlots(int reclaimableChildren, bool parentsExist) {
    if (reclaimableChildren < 0 || reclaimableChildren > 52) return -1;
    return 52 - reclaimableChildren + (parentsExist ? 0 : 2);
}
struct HeapApi {
    void* (*allocate)(unsigned, int, int, int);
    int (*create)(void*, unsigned, const char*, int);
    void (*configure)(int, int);
    void (*destroy)(int);
    void (*release)(void*);
};

// Native clothing records already hold four players; only their parent heaps need side storage.
struct ExtraHeaps {
    void* owner = nullptr;
    void* buffers[2]{};
    int ids[2]{-1, -1};

    void Reset(const HeapApi& api) {
        for (int i = 1; i >= 0; --i) {
            if (ids[i] > 0) api.destroy(ids[i]);
            if (buffers[i]) api.release(buffers[i]);
            ids[i] = -1;
            buffers[i] = nullptr;
        }
        owner = nullptr;
    }

    bool Create(void* manager, const HeapApi& api) {
        if (!manager || owner) return false;
        for (int i = 0; i < 2; ++i) {
            buffers[i] = api.allocate(kBackingBytes, 3, 0, 2);
            if (!buffers[i]) { Reset(api); return false; }
            ids[i] = api.create(buffers[i], kBackingBytes,
                               i == 0 ? "Co-op clothing P3" : "Co-op clothing P4", 0);
            if (ids[i] <= 0 || ids[i] >= 128) {
                ids[i] = -1;
                Reset(api);
                return false;
            }
            api.configure(ids[i], 1);
        }
        owner = manager;
        return true;
    }
};

struct Owners {
    ExtraHeaps entries[4]{};
    ExtraHeaps* Find(void* manager) {
        if (!manager) return nullptr;
        for (auto& entry : entries) if (entry.owner == manager) return &entry;
        return nullptr;
    }
    ExtraHeaps* Acquire(void* manager, const HeapApi& api) {
        if (auto* entry = Find(manager)) return entry;
        for (auto& entry : entries) if (!entry.owner) return entry.Create(manager, api) ? &entry : nullptr;
        return nullptr;
    }
    void Release(void* manager, const HeapApi& api) {
        if (auto* entry = Find(manager)) entry->Reset(api);
    }
};
}
