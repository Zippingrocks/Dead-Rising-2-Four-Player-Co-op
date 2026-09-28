#include "../runtime/steam_callback_abi.h"
#include <cassert>
#include <cstdio>

struct Callback {
    void** table;
    int singleCalls;
    int resultCalls;
    unsigned long long peer;
};

void __fastcall Single(Callback* callback, void*, void* payload) {
    callback->singleCalls++;
    callback->peer = *static_cast<unsigned long long*>(payload);
}

void __fastcall Result(Callback* callback, void*, void* payload, bool failed, unsigned long long handle) {
    assert(!failed);
    assert(handle == 0x123456789ABCDEF0ULL);
    callback->resultCalls++;
    callback->peer = *static_cast<unsigned long long*>(payload);
}

int main() {
    static_assert(sizeof(void*) == 4, "This test must use the game's x86 ABI");
    void* table[] = {reinterpret_cast<void*>(&Result), reinterpret_cast<void*>(&Single)};
    Callback callback = {table, 0, 0, 0};
    for (unsigned long long index = 0; index < 1000; index++) {
        unsigned long long peer = 0x0110000170000000ULL + index;
        coop_steam::RunCallback(&callback, &peer);
        assert(callback.peer == peer);
        coop_steam::RunCallResult(&callback, &peer, false, 0x123456789ABCDEF0ULL);
        assert(callback.peer == peer);
    }
    assert(callback.singleCalls == 1000 && callback.resultCalls == 1000);
    std::puts("Steam callback x86 ABI: 2000 dispatches passed");
}
