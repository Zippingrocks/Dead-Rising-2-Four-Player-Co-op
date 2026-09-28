using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;

internal static unsafe class Program
{
    private const uint DBG_CONTINUE = 0x00010002;
    private const uint DBG_EXCEPTION_NOT_HANDLED = 0x80010001;
    private const uint WAIT_TIMEOUT = 258;
    private const uint ERROR_SEM_TIMEOUT = 121;

    private const uint EXCEPTION_DEBUG_EVENT = 1;
    private const uint CREATE_THREAD_DEBUG_EVENT = 2;
    private const uint CREATE_PROCESS_DEBUG_EVENT = 3;
    private const uint EXIT_THREAD_DEBUG_EVENT = 4;
    private const uint EXIT_PROCESS_DEBUG_EVENT = 5;
    private const uint LOAD_DLL_DEBUG_EVENT = 6;
    private const uint UNLOAD_DLL_DEBUG_EVENT = 7;
    private const uint OUTPUT_DEBUG_STRING_EVENT = 8;
    private const uint RIP_EVENT = 9;

    private const uint EXCEPTION_BREAKPOINT = 0x80000003;
    private const uint EXCEPTION_SINGLE_STEP = 0x80000004;

    private const uint PROCESS_QUERY_INFORMATION = 0x0400;
    private const uint PROCESS_VM_READ = 0x0010;
    private const uint PROCESS_DUP_HANDLE = 0x0040;
    private const uint PROCESS_SUSPEND_RESUME = 0x0800;
    private const uint SYNCHRONIZE = 0x00100000;

    private static volatile bool stopRequested;

