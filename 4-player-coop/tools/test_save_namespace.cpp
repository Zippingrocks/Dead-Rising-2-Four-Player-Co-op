#include "../../tools/case-zero-runtime/save_namespace.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace dr2_save;

struct Storage {
    void** table;
    std::map<std::string, std::vector<char>> files;
    int calls = 0;
    int platforms = 0;
};

struct Context {
    static inline void* original[kRemoteStorageSlots] = {};
    static inline Policy policy = {nullptr, false};
    static inline int blocked = 0;
    static void** Table() { return original; }
    static Policy CurrentPolicy() { return policy; }
    static void Trace(int, const char*, const char* mapped) { if (!mapped) blocked++; }
};

bool __fastcall Write(Storage* self, void*, const char* name, const void* data, int size) {
    self->calls++;
    if (!data || size < 0) return false;
    auto bytes = static_cast<const char*>(data);
    self->files[name] = std::vector<char>(bytes, bytes + size);
    return true;
}
int __fastcall Read(Storage* self, void*, const char* name, void* data, int size) {
    self->calls++;
    const auto file = self->files.find(name);
    if (file == self->files.end() || !data || size < 0) return 0;
    const int length = std::min(size, static_cast<int>(file->second.size()));
    std::memcpy(data, file->second.data(), length);
    return length;
}
bool __fastcall Exists(Storage* self, void*, const char* name) {
    self->calls++;
    return self->files.count(name) != 0;
}
bool __fastcall Delete(Storage* self, void*, const char* name) {
    self->calls++;
    return self->files.erase(name) != 0;
}
std::uint64_t __fastcall Share(Storage* self, void*, const char* name) {
    return Exists(self, nullptr, name) ? 0xFEDCBA9876543210ULL : 0;
}
std::uint64_t __fastcall StreamOpen(Storage* self, void*, const char* name) {
    return Exists(self, nullptr, name) ? 0x12345678ABCDEF90ULL : UINT64_MAX;
}
std::int64_t __fastcall Timestamp(Storage* self, void*, const char* name) {
    return Exists(self, nullptr, name) ? -0x123456789LL : 0;
}
int __fastcall Size(Storage* self, void*, const char* name) {
    self->calls++;
    const auto file = self->files.find(name);
    return file == self->files.end() ? 0 : static_cast<int>(file->second.size());
}
bool __fastcall SetPlatforms(Storage* self, void*, const char* name, int platforms) {
    if (!Exists(self, nullptr, name)) return false;
    self->platforms = platforms;
    return true;
}
int __fastcall GetPlatforms(Storage* self, void*, const char* name) {
    return Exists(self, nullptr, name) ? self->platforms : 0;
}
int __fastcall Count(Storage* self, void*) { return static_cast<int>(self->files.size()); }
const char* __fastcall Name(Storage* self, void*, int index, int* size) {
    if (index < 0 || index >= static_cast<int>(self->files.size())) {
        if (size) *size = 0;
        return "";
    }
    auto file = self->files.begin();
    std::advance(file, index);
    if (size) *size = static_cast<int>(file->second.size());
    return file->first.c_str();
}

template <int Slot, typename Result, typename... Args>
Result Call(Storage& self, Args... args) {
    using Fn = Result(__thiscall*)(void*, Args...);
    return reinterpret_cast<Fn>(self.table[Slot])(&self, args...);
}

