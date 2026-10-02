#include "../../tools/case-zero-runtime/harness_mouse.h"
#include <cassert>
#include <cstdio>

int main() {
    HarnessMouse mouse, other;
    struct State { LONG x, y, z; BYTE buttons[8]; } state{};
    struct Event { DWORD offset, value, time, sequence; UINT_PTR appData; } events[8]{};
    DWORD sequence = 0, count = 8;
    assert(mouse.Submit(1, 24, -5, 120, 1, 10));
    assert(!mouse.Submit(1, 24, -5, 120, 1, 10));
    assert(!mouse.Submit(0, 0, 0, 0, 0, 10));
    assert(mouse.State(16, &state, &sequence) == S_OK && sequence == 1);
    assert(state.x == 24 && state.y == -5 && state.z == 120 && state.buttons[0] == 0x80);
    assert(mouse.State(20, &state) == S_OK && state.x == 0 && state.y == 0 && state.buttons[0] == 0x80);
    assert(other.State(20, &state) == S_OK && state.x == 0 && !state.buttons[0]);
    assert(mouse.Data(sizeof(Event), events, &count, 1) == S_OK && count == 4);
    assert(events[0].offset == 0 && events[1].value == static_cast<DWORD>(-5) && events[3].offset == 12);
    count = 8;
    assert(mouse.Data(sizeof(Event), events, &count, 0) == S_OK && count == 4);
    mouse.ReleaseButtons(20);
    assert(mouse.State(16, &state) == S_OK && !state.buttons[0]);
    count = 8;
    assert(mouse.Data(16, events, &count, 0) == S_OK && count == 1 && events[0].value == 0);
    assert(!mouse.Submit(2, 10001, 0, 0, 0, 20));
    assert(!mouse.Submit(2, 0, 0, 0, 256, 20));
    assert(mouse.Submit(2, 2, 3, 0, 128, 20));
    assert(mouse.Submit(3, 4, 5, 0, 128, 30));
    assert(mouse.State(20, &state) == S_OK && state.x == 6 && state.y == 8 && state.buttons[7] == 0x80);
    for (DWORD i = 4; i < 600; ++i) assert(mouse.Submit(i, 1, 0, 0, i & 1, i));
    count = INFINITE;
    assert(mouse.Data(16, nullptr, &count, 1) == S_FALSE && count == 512);
    count = INFINITE;
    assert(mouse.Data(16, nullptr, &count, 0) == S_FALSE && count == 512);
    count = 8;
    assert(mouse.Data(16, events, &count, 0) == S_OK && count == 0);
    assert(mouse.State(12, &state) == E_INVALIDARG);
    assert(mouse.State(16, nullptr) == E_INVALIDARG);
    assert(mouse.Data(1, events, &count, 0) == E_INVALIDARG);
    assert(mouse.Data(16, events, nullptr, 0) == E_INVALIDARG);
    assert(mouse.Data(16, events, &count, 2) == E_INVALIDARG);
    puts("Harness mouse: isolation, relative consumption, command replay rejection, buttons, buffered reads, bounds and overflow passed");
}