    public static int Main(string[] args)
    {
        try
        {
            var options = Options.Parse(args);
            if (options.Help)
            {
                Options.PrintUsage();
                return 0;
            }

            Directory.CreateDirectory(options.OutDir);

            if (options.ThreadsPath is not null)
            {
                ThreadSnapshot.Capture(options.Pid, options.ThreadsPath);
                Console.WriteLine(JsonSerializer.Serialize(new { type = "thread-snapshot", pid = options.Pid, path = options.ThreadsPath }));
                if (options.DumpPath is null) return 0;
            }

            if (options.DumpPath is not null)
            {
                Directory.CreateDirectory(Path.GetDirectoryName(options.DumpPath) ?? options.OutDir);
                var processHandle = OpenProcess(
                    PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE | PROCESS_SUSPEND_RESUME | SYNCHRONIZE,
                    false,
                    options.Pid);
                var ok = WriteDump(options.Pid, IntPtr.Zero, processHandle, options.DumpPath, out var dumpError);
                var length = ok && File.Exists(options.DumpPath) ? new FileInfo(options.DumpPath).Length : 0L;
                Console.WriteLine(JsonSerializer.Serialize(new
                {
                    type = "standalone-dump",
                    time = DateTimeOffset.Now,
                    pid = options.Pid,
                    dumpPath = options.DumpPath,
                    dumpType = DumpTypeLabel(),
                    saved = ok,
                    length,
                    error = ok ? 0 : dumpError
                }));
                CloseIfNonZero(processHandle);
                return ok ? 0 : 1;
            }
            using var log = new StreamWriter(Path.Combine(options.OutDir, "debug-events.jsonl"), append: false) { AutoFlush = true };

            Console.CancelKeyPress += (_, e) =>
            {
                e.Cancel = true;
                stopRequested = true;
                Console.WriteLine("Stop requested; detaching after current debug event.");
            };

            var started = DateTimeOffset.Now;
            WriteJson(log, new
            {
                type = "monitor-start",
                time = started,
                pid = options.Pid,
                outDir = options.OutDir,
                firstChanceDumpCodes = options.FirstChanceDumpCodes.Select(FormatCode).ToArray(),
                dumpOnAbnormalExit = options.DumpOnAbnormalExit,
                maxDumps = options.MaxDumps,
                dumpType = DumpTypeLabel()
            });

            Console.WriteLine($"DR2CrashDumpMonitor attaching to pid {options.Pid}");
            Console.WriteLine($"Output: {options.OutDir}");

            if (!DebugActiveProcess(options.Pid))
            {
                var error = Marshal.GetLastWin32Error();
                WriteJson(log, new { type = "attach-failed", time = DateTimeOffset.Now, error });
                Console.Error.WriteLine($"DebugActiveProcess failed with Win32 error {error}.");
                return 1;
            }

            DebugSetProcessKillOnExit(false);
            var attached = true;
            var dumpCount = 0;
            var processExitCode = 0u;
            var abnormalTeardownDumpAttempted = false;
            var abnormalTeardownDumpSucceeded = false;
            var debugProcessHandle = IntPtr.Zero;
            var fallbackProcessHandle = OpenProcess(
                PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE | PROCESS_SUSPEND_RESUME | SYNCHRONIZE,
                false,
                options.Pid);

            try
            {
                while (!stopRequested)
                {
                    if (!WaitForDebugEvent(out var e, 1000))
                    {
                        var error = Marshal.GetLastWin32Error();
                        if (error == WAIT_TIMEOUT || error == ERROR_SEM_TIMEOUT)
                        {
                            continue;
                        }

                        WriteJson(log, new { type = "wait-failed", time = DateTimeOffset.Now, error });
                        Console.Error.WriteLine($"WaitForDebugEvent failed with Win32 error {error}.");
                        break;
                    }

                    var continueStatus = DBG_CONTINUE;
                    var eventName = EventName(e.dwDebugEventCode);

                    switch (e.dwDebugEventCode)
                    {
                        case CREATE_PROCESS_DEBUG_EVENT:
                            debugProcessHandle = e.u.CreateProcessInfo.hProcess;
                            WriteJson(log, new
                            {
                                type = "debug-event",
                                time = DateTimeOffset.Now,
                                eventName,
                                e.dwProcessId,
                                e.dwThreadId,
                                baseOfImage = Ptr(e.u.CreateProcessInfo.lpBaseOfImage),
                                startAddress = Ptr(e.u.CreateProcessInfo.lpStartAddress)
                            });
                            CloseIfNonZero(e.u.CreateProcessInfo.hFile);
                            CloseIfNonZero(e.u.CreateProcessInfo.hThread);
                            break;

                        case CREATE_THREAD_DEBUG_EVENT:
                            WriteJson(log, new
                            {
                                type = "debug-event",
                                time = DateTimeOffset.Now,
                                eventName,
                                e.dwProcessId,
                                e.dwThreadId,
                                startAddress = Ptr(e.u.CreateThread.lpStartAddress)
                            });
                            CloseIfNonZero(e.u.CreateThread.hThread);
                            break;

                        case LOAD_DLL_DEBUG_EVENT:
                            WriteJson(log, new
                            {
                                type = "debug-event",
                                time = DateTimeOffset.Now,
                                eventName,
                                e.dwProcessId,
                                e.dwThreadId,
                                baseOfDll = Ptr(e.u.LoadDll.lpBaseOfDll)
                            });
                            CloseIfNonZero(e.u.LoadDll.hFile);
                            break;

                        case EXIT_PROCESS_DEBUG_EVENT:
                            processExitCode = e.u.ExitProcess.dwExitCode;
                            var abnormalExit = IsAbnormalExitCode(processExitCode);
                            string? exitDumpPath = null;
                            var exitDumpOk = false;
                            var exitDumpError = 0;
                            var exitDumpAttempted = false;

                            if (options.DumpOnAbnormalExit && dumpCount < options.MaxDumps && abnormalExit && !abnormalTeardownDumpSucceeded)
                            {
                                exitDumpAttempted = true;
                                exitDumpPath = Path.Combine(
                                    options.OutDir,
                                    $"deadrising2_pid{options.Pid}_{DateTime.Now:yyyyMMdd_HHmmss_fff}_exitcode_{processExitCode:x8}.dmp");
                                // The debug-event process handle can become unusable during teardown; retain the independently opened handle for exit dumps.
                                exitDumpOk = WriteDump(options.Pid, IntPtr.Zero, fallbackProcessHandle, exitDumpPath, out exitDumpError);
                                if (exitDumpOk) dumpCount++;
                                Console.WriteLine(exitDumpOk
                                    ? $"Abnormal-exit dump written: {exitDumpPath}"
                                    : $"Abnormal-exit dump write failed with Win32 error {exitDumpError}: {exitDumpPath}");
                            }

                            WriteJson(log, new
                            {
                                type = "debug-event",
                                time = DateTimeOffset.Now,
                                eventName,
                                e.dwProcessId,
                                e.dwThreadId,
                                exitCode = $"0x{processExitCode:x8}",
                                abnormalExit,
                                dumped = exitDumpOk,
                                dumpCount,
                                dumpPath = exitDumpPath,
                                dumpError = exitDumpAttempted && !exitDumpOk ? (int?)exitDumpError : null
                            });
                            ContinueDebugEvent(e.dwProcessId, e.dwThreadId, continueStatus);
                            attached = false;
                            goto done;

                        case EXIT_THREAD_DEBUG_EVENT:
                            var threadExitCode = e.u.ExitThread.dwExitCode;
                            var abnormalThreadExit = IsAbnormalExitCode(threadExitCode);
                            string? threadExitDumpPath = null;
                            var threadExitDumpOk = false;
                            var threadExitDumpError = 0;

                            // Some WER teardowns emit every thread exit but never deliver EXIT_PROCESS before the watcher grace period ends.
                            if (options.DumpOnAbnormalExit && dumpCount < options.MaxDumps && abnormalThreadExit && !abnormalTeardownDumpAttempted)
                            {
                                abnormalTeardownDumpAttempted = true;
                                threadExitDumpPath = Path.Combine(
                                    options.OutDir,
                                    $"deadrising2_pid{options.Pid}_{DateTime.Now:yyyyMMdd_HHmmss_fff}_threadexit_{threadExitCode:x8}.dmp");
                                threadExitDumpOk = WriteDump(options.Pid, debugProcessHandle, fallbackProcessHandle, threadExitDumpPath, out threadExitDumpError);
                                abnormalTeardownDumpSucceeded = threadExitDumpOk;
                                if (threadExitDumpOk) dumpCount++;
                                Console.WriteLine(threadExitDumpOk
                                    ? $"Abnormal-thread-exit dump written: {threadExitDumpPath}"
                                    : $"Abnormal-thread-exit dump write failed with Win32 error {threadExitDumpError}: {threadExitDumpPath}");
                            }

                            WriteJson(log, new
                            {
                                type = "debug-event",
                                time = DateTimeOffset.Now,
                                eventName,
                                e.dwProcessId,
                                e.dwThreadId,
                                exitCode = $"0x{threadExitCode:x8}",
                                abnormalExit = abnormalThreadExit,
                                dumped = threadExitDumpOk,
                                dumpCount,
                                dumpPath = threadExitDumpPath,
                                dumpError = abnormalThreadExit && abnormalTeardownDumpAttempted && !abnormalTeardownDumpSucceeded ? (int?)threadExitDumpError : null
                            });
                            break;

                        case UNLOAD_DLL_DEBUG_EVENT:
                            WriteJson(log, new
                            {
                                type = "debug-event",
                                time = DateTimeOffset.Now,
                                eventName,
                                e.dwProcessId,
                                e.dwThreadId,
                                baseOfDll = Ptr(e.u.UnloadDll.lpBaseOfDll)
                            });
                            break;

                        case OUTPUT_DEBUG_STRING_EVENT:
                            var debugStringText = TryReadDebugString(
                                debugProcessHandle != IntPtr.Zero ? debugProcessHandle : fallbackProcessHandle,
                                e.u.DebugString.lpDebugStringData,
                                e.u.DebugString.nDebugStringLength,
                                e.u.DebugString.fUnicode != 0);
                            WriteJson(log, new
                            {
                                type = "debug-event",
                                time = DateTimeOffset.Now,
                                eventName,
                                e.dwProcessId,
                                e.dwThreadId,
                                length = e.u.DebugString.nDebugStringLength,
                                unicode = e.u.DebugString.fUnicode != 0,
                                text = debugStringText
                            });
                            break;

                        case RIP_EVENT:
                            WriteJson(log, new
                            {
                                type = "debug-event",
                                time = DateTimeOffset.Now,
                                eventName,
                                e.dwProcessId,
                                e.dwThreadId,
                                error = e.u.RipInfo.dwError,
                                typeCode = e.u.RipInfo.dwType
                            });
                            break;

                        case EXCEPTION_DEBUG_EVENT:
                            var r = e.u.Exception.ExceptionRecord;
                            var firstChance = e.u.Exception.dwFirstChance != 0;
                            var exceptionInformation = GetExceptionInformation(r);
                            var shouldDump = dumpCount < options.MaxDumps && (!firstChance || options.FirstChanceDumpCodes.Contains(r.ExceptionCode));
                            string? dumpPath = null;
                            var dumpOk = false;
                            var dumpError = 0;

                            if (shouldDump)
                            {
                                dumpPath = Path.Combine(
                                    options.OutDir,
                                    $"deadrising2_pid{options.Pid}_{DateTime.Now:yyyyMMdd_HHmmss_fff}_{(firstChance ? "firstchance" : "secondchance")}_{r.ExceptionCode:x8}.dmp");
                                dumpOk = WriteDump(options.Pid, debugProcessHandle, fallbackProcessHandle, dumpPath, out dumpError);
                                if (dumpOk) dumpCount++;
                                Console.WriteLine(dumpOk
                                    ? $"Dump written: {dumpPath}"
                                    : $"Dump write failed with Win32 error {dumpError}: {dumpPath}");
                            }

                            WriteJson(log, new
                            {
                                type = "exception",
                                time = DateTimeOffset.Now,
                                e.dwProcessId,
                                e.dwThreadId,
                                firstChance,
                                exceptionCode = FormatCode(r.ExceptionCode),
                                exceptionFlags = $"0x{r.ExceptionFlags:x8}",
                                address = Ptr(r.ExceptionAddress),
                                parameterCount = r.NumberParameters,
                                exceptionInformation,
                                dumped = dumpOk,
                                dumpCount,
                                dumpPath,
                                dumpError = shouldDump && !dumpOk ? (int?)dumpError : null
                            });

                            continueStatus = firstChance && (r.ExceptionCode == EXCEPTION_BREAKPOINT || r.ExceptionCode == EXCEPTION_SINGLE_STEP)
                                ? DBG_CONTINUE
                                : DBG_EXCEPTION_NOT_HANDLED;
                            break;

                        default:
                            WriteJson(log, new
                            {
                                type = "debug-event",
                                time = DateTimeOffset.Now,
                                eventName,
                                e.dwProcessId,
                                e.dwThreadId
                            });
                            break;
                    }

                    if (!ContinueDebugEvent(e.dwProcessId, e.dwThreadId, continueStatus))
                    {
                        var error = Marshal.GetLastWin32Error();
                        WriteJson(log, new { type = "continue-failed", time = DateTimeOffset.Now, error, e.dwProcessId, e.dwThreadId });
                        Console.Error.WriteLine($"ContinueDebugEvent failed with Win32 error {error}.");
                        break;
                    }
                }

done:
                var ended = DateTimeOffset.Now;
                WriteJson(log, new
                {
                    type = "monitor-end",
                    time = ended,
                    elapsedSeconds = Math.Round((ended - started).TotalSeconds, 3),
                    dumpCount,
                    processExitCode = $"0x{processExitCode:x8}"
                });
                Console.WriteLine($"Monitor ended. Dump count: {dumpCount}. Exit code: 0x{processExitCode:x8}");
                return 0;
            }
            finally
            {
                if (attached)
                {
                    DebugActiveProcessStop(options.Pid);
                }

                CloseIfNonZero(fallbackProcessHandle);
                if (debugProcessHandle != fallbackProcessHandle)
                {
                    CloseIfNonZero(debugProcessHandle);
                }
            }
        }
        catch (Exception ex) when (ex is ArgumentException or FormatException)
        {
            Console.Error.WriteLine(ex.Message);
            Options.PrintUsage();
            return 2;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(ex);
            return 1;
        }
    }

