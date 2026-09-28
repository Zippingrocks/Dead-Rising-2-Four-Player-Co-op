#include <windows.h>
#include "../runtime/harness_sync_names.h"
#include <cassert>
#include <cstdio>

int main() {
    assert(!coop_sync::IsEngineJobObject(nullptr));
    assert(!coop_sync::IsEngineJobObject("SteamEvent"));
    assert(!coop_sync::IsEngineJobObject("ThreadPoolEvent"));
    assert(!coop_sync::IsEngineJobObject("ThreadPoolEvent0_extra"));
    HANDLE semaphores[4]{}, events[4]{};
    for (unsigned long index = 0; index < 4; index++) {
        char name[128];
        // Synthetic identities include this fixture PID, so concurrent fixtures remain separate.
        const unsigned long identity = GetCurrentProcessId() * 4 + index;
        assert(coop_sync::PrivateName("ThreadPoolEvent0", identity, name));
        semaphores[index] = CreateSemaphoreA(nullptr, 0, 16, name);
        assert(semaphores[index] && GetLastError() != ERROR_ALREADY_EXISTS);
        assert(coop_sync::PrivateName("HWJobManagerShutdownEvent", identity, name));
        events[index] = CreateEventA(nullptr, TRUE, FALSE, name);
        assert(events[index] && GetLastError() != ERROR_ALREADY_EXISTS);
    }
    assert(ReleaseSemaphore(semaphores[2], 1, nullptr));
    assert(SetEvent(events[2]));
    for (int index = 0; index < 4; index++) {
        const DWORD expected = index == 2 ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
        assert(WaitForSingleObject(semaphores[index], 0) == expected);
        assert(WaitForSingleObject(events[index], 0) == expected);
        CloseHandle(semaphores[index]);
        CloseHandle(events[index]);
    }
    puts("Harness sync: four independent kernel semaphore/event namespaces; unrelated names untouched");
}
