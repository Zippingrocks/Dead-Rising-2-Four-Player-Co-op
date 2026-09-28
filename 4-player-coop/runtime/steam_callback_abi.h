#pragma once

namespace coop_steam {

// MSVC x86 orders these overloads differently from their declaration order in Steam headers.
inline void RunCallback(void* callback, void* payload) {
    using Run = void(__thiscall*)(void*, void*);
    auto table = *static_cast<void***>(callback);
    reinterpret_cast<Run>(table[1])(callback, payload);
}

inline void RunCallResult(void* callback, void* payload, bool failed, unsigned long long handle) {
    using Run = void(__thiscall*)(void*, void*, bool, unsigned long long);
    auto table = *static_cast<void***>(callback);
    reinterpret_cast<Run>(table[0])(callback, payload, failed, handle);
}

}