    private static bool IsAbnormalExitCode(uint exitCode)
    {
        if (exitCode == 0 || exitCode == 0x00000103) return false;
        return exitCode >= 0x80000000;
    }

    private static MINIDUMP_TYPE FullDumpType() =>
        MINIDUMP_TYPE.MiniDumpWithDataSegs |
        MINIDUMP_TYPE.MiniDumpWithFullMemory |
        MINIDUMP_TYPE.MiniDumpWithHandleData |
        MINIDUMP_TYPE.MiniDumpWithUnloadedModules |
        MINIDUMP_TYPE.MiniDumpWithIndirectlyReferencedMemory |
        MINIDUMP_TYPE.MiniDumpWithProcessThreadData |
        MINIDUMP_TYPE.MiniDumpWithPrivateReadWriteMemory |
        MINIDUMP_TYPE.MiniDumpWithFullMemoryInfo |
        MINIDUMP_TYPE.MiniDumpWithThreadInfo |
        MINIDUMP_TYPE.MiniDumpWithCodeSegs |
        MINIDUMP_TYPE.MiniDumpIgnoreInaccessibleMemory;

    private static string DumpTypeLabel() => FullDumpType().ToString();

    private static List<string> GetExceptionInformation(EXCEPTION_RECORD record)
    {
        var result = new List<string>();
        var count = Math.Min((int)record.NumberParameters, 15);
        for (var i = 0; i < count; i++)
        {
            result.Add($"0x{record.ExceptionInformation[i]:x}");
        }

        return result;
    }

