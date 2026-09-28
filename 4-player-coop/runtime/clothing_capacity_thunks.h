#pragma once

namespace coop_clothing {
int __cdecl ParentHeap(void* manager, int player);
void __cdecl DestroyParents(void* manager);

// Replaces MOV EDI,[EBP+EBX*4+3DD0]. The following JE consumes preexisting flags.
__declspec(naked) inline void ParentHeapThunk() {
    __asm {
        pushfd
        pushad
        push ebx
        push ebp
        call ParentHeap
        add esp, 8
        mov [esp], eax
        popad
        popfd
        ret
    }
}

// Runs only after the native destructor has released all four sets of child heaps.
__declspec(naked) inline void DestroyParentsThunk() {
    __asm {
        pushfd
        pushad
        push esi
        call DestroyParents
        add esp, 4
        popad
        popfd
        mov eax, [esi + 3DD0h]
        ret
    }
}
}
