[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RunRoot,
    [Parameter(Mandatory)][ValidateRange(0,3)][int]$Instance,
    [Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$ExpectedPid,
    [ValidateSet(2,4)][int]$Instances = 4,
    [ValidateRange(10,3600)][int]$DurationSeconds = 900,
    [string]$GameRoot = 'C:\Program Files (x86)\Steam\steamapps\common\Dead Rising 2'
)

$ErrorActionPreference = 'Stop'
$run = (Resolve-Path -LiteralPath $RunRoot).Path
$workspace = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$allowed = [IO.Path]::GetFullPath((Join-Path $workspace 'runtime_logs\coop')) + '\'
if (-not $run.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase) -or $Instance -ge $Instances) {
    throw 'Invalid owned harness run or instance'
}
$description = Get-CimInstance Win32_Process -Filter "ProcessId=$ExpectedPid"
if ($null -eq $description -or $description.ExecutablePath -ne (Join-Path $GameRoot 'deadrising2.exe') -or
    $description.CommandLine -notmatch "(?i)(?:^|\s)-coopinstance=$Instance(?:\s|$)" -or
    $description.CommandLine -notmatch '(?i)(?:^|\s)-coopsilent(?:\s|$)' -or
    $description.CommandLine -notmatch '(?i)(?:^|\s)-coopprivatemouse(?:\s|$)') {
    throw 'Process is not the expected isolated harness instance'
}
$samples = @(Import-Csv -LiteralPath (Join-Path $run 'resources.csv') | Select-Object -Last $Instances)
$owned = @($samples | Where-Object { [int]$_.Instance -eq $Instance -and [int]$_.Pid -eq $ExpectedPid -and $_.Alive -eq 'true' })
if ($samples.Count -ne $Instances -or $owned.Count -ne 1 -or
    ([DateTimeOffset]::Now - [DateTimeOffset]::Parse($owned[0].Timestamp)).TotalSeconds -gt 10) {
    throw 'A fresh owned process sample is required'
}

$lockPath = Join-Path $run "interactive-leader.$Instance.lock"
$keyboardPath = Join-Path $GameRoot "coop_input.$Instance.txt"
$mousePath = Join-Path $GameRoot "coop_mouse.$Instance.txt"
if ((Test-Path -LiteralPath $lockPath) -or (Test-Path -LiteralPath $keyboardPath) -or
    (Test-Path -LiteralPath $mousePath)) {
    throw 'Interactive input or another owned command is already active'
}
$lock = [IO.File]::Open($lockPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
$bytes = [Text.Encoding]::ASCII.GetBytes("pid=$PID target=$ExpectedPid instance=$Instance")
$lock.Write($bytes, 0, $bytes.Length)
$lock.Dispose()

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class HarnessUserInput {
  public delegate bool EnumProc(IntPtr window, IntPtr parameter);
  [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int key);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point point);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rectangle);
}
'@

$window = [IntPtr]::Zero
[HarnessUserInput]::EnumWindows({
    param($candidate, $parameter)
    $processId = 0
    [void][HarnessUserInput]::GetWindowThreadProcessId($candidate, [ref]$processId)
    if ($processId -eq $ExpectedPid) { $script:window = $candidate; return $false }
    return $true
}, [IntPtr]::Zero) | Out-Null
if ($window -eq [IntPtr]::Zero) {
    Remove-Item -LiteralPath $lockPath -Force
    throw 'Owned harness window was not found'
}

# Virtual key to DirectInput scan code. F12 is reserved as the bridge release key.
$keyMap = [ordered]@{
    0x57=17; 0x41=30; 0x53=31; 0x44=32; 0x45=18; 0x51=16; 0x52=19; 0x46=33
    0x20=57; 0x10=42; 0x11=29; 0x1B=1; 0x09=15; 0x0D=28
    0x26=200; 0x28=208; 0x25=203; 0x27=205
}
$lastKeys = $null
$lastButtons = 0
$sequence = [uint32](([DateTimeOffset]::Now.ToUnixTimeMilliseconds() % 3000000000) + 1)
$started = [DateTimeOffset]::Now
$logPath = Join-Path $run 'interactive-leader.jsonl'