    private static string? TryReadDebugString(IntPtr processHandle, IntPtr address, ushort length, bool unicode)
    {
        if (processHandle == IntPtr.Zero || address == IntPtr.Zero || length == 0) return null;

        var byteCount = unicode ? Math.Min((int)length * 2, 32768) : Math.Min((int)length, 32768);
        var buffer = new byte[byteCount];
        if (!ReadProcessMemory(processHandle, address, buffer, new IntPtr(buffer.Length), out var bytesReadPtr)) return null;

        var bytesRead = Math.Max(0, Math.Min(bytesReadPtr.ToInt32(), buffer.Length));
        if (bytesRead == 0) return string.Empty;

        var text = unicode
            ? Encoding.Unicode.GetString(buffer, 0, bytesRead)
            : Encoding.Default.GetString(buffer, 0, bytesRead);
        return text.TrimEnd('\0', '\r', '\n');
    }

    private static bool WriteDump(uint pid, IntPtr debugProcessHandle, IntPtr fallbackProcessHandle, string path, out int error)
    {
        var processHandle = debugProcessHandle != IntPtr.Zero ? debugProcessHandle : fallbackProcessHandle;
        if (processHandle == IntPtr.Zero)
        {
            processHandle = OpenProcess(
                PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE | PROCESS_SUSPEND_RESUME | SYNCHRONIZE,
                false,
                pid);
        }

        if (processHandle == IntPtr.Zero)
        {
            error = Marshal.GetLastWin32Error();
            return false;
        }

        using var stream = new FileStream(path, FileMode.Create, FileAccess.ReadWrite, FileShare.None);
        var dumpType = FullDumpType();

        if (MiniDumpWriteDump(processHandle, pid, stream.SafeFileHandle.DangerousGetHandle(), dumpType, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero))
        {
            error = 0;
            return true;
        }

        error = Marshal.GetLastWin32Error();
        return false;
    }

