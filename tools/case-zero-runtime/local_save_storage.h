#pragma once

#include <windows.h>
#include <string>
#include "save_namespace.h"

namespace dr2_save {

// Harness-only, one save per disposable directory. Never falls back to Steam.
class LocalStorage {
    std::wstring path_;
    const char* name_ = nullptr;
    SRWLOCK lock_ = SRWLOCK_INIT;

    struct Guard {
        SRWLOCK* lock;
        explicit Guard(SRWLOCK* value) : lock(value) { AcquireSRWLockExclusive(lock); }
        ~Guard() { ReleaseSRWLockExclusive(lock); }
    };

    bool Accepts(const char* name) const { return name_ && SameName(name_, name); }
    bool Info(WIN32_FILE_ATTRIBUTE_DATA& info) const {
        return name_ && GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &info) &&
            !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
    }

public:
    bool Configure(const wchar_t* directory, const char* name) {
        if (name_ || !directory || !name) return false;
        bool known = false;
        for (const char* candidate : kHarnessSaveNames) if (SameName(name, candidate)) known = true;
        if (!known || (directory[0] != L'D' && directory[0] != L'd') ||
            directory[1] != L':' || directory[2] != L'\\') return false;
        wchar_t full[MAX_PATH];
        const DWORD length = GetFullPathNameW(directory, MAX_PATH, full, nullptr);
        if (!length || length >= MAX_PATH - 32) return false;
        std::wstring root(full);
        while (root.size() > 3 && root.back() == L'\\') root.pop_back();
        // Check every ancestor, not only the final directory, for junctions/symlinks.
        for (size_t end = 3; end <= root.size(); end++) {
            if (end < root.size() && root[end] != L'\\') continue;
            const DWORD attributes = GetFileAttributesW(root.substr(0, end).c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        }
        std::wstring filename;
        for (const char* character = name; *character; character++) filename += static_cast<wchar_t>(*character);
        path_ = root + L"\\" + filename;
        name_ = name;
        return true;
    }

    bool Exists(const char* name) {
        Guard guard(&lock_);
        WIN32_FILE_ATTRIBUTE_DATA info{};
        return Accepts(name) && Info(info);
    }

    bool Write(const char* name, const void* data, int size) {
        Guard guard(&lock_);
        if (!Accepts(name) || !data || size < 0 || size > 100 * 1024 * 1024) return false;
        const std::wstring pending = path_ + L".pending";
        // CREATE_NEW refuses an existing pending file. Never delete an unknown previous transaction.
        HANDLE file = CreateFileW(pending.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        const bool ready = WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr) &&
            written == static_cast<DWORD>(size) && FlushFileBuffers(file);
        CloseHandle(file);
        const bool committed = ready && MoveFileExW(pending.c_str(), path_.c_str(),
                                                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!committed) DeleteFileW(pending.c_str());
        return committed;
    }

    int Read(const char* name, void* data, int capacity) {
        Guard guard(&lock_);
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (!Accepts(name) || !data || capacity < 0 || !Info(info)) return 0;
        HANDLE file = CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE) return 0;
        DWORD count = 0;
        const bool read = ReadFile(file, data, static_cast<DWORD>(capacity), &count, nullptr) != FALSE;
        CloseHandle(file);
        return read ? static_cast<int>(count) : 0;
    }

    bool Delete(const char* name) {
        Guard guard(&lock_);
        WIN32_FILE_ATTRIBUTE_DATA info{};
        return Accepts(name) && Info(info) && DeleteFileW(path_.c_str());
    }

    int Size(const char* name) {
        Guard guard(&lock_);
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (!Accepts(name) || !Info(info) || info.nFileSizeHigh || info.nFileSizeLow > INT32_MAX) return 0;
        return static_cast<int>(info.nFileSizeLow);
    }