function Set-Keyboard([string]$value) {
    if ($value) {
        [IO.File]::WriteAllText($keyboardPath, $value, [Text.Encoding]::ASCII)
    } elseif (Test-Path -LiteralPath $keyboardPath) {
        Remove-Item -LiteralPath $keyboardPath -Force
    }
}

try {
    [IO.File]::AppendAllText($logPath, (@{event='started';time=$started.ToString('o');instance=$Instance;
        pid=$ExpectedPid;window=('0x{0:X}' -f $window.ToInt64());release_key='F12'} | ConvertTo-Json -Compress) + "`n")
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $DurationSeconds) {
        if (-not (Get-Process -Id $ExpectedPid -ErrorAction SilentlyContinue) -or
            (Test-Path -LiteralPath (Join-Path $run 'outcome.json'))) { break }
        if (([HarnessUserInput]::GetAsyncKeyState(0x7B) -band 0x8000) -ne 0) { break }
        $focused = [HarnessUserInput]::GetForegroundWindow() -eq $window
        $held = @()
        if ($focused) {
            foreach ($entry in $keyMap.GetEnumerator()) {
                if (([HarnessUserInput]::GetAsyncKeyState([int]$entry.Key) -band 0x8000) -ne 0) {
                    $held += [int]$entry.Value
                }
            }
        }
        $keys = ($held | Sort-Object -Unique) -join ' '
        if ($keys -ne $lastKeys) { Set-Keyboard $keys; $lastKeys = $keys }

        if ($focused) {
            $rectangle = [HarnessUserInput+Rect]::new()
            $point = [HarnessUserInput+Point]::new()
            if ([HarnessUserInput]::GetWindowRect($window, [ref]$rectangle) -and
                [HarnessUserInput]::GetCursorPos([ref]$point)) {
                $centerX = [int](($rectangle.L + $rectangle.R) / 2)
                $centerY = [int](($rectangle.T + $rectangle.B) / 2)
                $x = [Math]::Max(-120, [Math]::Min(120, $point.X - $centerX))
                $y = [Math]::Max(-120, [Math]::Min(120, $point.Y - $centerY))
                $buttons = 0
                if (([HarnessUserInput]::GetAsyncKeyState(0x01) -band 0x8000) -ne 0) { $buttons = $buttons -bor 1 }
                if (([HarnessUserInput]::GetAsyncKeyState(0x02) -band 0x8000) -ne 0) { $buttons = $buttons -bor 2 }
                if ($x -ne 0 -or $y -ne 0 -or $buttons -ne $lastButtons) {
                    $sequence++
                    [IO.File]::WriteAllText($mousePath, "$sequence $x $y 0 $buttons", [Text.Encoding]::ASCII)
                    $lastButtons = $buttons
                }
                [void][HarnessUserInput]::SetCursorPos($centerX, $centerY)
            }
        } elseif ($lastButtons -ne 0) {
            $sequence++
            [IO.File]::WriteAllText($mousePath, "$sequence 0 0 0 0", [Text.Encoding]::ASCII)
            $lastButtons = 0
        }
        Start-Sleep -Milliseconds 50
    }
} finally {
    Set-Keyboard ''
    if (Test-Path -LiteralPath $mousePath) { Remove-Item -LiteralPath $mousePath -Force }
    if (Test-Path -LiteralPath $lockPath) { Remove-Item -LiteralPath $lockPath -Force }
    [IO.File]::AppendAllText($logPath, (@{event='stopped';time=[DateTimeOffset]::Now.ToString('o');
        instance=$Instance;pid=$ExpectedPid} | ConvertTo-Json -Compress) + "`n")
}
