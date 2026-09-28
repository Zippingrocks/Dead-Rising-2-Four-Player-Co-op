import ctypes
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32


PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400


def main():
    pid = int(sys.argv[1], 0)
    start = int(sys.argv[2], 0)
    end = int(sys.argv[3], 0)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    handle = kernel32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        buffer = ctypes.create_string_buffer(end - start)
        read = ctypes.c_size_t()
        if not kernel32.ReadProcessMemory(handle, ctypes.c_void_p(start), buffer, len(buffer), ctypes.byref(read)):
            raise ctypes.WinError(ctypes.get_last_error())
        disassembler = Cs(CS_ARCH_X86, CS_MODE_32)
        for instruction in disassembler.disasm(buffer.raw[:read.value], start):
            if instruction.address >= end:
                break
            print(f"{instruction.address:08X}  {instruction.mnemonic:<8} {instruction.op_str}")
    finally:
        kernel32.CloseHandle(handle)


if __name__ == "__main__":
    main()