int main() {
    static_assert(sizeof(void*) == 4, "Run against the game's x86 ABI");
    Context::original[kFileWrite] = reinterpret_cast<void*>(&Write);
    Context::original[kFileRead] = reinterpret_cast<void*>(&Read);
    Context::original[kFileForget] = reinterpret_cast<void*>(&Exists);
    Context::original[kFileDelete] = reinterpret_cast<void*>(&Delete);
    Context::original[kFileExists] = reinterpret_cast<void*>(&Exists);
    Context::original[kFilePersisted] = reinterpret_cast<void*>(&Exists);
    Context::original[kFileShare] = reinterpret_cast<void*>(&Share);
    Context::original[kFileWriteStreamOpen] = reinterpret_cast<void*>(&StreamOpen);
    Context::original[kGetFileTimestamp] = reinterpret_cast<void*>(&Timestamp);
    Context::original[kGetFileSize] = reinterpret_cast<void*>(&Size);
    Context::original[kSetSyncPlatforms] = reinterpret_cast<void*>(&SetPlatforms);
    Context::original[kGetSyncPlatforms] = reinterpret_cast<void*>(&GetPlatforms);
    Context::original[kGetFileCount] = reinterpret_cast<void*>(&Count);
    Context::original[kGetFileNameAndSize] = reinterpret_cast<void*>(&Name);
    void* hooked[kRemoteStorageSlots];
    std::memcpy(hooked, Context::original, sizeof(hooked));
    Adapter<Context>::Install(hooked);
    Storage self{hooked, {}};
    self.files[kDr2SaveName] = {'v', 'a', 'n', 'i', 'l', 'l', 'a'};
    self.files[kCaseZeroSaveName] = {'c', 'z'};
    self.files["preferences.dat"] = {'p'};
    for (int index = 0; index < 4; index++) self.files[kHarnessSaveNames[index]] = {static_cast<char>('0' + index)};

    const char* logical = kDr2SaveName;
    for (const char* owned : kHarnessSaveNames) {
        Context::policy = {owned, true};
        const auto before = self.files;
        assert((Call<kGetFileCount, int>(self) == 1));
        int size = -1;
        assert(SameName(Call<kGetFileNameAndSize, const char*>(self, 0, &size), logical));
        assert(size == 1);
        for (int invalid : {-1, 1, 100}) {
            assert(std::strcmp(Call<kGetFileNameAndSize, const char*>(self, invalid, &size), "") == 0);
            assert(size == 0);
        }
        assert((Call<kFileWrite, bool>(self, logical, static_cast<const void*>("roundtrip"), 10)));
        char buffer[20] = {};
        assert((Call<kFileRead, int>(self, logical, static_cast<void*>(buffer), 20) == 10));
        assert(std::strcmp(buffer, "roundtrip") == 0);
        assert((Call<kFileRead, int>(self, logical, static_cast<void*>(buffer), 3) == 3));
        assert((Call<kGetFileSize, int>(self, logical) == 10));
        assert((Call<kFileExists, bool>(self, logical)));
        assert((Call<kFilePersisted, bool>(self, logical)));
        assert((Call<kFileForget, bool>(self, logical)));
        assert((Call<kSetSyncPlatforms, bool>(self, logical, 7)));
        assert((Call<kGetSyncPlatforms, int>(self, logical) == 7));
        assert(!(Call<kFileWrite, bool>(self, logical, static_cast<const void*>(nullptr), -1)));
        for (int repeat = 0; repeat < 1000; repeat++) {
            assert((Call<kFileShare, std::uint64_t>(self, logical) == 0xFEDCBA9876543210ULL));
            assert((Call<kFileWriteStreamOpen, std::uint64_t>(self, logical) == 0x12345678ABCDEF90ULL));
            assert((Call<kGetFileTimestamp, std::int64_t>(self, logical) == -0x123456789LL));
        }
        std::vector<const char*> forbidden = {kCaseZeroSaveName, "preferences.dat", "./DR2SAVE.DR2S",
            "..\\DR2SAVE.DR2S", "D:DR2SAVE.DR2S", "", nullptr};
        for (const char* other : kHarnessSaveNames) if (!SameName(other, owned)) forbidden.push_back(other);
        for (const char* name : forbidden) {
            const int calls = self.calls;
            assert(!(Call<kFileWrite, bool>(self, name, static_cast<const void*>("bad"), 4)));
            assert(!(Call<kFileDelete, bool>(self, name)));
            assert(!(Call<kFileForget, bool>(self, name)));
            assert(!(Call<kFileExists, bool>(self, name)));
            assert(!(Call<kFilePersisted, bool>(self, name)));
            assert(!(Call<kSetSyncPlatforms, bool>(self, name, 0)));
            assert((Call<kFileRead, int>(self, name, static_cast<void*>(buffer), 20) == 0));
            assert((Call<kFileShare, std::uint64_t>(self, name) == 0));
            assert((Call<kFileWriteStreamOpen, std::uint64_t>(self, name) == UINT64_MAX));
            assert((Call<kGetFileTimestamp, std::int64_t>(self, name) == 0));
            assert((Call<kGetFileSize, int>(self, name) == 0));
            assert((Call<kGetSyncPlatforms, int>(self, name) == 0));
            assert(self.calls == calls);
        }
        assert((Call<kFileDelete, bool>(self, logical)));
        assert((Call<kGetFileCount, int>(self) == 0));
        assert(!(Call<kFileExists, bool>(self, logical)));
        for (const auto& file : before) if (file.first != owned) assert(self.files.at(file.first) == file.second);
        self.files = before;
    }

    Context::policy = {kCaseZeroSaveName, false};
    assert((Call<kGetFileCount, int>(self) == 2));
    assert(SameName(Context::policy.Map("dr2save.dr2s"), kCaseZeroSaveName));
    assert(Context::policy.Map("C4P0SAVE.DR2S") == nullptr);
    assert(Context::policy.Map("../DR2SAVE.DR2S") == nullptr);
    assert(Context::policy.Visible(nullptr) == nullptr);
    assert(Context::policy.Visible("") == nullptr);
    for (int index = 0; index < 2; index++) {
        const char* name = Call<kGetFileNameAndSize, const char*>(self, index, static_cast<int*>(nullptr));
        assert(SameName(name, logical) || SameName(name, "preferences.dat"));
    }
    Context::policy = {nullptr, false};
    assert((Call<kGetFileCount, int>(self) == 7));
    for (int index = 0; index < 7; index++) {
        int size = 0;
        assert(std::strcmp(Call<kGetFileNameAndSize, const char*>(self, index, &size), Name(&self, nullptr, index, nullptr)) == 0);
    }
    assert((Call<kFileRead, int>(self, logical, static_cast<void*>(nullptr), 0) == 0));
    assert(self.files.at(kDr2SaveName) == std::vector<char>({'v', 'a', 'n', 'i', 'l', 'l', 'a'}));
    assert(Context::blocked > 0);
    std::puts("Save namespace x86: four isolated in-memory round trips, negative access tests, vanilla/Case Zero enumeration, and 12000 wide-return calls passed");
}