    private static void WriteJson(StreamWriter log, object value) => log.WriteLine(JsonSerializer.Serialize(value));
    private static string FormatCode(uint code) => $"0x{code:x8}";
    private static string Ptr(IntPtr ptr) => $"0x{ptr.ToInt64():x}";

    private static string EventName(uint code) => code switch
    {
        EXCEPTION_DEBUG_EVENT => "EXCEPTION_DEBUG_EVENT",
        CREATE_THREAD_DEBUG_EVENT => "CREATE_THREAD_DEBUG_EVENT",
        CREATE_PROCESS_DEBUG_EVENT => "CREATE_PROCESS_DEBUG_EVENT",
        EXIT_THREAD_DEBUG_EVENT => "EXIT_THREAD_DEBUG_EVENT",
        EXIT_PROCESS_DEBUG_EVENT => "EXIT_PROCESS_DEBUG_EVENT",
        LOAD_DLL_DEBUG_EVENT => "LOAD_DLL_DEBUG_EVENT",
        UNLOAD_DLL_DEBUG_EVENT => "UNLOAD_DLL_DEBUG_EVENT",
        OUTPUT_DEBUG_STRING_EVENT => "OUTPUT_DEBUG_STRING_EVENT",
        RIP_EVENT => "RIP_EVENT",
        _ => $"UNKNOWN_{code}"
    };

