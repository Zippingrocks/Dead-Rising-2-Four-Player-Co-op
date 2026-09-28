using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json;

internal static class ThreadSnapshot
{
    [StructLayout(LayoutKind.Explicit, Size = 716)]
    private struct X86Context
    {
        [FieldOffset(0)] public uint Flags;
        [FieldOffset(156)] public uint Edi;
        [FieldOffset(160)] public uint Esi;
        [FieldOffset(164)] public uint Ebx;
        [FieldOffset(168)] public uint Edx;
        [FieldOffset(172)] public uint Ecx;
        [FieldOffset(176)] public uint Eax;
        [FieldOffset(180)] public uint Ebp;
        [FieldOffset(184)] public uint Eip;
        [FieldOffset(196)] public uint Esp;
    }

    [DllImport("kernel32.dll", SetLastError = true)] private static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern IntPtr OpenThread(uint access, bool inherit, uint tid);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern uint Wow64SuspendThread(IntPtr thread);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern uint ResumeThread(IntPtr thread);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool Wow64GetThreadContext(IntPtr thread, ref X86Context context);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool IsWow64Process(IntPtr process, out bool wow64);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] buffer, IntPtr size, out IntPtr read);
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);

    public static void Capture(uint pid, string path)
    {
        using var process = Process.GetProcessById(checked((int)pid));
        var handle = OpenProcess(0x0410, false, pid);
        if (handle == IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        try
        {
            if (!IsWow64Process(handle, out var wow64) || !wow64)
                throw new InvalidOperationException("Thread snapshot requires a 32-bit WOW64 target.");
            var modules = process.Modules.Cast<ProcessModule>().Select(module => new
            {
                name = module.ModuleName,
                start = unchecked((ulong)module.BaseAddress.ToInt64()),
                size = (ulong)module.ModuleMemorySize
            }).ToArray();
            string Locate(uint address)
            {
                var module = modules.FirstOrDefault(value => address >= value.start && address - value.start < value.size);
                return module is null ? $"0x{address:X8}" : $"{module.name}+0x{address - module.start:X}";
            }
            var records = new List<object>();
            foreach (ProcessThread processThread in process.Threads)
            {
                var thread = OpenThread(0x004A, false, unchecked((uint)processThread.Id));
                var suspended = false;
                object? record = null;
                uint resumeResult = uint.MaxValue;
                try
                {
                    if (thread == IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
                    if (Wow64SuspendThread(thread) == uint.MaxValue) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
                    suspended = true;
                    var context = new X86Context { Flags = 0x00010007 };
                    if (!Wow64GetThreadContext(thread, ref context)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
                    var stack = new byte[4096];
                    // Near a stack's upper boundary a single 4 KB read can fail entirely.
                    // Read page-bounded pieces so valid return addresses are still retained.
                    var length = 0;
                    while (length < stack.Length)
                    {
                        var address = (long)context.Esp + length;
                        var size = Math.Min(stack.Length - length, 4096 - (int)(address & 4095));
                        var part = new byte[size];
                        ReadProcessMemory(handle, new IntPtr(address), part, new IntPtr(size), out var read);
                        var received = (int)Math.Clamp(read.ToInt64(), 0, size);
                        if (received == 0) break;
                        Array.Copy(part, 0, stack, length, received);
                        length += received;
                        if (received != size) break;
                    }
                    var hints = new List<object>();
                    for (var offset = 0; offset + 4 <= length; offset += 4)
                    {
                        var value = BitConverter.ToUInt32(stack, offset);
                        var location = Locate(value);
                        if (offset < 64 || !location.StartsWith("0x", StringComparison.Ordinal))
                            hints.Add(new { stackOffset = offset, value = $"0x{value:X8}", location });
                    }
                    record = new
                    {
                        threadId = processThread.Id, architecture = "x86", instruction = Locate(context.Eip),
                        registers = new
                        {
                            eip = $"0x{context.Eip:X8}", esp = $"0x{context.Esp:X8}", ebp = $"0x{context.Ebp:X8}",
                            eax = $"0x{context.Eax:X8}", ebx = $"0x{context.Ebx:X8}", ecx = $"0x{context.Ecx:X8}",
                            edx = $"0x{context.Edx:X8}", esi = $"0x{context.Esi:X8}", edi = $"0x{context.Edi:X8}"
                        },
                        stackBytesRead = length, stackAddressHints = hints
                    };
                }
                catch (Exception exception)
                {
                    record = new { threadId = processThread.Id, error = exception.Message };
                }
                finally
                {
                    // Balance exactly our own suspension, including every error path.
                    if (suspended) resumeResult = ResumeThread(thread);
                    if (thread != IntPtr.Zero) CloseHandle(thread);
                }
                records.Add(new { capture = record, resumed = !suspended || resumeResult != uint.MaxValue });
            }
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, JsonSerializer.Serialize(new
            {
                pid, timestamp = DateTimeOffset.Now,
                note = "Per-thread WOW64 contexts. Stack-address hints are not an unwound call stack. Threads are sampled sequentially, not atomically.",
                modules, threads = records
            }, new JsonSerializerOptions { WriteIndented = true }));
        }
        finally { CloseHandle(handle); }
    }
}
