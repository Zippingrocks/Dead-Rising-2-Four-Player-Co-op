#pragma once
#include <cstring>
#include <cstdio>

namespace coop_sync {
inline bool IsEngineJobObject(const char* name) {
    return name && (strcmp(name, "HWJobManagerShutdownEvent") == 0 ||
        (strlen(name) == 16 && strncmp(name, "ThreadPoolEvent", 15) == 0));
}
template <size_t Size>
bool PrivateName(const char* name, unsigned long processId, char (&output)[Size]) {
    if (!IsEngineJobObject(name)) return false;
    const int length = snprintf(output, Size, "DR2CoopHarness_%lu_%s", processId, name);
    return length > 0 && static_cast<size_t>(length) < Size;
}
}
