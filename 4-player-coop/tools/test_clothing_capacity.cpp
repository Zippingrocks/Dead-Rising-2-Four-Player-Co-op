#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include "../runtime/clothing_heap_owner.h"
#include "../runtime/clothing_capacity_thunks.h"

using coop_clothing::ParentHeapThunk;
using coop_clothing::DestroyParentsThunk;
static std::set<void*> buffers;
static std::map<int, void*> heaps;
static int allocations, creations, failAllocation, failCreation, nextId = 10;
static coop_clothing::Owners owners;
static void* lastDestroyed;

static void* Allocate(unsigned size, int flags, int parent, int type) {
    assert(size == coop_clothing::kBackingBytes && flags == 3 && parent == 0 && type == 2);
    if (++allocations == failAllocation) return nullptr;
    void* result = std::malloc(1);
    assert(result);
    buffers.insert(result);
    return result;
}
static int Create(void* data, unsigned size, const char* name, int parent) {
    assert(buffers.count(data) && size == coop_clothing::kBackingBytes && name && parent == 0);
    if (++creations == failCreation) return 0;
    const int id = nextId++;
    heaps[id] = data;
    return id;
}
static void Configure(int id, int flag) { assert(heaps.count(id) && flag == 1); }
static void Destroy(int id) { assert(heaps.erase(id) == 1); }
static void Release(void* data) {
    for (const auto& heap : heaps) assert(heap.second != data);
    assert(buffers.erase(data) == 1);
    std::free(data);
}
static const coop_clothing::HeapApi api{Allocate, Create, Configure, Destroy, Release};

namespace coop_clothing {
int __cdecl ParentHeap(void* manager, int player) {
    if (player < 2) return *reinterpret_cast<int*>(static_cast<BYTE*>(manager) + 0x3DD0 + player * 4);
    auto* owner = owners.Find(manager);
    return owner ? owner->ids[player - 2] : -1;
}
void __cdecl DestroyParents(void* manager) {
    lastDestroyed = manager;
    owners.Release(manager, api);
}
}

static DWORD registers[8], beforeFlags, afterFlags, beforeStack;
__declspec(naked) static void ProbeParent(void*, int) {
    __asm {
        pushad
        mov ebp, [esp + 36]
        mov ebx, [esp + 40]
        mov edi, 11223344h
        mov esi, 22334455h
        mov edx, 33445566h
        mov ecx, 44556677h
        mov eax, 55667788h
        mov beforeStack, esp
        cmp eax, eax
        stc
        pushfd
        pop beforeFlags
        call ParentHeapThunk
        mov registers[0], eax
        mov registers[4], ecx
        mov registers[8], edx
        mov registers[12], ebx
        mov registers[16], esp
        mov registers[20], ebp
        mov registers[24], esi
        mov registers[28], edi
        pushfd
        pop afterFlags
        popad
        ret
    }
}
__declspec(naked) static void ProbeDestroy(void*) {
    __asm {
        pushad
        mov esi, [esp + 36]
        mov ebp, 11223344h
        mov ebx, 22334455h
        mov edx, 33445566h
        mov ecx, 44556677h
        mov eax, 55667788h
        mov edi, 66778899h
        mov beforeStack, esp
        cmp eax, eax
        stc
        pushfd
        pop beforeFlags
        call DestroyParentsThunk
        mov registers[0], eax
        mov registers[4], ecx
        mov registers[8], edx
        mov registers[12], ebx
        mov registers[16], esp
        mov registers[20], ebp
        mov registers[24], esi
        mov registers[28], edi
        pushfd
        pop afterFlags
        popad
        ret
    }
}

int main() {
    assert(coop_clothing::RequiredFreeHeapSlots(0, false) == 54);
    assert(coop_clothing::RequiredFreeHeapSlots(13, false) == 41);
    assert(coop_clothing::RequiredFreeHeapSlots(26, false) == 28);
    assert(coop_clothing::RequiredFreeHeapSlots(13, true) == 39);
    assert(coop_clothing::RequiredFreeHeapSlots(26, true) == 26);
    assert(coop_clothing::RequiredFreeHeapSlots(52, true) == 0);
    assert(coop_clothing::RequiredFreeHeapSlots(-1, false) == -1);
    assert(coop_clothing::RequiredFreeHeapSlots(53, false) == -1);
    BYTE managers[5][0x3E20]{};
    for (auto& manager : managers) {
        *reinterpret_cast<int*>(manager + 0x3DD0) = 30;
        *reinterpret_cast<int*>(manager + 0x3DD4) = 31;
        *reinterpret_cast<int*>(manager + 0x3DD8) = 40;
    }
    assert(!owners.Acquire(nullptr, api));
    for (int failure = 1; failure <= 2; ++failure) {
        allocations = creations = 0;
        failAllocation = failure;
        assert(!owners.Acquire(managers[0], api));
        assert(buffers.empty() && heaps.empty() && !owners.Find(managers[0]));
        allocations = creations = 0;
        failAllocation = 0;
        failCreation = failure;
        assert(!owners.Acquire(managers[0], api));
        assert(buffers.empty() && heaps.empty());
        failCreation = 0;
    }
    for (int i = 0; i < 4; ++i) assert(owners.Acquire(managers[i], api));
    assert(!owners.Acquire(managers[4], api));
    const int oldAllocations = allocations;
    assert(owners.Acquire(managers[0], api) && allocations == oldAllocations);
    assert(heaps.size() == 8 && buffers.size() == 8);
    for (int repeat = 0; repeat < 2000; ++repeat) {
        const int player = repeat % 4;
        const int expected = coop_clothing::ParentHeap(managers[0], player);
        ProbeParent(managers[0], player);
        assert(registers[0] == 0x55667788 && registers[1] == 0x44556677 && registers[2] == 0x33445566);
        assert(registers[3] == static_cast<DWORD>(player) && registers[4] == beforeStack);
        assert(registers[5] == reinterpret_cast<DWORD>(managers[0]) && registers[6] == 0x22334455);
        assert(registers[7] == static_cast<DWORD>(expected) && beforeFlags == afterFlags);
    }
    ProbeDestroy(managers[0]);
    assert(lastDestroyed == managers[0] && !owners.Find(managers[0]) && heaps.size() == 6);
    assert(registers[0] == 30 && registers[1] == 0x44556677 && registers[2] == 0x33445566);
    assert(registers[3] == 0x22334455 && registers[4] == beforeStack && registers[5] == 0x11223344);
    assert(registers[6] == reinterpret_cast<DWORD>(managers[0]) && registers[7] == 0x66778899);
    assert(beforeFlags == afterFlags && *reinterpret_cast<int*>(managers[0] + 0x3DD8) == 40);
    owners.Release(managers[0], api);
    assert(owners.Acquire(managers[4], api));
    for (auto& manager : managers) owners.Release(manager, api);
    assert(buffers.empty() && heaps.empty());
    puts("Clothing capacity: rollback, independent owners, idempotent cleanup, 2000 x86 register/flags/stack checks passed");
}
