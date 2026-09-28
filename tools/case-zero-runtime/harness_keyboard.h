#pragma once
#include <windows.h>
#include <cstring>

// Independent of hardware acquisition: background tests consume only their own scripted keys.
class HarnessKeyboard {
    struct Event { DWORD key, value, time, sequence; UINT_PTR appData; };
    SRWLOCK lock_ = SRWLOCK_INIT;
    BYTE state_[256]{};
    Event events_[512]{};
    DWORD head_ = 0, count_ = 0, sequence_ = 0;
    bool overflow_ = false;
    struct Guard {
        SRWLOCK* lock;
        explicit Guard(SRWLOCK* value) : lock(value) { AcquireSRWLockExclusive(lock); }
        ~Guard() { ReleaseSRWLockExclusive(lock); }
    };
public:
    void Update(const BYTE (&state)[256], DWORD now) {
        Guard guard(&lock_);
        for (DWORD key = 0; key < 256; key++) {
            if (state_[key] == state[key]) continue;
            if (count_ == 512) { head_ = (head_ + 1) % 512; count_--; overflow_ = true; }
            events_[(head_ + count_++) % 512] = {key, state[key], now, sequence_++, 0};
            state_[key] = state[key];
        }
    }
    HRESULT State(DWORD size, void* data) {
        if (!data || size != 256) return E_INVALIDARG;
        Guard guard(&lock_);
        memcpy(data, state_, sizeof(state_));
        return S_OK;
    }
    HRESULT Data(DWORD size, void* data, DWORD* count, DWORD flags) {
        if (!count || (size != 16 && size != sizeof(Event)) || (flags & ~1u)) return E_INVALIDARG;
        Guard guard(&lock_);
        const DWORD obtained = *count < count_ ? *count : count_;
        if (data) for (DWORD index = 0; index < obtained; index++)
            memcpy(static_cast<BYTE*>(data) + index * size, &events_[(head_ + index) % 512], size);
        *count = obtained;
        const HRESULT result = overflow_ ? S_FALSE /* DI_BUFFEROVERFLOW */ : S_OK;
        if (!(flags & 1)) {
            head_ = (head_ + obtained) % 512;
            count_ -= obtained;
            overflow_ = false;
        }
        return result;
    }
};
