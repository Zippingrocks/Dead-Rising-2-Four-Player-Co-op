#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../../tools/case-zero-runtime/local_save_storage.h"
#include <cassert>
#include <cstdio>

using namespace dr2_save;
struct Context {
    static inline LocalStorage* store = nullptr;
    static inline void* original[kRemoteStorageSlots] = {};
    static LocalStorage& Store() { return *store; }
    static void** Table() { return original; }
    static Policy CurrentPolicy() { return {store->Name(), true}; }
    static void Trace(int, const char*, const char*) {}
};

struct Object { void** table; };
template <int Slot, typename Result, typename... Args>
Result Call(Object& self, Args... args) {
    using Fn = Result(__thiscall*)(void*, Args...);
    return reinterpret_cast<Fn>(self.table[Slot])(&self, args...);
}

int wmain(int argc, wchar_t** argv) {
    static_assert(sizeof(void*) == 4, "Requires x86 ABI");
    assert(argc == 2);
    LocalBackend<Context>::Install(Context::original);
    void* hooks[kRemoteStorageSlots];
    std::memcpy(hooks, Context::original, sizeof(hooks));
    Adapter<Context>::Install(hooks);
    Object object{hooks};
    LocalStorage stores[4];
    std::wstring directories[4];
    const char* logical = kDr2SaveName;
    for (int index = 0; index < 4; index++) {
        directories[index] = std::wstring(argv[1]) + L"\\instance" + std::to_wstring(index);
        assert(CreateDirectoryW(directories[index].c_str(), nullptr));
        assert(stores[index].Configure(directories[index].c_str(), kHarnessSaveNames[index]));
        Context::store = &stores[index];
        assert((Call<kGetFileCount, int>(object) == 0));
        const char payload[] = {'D', 'R', '2', static_cast<char>(index)};
        assert((Call<kFileWrite, bool>(object, logical, static_cast<const void*>(payload), 4)));
        assert((Call<kFileExists, bool>(object, logical)));
        assert(!(Call<kFilePersisted, bool>(object, logical)));
        assert((Call<kFileShare, std::uint64_t>(object, logical) == 0));
        assert((Call<kFileWriteStreamOpen, std::uint64_t>(object, logical) == UINT64_MAX));
        assert(!(Call<7, bool>(object, UINT64_MAX, static_cast<const void*>(payload), 4)));
        assert(!(Call<8, bool>(object, UINT64_MAX)));
        assert(!(Call<9, bool>(object, UINT64_MAX)));
        assert(!(Call<18, bool>(object)) && !(Call<19, bool>(object)));
        Call<20, void>(object, true);
        assert(!(Call<19, bool>(object)));
        assert((Call<kGetFileTimestamp, std::int64_t>(object, logical) > 0));
        int size = 0;
        assert((Call<kGetFileCount, int>(object) == 1));
        assert(SameName(Call<kGetFileNameAndSize, const char*>(object, 0, &size), logical) && size == 4);
        assert(!(Call<kFileWrite, bool>(object, kCaseZeroSaveName, static_cast<const void*>(payload), 4)));
        for (int other = 0; other < 4; other++) if (other != index)
            assert(!(Call<kFileDelete, bool>(object, kHarnessSaveNames[other])));
    }
    for (int index = 0; index < 4; index++) {
        LocalStorage reopened;
        assert(reopened.Configure(directories[index].c_str(), kHarnessSaveNames[index]));
        Context::store = &reopened;
        char data[8] = {};
        assert((Call<kFileRead, int>(object, logical, static_cast<void*>(data), 8) == 4));
        assert(data[0] == 'D' && data[3] == static_cast<char>(index));
        assert((Call<kFileRead, int>(object, logical, static_cast<void*>(data), 2) == 2));

        std::wstring pending = directories[index] + L"\\C4P" + std::to_wstring(index) + L"SAVE.DR2S.pending";
        HANDLE file = CreateFileW(pending.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        assert(file != INVALID_HANDLE_VALUE);
        CloseHandle(file);
        assert(!(Call<kFileWrite, bool>(object, logical, static_cast<const void*>("bad"), 4)));
        assert(GetFileAttributesW(pending.c_str()) != INVALID_FILE_ATTRIBUTES);
        assert((Call<kFileRead, int>(object, logical, static_cast<void*>(data), 8) == 4));
        assert(data[3] == static_cast<char>(index));
        assert(DeleteFileW(pending.c_str()));
        assert((Call<kFileWrite, bool>(object, logical, static_cast<const void*>("replace"), 8)));
        assert((Call<kFileRead, int>(object, logical, static_cast<void*>(data), 8) == 8));
        assert(std::strcmp(data, "replace") == 0);
        assert((Call<kFileDelete, bool>(object, logical)));
        assert((Call<kGetFileCount, int>(object) == 0));
        assert(!(Call<kFileDelete, bool>(object, logical)));
    }
    LocalStorage invalid;
    assert(!invalid.Configure(L"C:\\", kHarnessSaveNames[0]));
    assert(!invalid.Configure(argv[1], "../DR2SAVE.DR2S"));
    assert(!invalid.Configure(L"", kHarnessSaveNames[0]));
    assert(!invalid.Write(kHarnessSaveNames[0], "bad", 4));
    std::puts("Local save x86: four D: file round trips, reopen/persistence, atomic replacement, pending-file protection, blocked cloud operations and namespace isolation passed");
}