    private static void CloseIfNonZero(IntPtr handle)
    {
        if (handle != IntPtr.Zero && handle != new IntPtr(-1))
        {
            CloseHandle(handle);
        }
    }

    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool DebugActiveProcess(uint processId);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool DebugActiveProcessStop(uint processId);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool DebugSetProcessKillOnExit(bool killOnExit);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool WaitForDebugEvent(out DEBUG_EVENT debugEvent, uint milliseconds);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool ContinueDebugEvent(uint processId, uint threadId, uint continueStatus);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern IntPtr OpenProcess(uint desiredAccess, bool inheritHandle, uint processId);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool ReadProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, IntPtr nSize, out IntPtr lpNumberOfBytesRead);

    [DllImport("dbghelp.dll", SetLastError = true)]
    private static extern bool MiniDumpWriteDump(
        IntPtr processHandle,
        uint processId,
        IntPtr fileHandle,
        MINIDUMP_TYPE dumpType,
        IntPtr exceptionParam,
        IntPtr userStreamParam,
        IntPtr callbackParam);

    [StructLayout(LayoutKind.Sequential)]
    private struct DEBUG_EVENT
    {
        public uint dwDebugEventCode;
        public uint dwProcessId;
        public uint dwThreadId;
        public DEBUG_EVENT_UNION u;
    }

    [StructLayout(LayoutKind.Explicit)]
    private struct DEBUG_EVENT_UNION
    {
        [FieldOffset(0)] public EXCEPTION_DEBUG_INFO Exception;
        [FieldOffset(0)] public CREATE_THREAD_DEBUG_INFO CreateThread;
        [FieldOffset(0)] public CREATE_PROCESS_DEBUG_INFO CreateProcessInfo;
        [FieldOffset(0)] public EXIT_THREAD_DEBUG_INFO ExitThread;
        [FieldOffset(0)] public EXIT_PROCESS_DEBUG_INFO ExitProcess;
        [FieldOffset(0)] public LOAD_DLL_DEBUG_INFO LoadDll;
        [FieldOffset(0)] public UNLOAD_DLL_DEBUG_INFO UnloadDll;
        [FieldOffset(0)] public OUTPUT_DEBUG_STRING_INFO DebugString;
        [FieldOffset(0)] public RIP_INFO RipInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct EXCEPTION_DEBUG_INFO
    {
        public EXCEPTION_RECORD ExceptionRecord;
        public uint dwFirstChance;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct EXCEPTION_RECORD
    {
        public uint ExceptionCode;
        public uint ExceptionFlags;
        public IntPtr ExceptionRecord;
        public IntPtr ExceptionAddress;
        public uint NumberParameters;
        public fixed ulong ExceptionInformation[15];
    }

    [StructLayout(LayoutKind.Sequential)] private struct CREATE_THREAD_DEBUG_INFO { public IntPtr hThread; public IntPtr lpThreadLocalBase; public IntPtr lpStartAddress; }
    [StructLayout(LayoutKind.Sequential)] private struct EXIT_THREAD_DEBUG_INFO { public uint dwExitCode; }
    [StructLayout(LayoutKind.Sequential)] private struct EXIT_PROCESS_DEBUG_INFO { public uint dwExitCode; }
    [StructLayout(LayoutKind.Sequential)] private struct UNLOAD_DLL_DEBUG_INFO { public IntPtr lpBaseOfDll; }
    [StructLayout(LayoutKind.Sequential)] private struct OUTPUT_DEBUG_STRING_INFO { public IntPtr lpDebugStringData; public ushort fUnicode; public ushort nDebugStringLength; }
    [StructLayout(LayoutKind.Sequential)] private struct RIP_INFO { public uint dwError; public uint dwType; }

    [StructLayout(LayoutKind.Sequential)]
    private struct CREATE_PROCESS_DEBUG_INFO
    {
        public IntPtr hFile;
        public IntPtr hProcess;
        public IntPtr hThread;
        public IntPtr lpBaseOfImage;
        public uint dwDebugInfoFileOffset;
        public uint nDebugInfoSize;
        public IntPtr lpThreadLocalBase;
        public IntPtr lpStartAddress;
        public IntPtr lpImageName;
        public ushort fUnicode;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct LOAD_DLL_DEBUG_INFO
    {
        public IntPtr hFile;
        public IntPtr lpBaseOfDll;
        public uint dwDebugInfoFileOffset;
        public uint nDebugInfoSize;
        public IntPtr lpImageName;
        public ushort fUnicode;
    }

    [Flags]
    private enum MINIDUMP_TYPE : uint
    {
        MiniDumpWithDataSegs = 0x00000001,
        MiniDumpWithFullMemory = 0x00000002,
        MiniDumpWithHandleData = 0x00000004,
        MiniDumpWithUnloadedModules = 0x00000020,
        MiniDumpWithIndirectlyReferencedMemory = 0x00000040,
        MiniDumpWithProcessThreadData = 0x00000100,
        MiniDumpWithPrivateReadWriteMemory = 0x00000200,
        MiniDumpWithFullMemoryInfo = 0x00000800,
        MiniDumpWithThreadInfo = 0x00001000,
        MiniDumpWithCodeSegs = 0x00002000,
        MiniDumpIgnoreInaccessibleMemory = 0x00020000
    }

    private sealed class Options
    {
        private Options(uint pid, string outDir, HashSet<uint> firstChanceDumpCodes, bool dumpOnAbnormalExit, int maxDumps, string? dumpPath, bool help)
        {
            Pid = pid;
            OutDir = outDir;
            FirstChanceDumpCodes = firstChanceDumpCodes;
            DumpOnAbnormalExit = dumpOnAbnormalExit;
            MaxDumps = maxDumps;
            DumpPath = dumpPath;
            Help = help;
        }

        public uint Pid { get; }
        public string OutDir { get; }
        public HashSet<uint> FirstChanceDumpCodes { get; }
        public bool DumpOnAbnormalExit { get; }
        public int MaxDumps { get; }
        public string? DumpPath { get; }
        public string? ThreadsPath { get; private set; }
        public bool Help { get; }

        public static Options Parse(string[] args)
        {
            uint pid = 0;
            string? outDir = null;
            var firstChance = new HashSet<uint> { 0xc0000409 };
            var dumpOnAbnormalExit = true;
            var maxDumps = 3;
            string? dumpPath = null;
            string? threadsPath = null;

            for (var i = 0; i < args.Length; i++)
            {
                switch (args[i])
                {
                    case "-h":
                    case "--help":
                        return new Options(0, "", firstChance, dumpOnAbnormalExit, maxDumps, null, true);
                    case "--pid":
                        pid = ParseUInt(Next(args, ref i, "--pid"));
                        break;
                    case "--out-dir":
                        outDir = Next(args, ref i, "--out-dir");
                        break;
                    case "--dump-path":
                        dumpPath = Next(args, ref i, "--dump-path");
                        break;
                    case "--threads-path":
                        threadsPath = Next(args, ref i, "--threads-path");
                        break;
                    case "--first-chance":
                        firstChance = ParseCodes(Next(args, ref i, "--first-chance"));
                        break;
                    case "--no-exit-dump":
                        dumpOnAbnormalExit = false;
                        break;
                    case "--max-dumps":
                        maxDumps = Math.Max(1, (int)ParseUInt(Next(args, ref i, "--max-dumps")));
                        break;
                    default:
                        throw new ArgumentException($"Unknown argument: {args[i]}");
                }
            }

            if (pid == 0) throw new ArgumentException("--pid is required.");
            var resolvedDumpPath = string.IsNullOrWhiteSpace(dumpPath) ? null : Path.GetFullPath(dumpPath);
            var resolvedThreadsPath = string.IsNullOrWhiteSpace(threadsPath) ? null : Path.GetFullPath(threadsPath);
            if (string.IsNullOrWhiteSpace(outDir))
            {
                if (resolvedDumpPath is null && resolvedThreadsPath is null) throw new ArgumentException("--out-dir is required unless --dump-path or --threads-path is provided.");
                outDir = Path.GetDirectoryName(resolvedDumpPath ?? resolvedThreadsPath!) ?? Directory.GetCurrentDirectory();
            }
            return new Options(pid, Path.GetFullPath(outDir), firstChance, dumpOnAbnormalExit, maxDumps, resolvedDumpPath, false)
                { ThreadsPath = resolvedThreadsPath };
        }

        public static void PrintUsage()
        {
            Console.WriteLine("Usage: DR2CrashDumpMonitor --pid <process-id> --out-dir <directory> [--first-chance 0xc0000409,0xc0000005] [--max-dumps 3] [--no-exit-dump]");
            Console.WriteLine("       DR2CrashDumpMonitor --pid <process-id> --dump-path <file.dmp>");
            Console.WriteLine("       DR2CrashDumpMonitor --pid <process-id> --threads-path <file.json>");
            Console.WriteLine("Writes debug-events.jsonl plus maximum-detail dumps on second-chance exceptions, configured first-chance crash codes, and abnormal process exits by default.");
            Console.WriteLine("--dump-path writes one maximum-detail dump without attaching as a debugger, for hang snapshots.");
        }

        private static string Next(string[] args, ref int index, string name)
        {
            index++;
            if (index >= args.Length) throw new ArgumentException($"{name} expects a value.");
            return args[index];
        }

        private static uint ParseUInt(string value)
        {
            if (uint.TryParse(value, out var parsed)) return parsed;
            if (value.StartsWith("0x", StringComparison.OrdinalIgnoreCase) && uint.TryParse(value[2..], System.Globalization.NumberStyles.HexNumber, null, out parsed)) return parsed;
            throw new ArgumentException($"Invalid unsigned integer: {value}");
        }

        private static HashSet<uint> ParseCodes(string value)
        {
            var result = new HashSet<uint>();
            foreach (var part in value.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
            {
                if (int.TryParse(part, out var signedDecimal))
                {
                    result.Add(unchecked((uint)signedDecimal));
                    continue;
                }

                var hex = part.StartsWith("0x", StringComparison.OrdinalIgnoreCase) ? part[2..] : part;
                if (!uint.TryParse(hex, System.Globalization.NumberStyles.HexNumber, null, out var code)) throw new ArgumentException($"Invalid exception code: {part}");
                result.Add(code);
            }
            return result;
        }
    }
}
