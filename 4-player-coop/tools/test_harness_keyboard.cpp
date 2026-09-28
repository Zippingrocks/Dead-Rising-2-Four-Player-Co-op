#include "../../tools/case-zero-runtime/harness_keyboard.h"
#include <cassert>
#include <cstdio>

int main() {
    HarnessKeyboard keyboard;
    BYTE state[256]{}, read[256];
    struct Event { DWORD key, value, time, sequence; UINT_PTR appData; } events[4]{};
    state[28] = 0x80;
    keyboard.Update(state, 10);
    assert(keyboard.State(256, read) == S_OK && read[28] == 0x80);
    state[28] = 0;
    keyboard.Update(state, 20);
    assert(keyboard.State(256, read) == S_OK && read[28] == 0);
    DWORD count = INFINITE;
    assert(keyboard.Data(sizeof(Event), nullptr, &count, 1) == S_OK && count == 2);
    count = 1;
    assert(keyboard.Data(sizeof(Event), events, &count, 1) == S_OK && count == 1 && events[0].value == 0x80);
    count = 4;
    assert(keyboard.Data(sizeof(Event), events, &count, 0) == S_OK && count == 2);
    assert(events[0].key == 28 && events[1].key == 28 && events[1].value == 0);
    assert(events[1].sequence == events[0].sequence + 1 && events[1].time == 20);
    count = 4;
    assert(keyboard.Data(sizeof(Event), events, &count, 0) == S_OK && count == 0);
    for (int index = 0; index < 600; index++) {
        state[28] ^= 0x80;
        keyboard.Update(state, static_cast<DWORD>(index));
    }
    count = INFINITE;
    assert(keyboard.Data(16, nullptr, &count, 1) == S_FALSE && count == 512);
    count = INFINITE;
    assert(keyboard.Data(16, nullptr, &count, 0) == S_FALSE && count == 512);
    count = 0;
    assert(keyboard.Data(16, nullptr, &count, 0) == S_OK && count == 0);
    assert(keyboard.State(1, read) == E_INVALIDARG);
    assert(keyboard.Data(1, events, &count, 0) == E_INVALIDARG);
    assert(keyboard.Data(16, events, &count, 2) == E_INVALIDARG);
    puts("Harness keyboard: press/release, independent state, peek, flush, bounded overflow and invalid arguments passed");
}