    std::int64_t Timestamp(const char* name) {
        Guard guard(&lock_);
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (!Accepts(name) || !Info(info)) return 0;
        ULARGE_INTEGER time{};
        time.LowPart = info.ftLastWriteTime.dwLowDateTime;
        time.HighPart = info.ftLastWriteTime.dwHighDateTime;
        return static_cast<std::int64_t>(time.QuadPart / 10000000ULL) - 11644473600LL;
    }

    const char* Name() const { return name_; }
};

template <class Context>
struct LocalBackend {
    static bool __fastcall Write(void*, void*, const char* name, const void* data, int size) {
        return Context::Store().Write(name, data, size);
    }
    static int __fastcall Read(void*, void*, const char* name, void* data, int size) {
        return Context::Store().Read(name, data, size);
    }
    static bool __fastcall Exists(void*, void*, const char* name) { return Context::Store().Exists(name); }
    static bool __fastcall Delete(void*, void*, const char* name) { return Context::Store().Delete(name); }
    static bool __fastcall Persisted(void*, void*, const char*) { return false; }
    static std::uint64_t __fastcall Share(void*, void*, const char*) { return 0; }
    static bool __fastcall SetSync(void*, void*, const char* name, int) { return Context::Store().Exists(name); }
    static int __fastcall Sync(void*, void*, const char*) { return 0; }
    static std::uint64_t __fastcall StreamOpen(void*, void*, const char*) { return UINT64_MAX; }
    static bool __fastcall StreamChunk(void*, void*, std::uint64_t, const void*, int) { return false; }
    static bool __fastcall StreamEnd(void*, void*, std::uint64_t) { return false; }
    static int __fastcall Size(void*, void*, const char* name) { return Context::Store().Size(name); }
    static std::int64_t __fastcall Timestamp(void*, void*, const char* name) { return Context::Store().Timestamp(name); }
    static int __fastcall Count(void*, void*) { return Context::Store().Exists(Context::Store().Name()) ? 1 : 0; }
    static const char* __fastcall Name(void*, void*, int index, int* size) {
        const char* name = Context::Store().Name();
        if (index != 0 || !Context::Store().Exists(name)) {
            if (size) *size = 0;
            return "";
        }
        if (size) *size = Context::Store().Size(name);
        return name;
    }
    static bool __fastcall CloudEnabled(void*, void*) { return false; }
    static void __fastcall SetCloud(void*, void*, bool) {}

    static void Install(void** table) {
        table[kFileWrite] = reinterpret_cast<void*>(&Write);
        table[kFileRead] = reinterpret_cast<void*>(&Read);
        table[kFileForget] = reinterpret_cast<void*>(&Exists);
        table[kFileDelete] = reinterpret_cast<void*>(&Delete);
        table[kFileShare] = reinterpret_cast<void*>(&Share);
        table[kSetSyncPlatforms] = reinterpret_cast<void*>(&SetSync);
        table[kFileWriteStreamOpen] = reinterpret_cast<void*>(&StreamOpen);
        table[7] = reinterpret_cast<void*>(&StreamChunk);
        table[8] = reinterpret_cast<void*>(&StreamEnd);
        table[9] = reinterpret_cast<void*>(&StreamEnd);
        table[kFileExists] = reinterpret_cast<void*>(&Exists);
        table[kFilePersisted] = reinterpret_cast<void*>(&Persisted);
        table[kGetFileSize] = reinterpret_cast<void*>(&Size);
        table[kGetFileTimestamp] = reinterpret_cast<void*>(&Timestamp);
        table[kGetSyncPlatforms] = reinterpret_cast<void*>(&Sync);
        table[kGetFileCount] = reinterpret_cast<void*>(&Count);
        table[kGetFileNameAndSize] = reinterpret_cast<void*>(&Name);
        table[18] = reinterpret_cast<void*>(&CloudEnabled);
        table[19] = reinterpret_cast<void*>(&CloudEnabled);
        table[20] = reinterpret_cast<void*>(&SetCloud);
    }
};

} // namespace dr2_save
