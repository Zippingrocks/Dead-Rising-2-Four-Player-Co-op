#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int main() {
    static_assert(sizeof(void*) == 4, "WOW64 fixture must be x86");
    Sleep(30000);
    return 0;
}
