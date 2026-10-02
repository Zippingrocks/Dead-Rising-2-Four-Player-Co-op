#pragma once
#include <windows.h>
#include <cstring>

// Private relative input. Hardware state is never merged into a background test.
class HarnessMouse {
    struct Event { DWORD offset, value, time, sequence; UINT_PTR appData; };
    struct Guard {
        SRWLOCK* lock;
        explicit Guard(SRWLOCK* value) : lock(value) { AcquireSRWLockExclusive(lock); }
        ~Guard() { ReleaseSRWLockExclusive(lock); }
    };
    SRWLOCK lock_ = SRWLOCK_INIT;
    LONG axes_[3]{};
    DWORD buttons_ = 0, command_ = 0, head_ = 0, count_ = 0, eventSequence_ = 0;
    Event events_[512]{};
    bool overflow_ = false;
    void Add(DWORD offset, DWORD value, DWORD now) {
        if (count_ == 512) { head_ = (head_ + 1) % 512; --count_; overflow_ = true; }
        events_[(head_ + count_++) % 512] = {offset, value, now, eventSequence_++, 0};
    }
    void Buttons(DWORD value, DWORD now) {
        for (DWORD i = 0; i < 8; ++i)
            if ((buttons_ ^ value) & (1u << i)) Add(12 + i, value & (1u << i) ? 0x80 : 0, now);
        buttons_ = value;
    }
public:
    bool Submit(DWORD command, LONG x, LONG y, LONG z, DWORD buttons, DWORD now) {
        const LONG values[3]{x, y, z};
        Guard guard(&lock_);
        if (!command || command <= command_ || buttons > 255) return false;
        for (unsigned i = 0; i < 3; ++i)
            if (values[i] < -10000 || values[i] > 10000 ||
                static_cast<LONGLONG>(axes_[i]) + values[i] < -1000000 ||
                static_cast<LONGLONG>(axes_[i]) + values[i] > 1000000) return false;
        for (DWORD i = 0; i < 3; ++i) {
            axes_[i] += values[i];
            if (values[i]) Add(i * 4, static_cast<DWORD>(values[i]), now);
        }
        Buttons(buttons, now);
        command_ = command;
        return true;
    }
    void ReleaseButtons(DWORD now) { Guard guard(&lock_); Buttons(0, now); }
    HRESULT State(DWORD size, void* data, DWORD* command = nullptr) {
        if (!data || (size != 16 && size != 20)) return E_INVALIDARG;
        Guard guard(&lock_);
        memset(data, 0, size);
        memcpy(data, axes_, sizeof(axes_));
        memset(axes_, 0, sizeof(axes_));
        for (DWORD i = 0; i < size - 12; ++i)
            static_cast<BYTE*>(data)[12 + i] = buttons_ & (1u << i) ? 0x80 : 0;
        if (command) *command = command_;
        return S_OK;
    }
    HRESULT Data(DWORD size, void* data, DWORD* count, DWORD flags) {
        if (!count || (size != 16 && size != sizeof(Event)) || (flags & ~1u)) return E_INVALIDARG;
        Guard guard(&lock_);
        const DWORD obtained = *count < count_ ? *count : count_;
        if (data) for (DWORD i = 0; i < obtained; ++i)
            memcpy(static_cast<BYTE*>(data) + i * size, &events_[(head_ + i) % 512], size);
        *count = obtained;
        const HRESULT result = overflow_ ? S_FALSE : S_OK;
        if (!(flags & 1)) {
            head_ = (head_ + obtained) % 512;
            count_ -= obtained;
            overflow_ = false;
        }
        return result;
    }
};
