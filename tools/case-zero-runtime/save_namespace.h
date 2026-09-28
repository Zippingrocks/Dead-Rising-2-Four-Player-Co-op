#pragma once

#include <cstdint>
#include <cstring>

namespace dr2_save {

inline constexpr char kDr2SaveName[] = "DR2SAVE.DR2S";
inline constexpr char kCaseZeroSaveName[] = "CZ0SAVE.DR2S";
inline constexpr const char* kHarnessSaveNames[] = {
    "C4P0SAVE.DR2S", "C4P1SAVE.DR2S", "C4P2SAVE.DR2S", "C4P3SAVE.DR2S"};

inline bool SameName(const char* left, const char* right) {
    return left && right && _stricmp(left, right) == 0;
}

inline bool ReservedSave(const char* name) {
    if (SameName(name, kDr2SaveName) || SameName(name, kCaseZeroSaveName)) return true;
    for (const char* candidate : kHarnessSaveNames) if (SameName(name, candidate)) return true;
    return false;
}

struct Policy {
    const char* privateName;
    bool saveOnly;

    const char* Map(const char* name) const {
        if (!privateName) return name;
        if (!name || !*name) return nullptr;
        if (SameName(name, kDr2SaveName) || SameName(name, privateName)) return privateName;
        // Reject aliases as well as direct access to another namespace.
        if (ReservedSave(name) || saveOnly || std::strpbrk(name, "/\\:") ||
            std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) return nullptr;
        return name;
    }

    const char* Visible(const char* name) const {
        if (!privateName) return name;
        if (SameName(name, privateName)) return kDr2SaveName;
        if (ReservedSave(name)) return nullptr;
        return Map(name);
    }
};

// Legacy interface 012 order. Modern Steam headers insert async methods here;
// do not substitute a modern interface's slot numbers.
enum RemoteStorageSlot {
    kFileWrite = 0, kFileRead = 1, kFileForget = 2, kFileDelete = 3, kFileShare = 4,
    kSetSyncPlatforms = 5, kFileWriteStreamOpen = 6, kFileExists = 10, kFilePersisted = 11,
    kGetFileSize = 12, kGetFileTimestamp = 13, kGetSyncPlatforms = 14,
    kGetFileCount = 15, kGetFileNameAndSize = 16, kRemoteStorageSlots = 64,
};

// Context supplies the original vtable, current namespace and diagnostic callback.
// Production and the x86 regression executable instantiate these exact wrappers.
template <class Context>
struct Adapter {
    template <int Slot, typename Result, Result Failure, typename... Args>
    static Result __fastcall Forward(void* self, void*, const char* name, Args... args) {
        const char* mapped = Context::CurrentPolicy().Map(name);
        if (!mapped) {
            Context::Trace(Slot, name, nullptr);
            return Failure;
        }
        if (mapped != name) Context::Trace(Slot, name, mapped);
        using Fn = Result(__thiscall*)(void*, const char*, Args...);
        return reinterpret_cast<Fn>(Context::Table()[Slot])(self, mapped, args...);
    }

    static int VisibleIndex(void* self, int index) {
        if (index < 0) return -1;
        using Count = int(__thiscall*)(void*);
        using Name = const char*(__thiscall*)(void*, int, int*);
        const int total = reinterpret_cast<Count>(Context::Table()[kGetFileCount])(self);
        const Policy policy = Context::CurrentPolicy();
        for (int real = 0, visible = 0; real < total; real++) {
            int size = 0;
            const char* name = reinterpret_cast<Name>(Context::Table()[kGetFileNameAndSize])(self, real, &size);
            if (policy.Visible(name) && visible++ == index) return real;
        }
        return -1;
    }

    static int __fastcall Count(void* self, void*) {
        using Fn = int(__thiscall*)(void*);
        const int total = reinterpret_cast<Fn>(Context::Table()[kGetFileCount])(self);
        const Policy policy = Context::CurrentPolicy();
        if (!policy.privateName) return total;
        using Name = const char*(__thiscall*)(void*, int, int*);
        int visible = 0;
        for (int real = 0; real < total; real++) {
            int size = 0;
            const char* name = reinterpret_cast<Name>(Context::Table()[kGetFileNameAndSize])(self, real, &size);
            if (policy.Visible(name)) visible++;
        }
        return visible;
    }

    static const char* __fastcall NameAndSize(void* self, void*, int index, int* size) {
        using Fn = const char*(__thiscall*)(void*, int, int*);
        auto original = reinterpret_cast<Fn>(Context::Table()[kGetFileNameAndSize]);
        const Policy policy = Context::CurrentPolicy();
        if (!policy.privateName) return original(self, index, size);
        const int real = VisibleIndex(self, index);
        if (real >= 0) {
            const char* name = policy.Visible(original(self, real, size));
            if (name) return name;
        }
        if (size) *size = 0;
        return "";
    }

    static void Install(void** table) {
        table[kFileWrite] = reinterpret_cast<void*>(&Forward<kFileWrite, bool, false, const void*, int>);
        table[kFileRead] = reinterpret_cast<void*>(&Forward<kFileRead, int, 0, void*, int>);
        table[kFileForget] = reinterpret_cast<void*>(&Forward<kFileForget, bool, false>);
        table[kFileDelete] = reinterpret_cast<void*>(&Forward<kFileDelete, bool, false>);
        table[kFileShare] = reinterpret_cast<void*>(&Forward<kFileShare, std::uint64_t, 0>);
        table[kSetSyncPlatforms] = reinterpret_cast<void*>(&Forward<kSetSyncPlatforms, bool, false, int>);
        table[kFileWriteStreamOpen] = reinterpret_cast<void*>(&Forward<kFileWriteStreamOpen, std::uint64_t, UINT64_MAX>);
        table[kFileExists] = reinterpret_cast<void*>(&Forward<kFileExists, bool, false>);
        table[kFilePersisted] = reinterpret_cast<void*>(&Forward<kFilePersisted, bool, false>);
        table[kGetFileSize] = reinterpret_cast<void*>(&Forward<kGetFileSize, int, 0>);
        table[kGetFileTimestamp] = reinterpret_cast<void*>(&Forward<kGetFileTimestamp, std::int64_t, 0>);
        table[kGetSyncPlatforms] = reinterpret_cast<void*>(&Forward<kGetSyncPlatforms, int, 0>);
        table[kGetFileCount] = reinterpret_cast<void*>(&Count);
        table[kGetFileNameAndSize] = reinterpret_cast<void*>(&NameAndSize);
    }
};

} // namespace dr2_save
