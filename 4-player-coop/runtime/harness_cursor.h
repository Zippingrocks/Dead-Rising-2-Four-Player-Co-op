#pragma once
#include <windows.h>

// State only. None of these operations invoke desktop cursor APIs.
class HarnessCursor {
 public:
  BOOL SetPosition(int x, int y) {
    AcquireSRWLockExclusive(&lock_);
    point_ = {x, y};
    ReleaseSRWLockExclusive(&lock_);
    return TRUE;
  }
  BOOL GetPosition(POINT* point) {
    if (!point) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    AcquireSRWLockShared(&lock_);
    *point = point_;
    ReleaseSRWLockShared(&lock_);
    return TRUE;
  }
  int Show(BOOL visible) { return visible ? InterlockedIncrement(&displayCount_) : InterlockedDecrement(&displayCount_); }
  HCURSOR SetShape(HCURSOR cursor) {
    return static_cast<HCURSOR>(InterlockedExchangePointer(&cursor_, cursor));
  }
 private:
  SRWLOCK lock_ = SRWLOCK_INIT;
  POINT point_{};
  volatile LONG displayCount_ = 0;
  PVOID volatile cursor_ = nullptr;
};
