[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RunRoot,
    [Parameter(Mandatory)][int[]]$OwnedPids,
    [ValidateRange(0,3)][int]$InitialInstance = 0,
    [ValidateRange(10,3600)][int]$DurationSeconds = 1800,
    [string]$GameRoot = 'C:\Program Files (x86)\Steam\steamapps\common\Dead Rising 2'
)

$ErrorActionPreference = 'Stop'
$instances = 4
if ($OwnedPids.Count -ne $instances -or @($OwnedPids | Select-Object -Unique).Count -ne $instances -or
    @($OwnedPids | Where-Object { $_ -le 0 }).Count) {
    throw 'Four distinct owned PIDs in session order are required'
}
$run = (Resolve-Path -LiteralPath $RunRoot).Path
$workspace = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$allowed = [IO.Path]::GetFullPath((Join-Path $workspace 'runtime_logs\coop')) + '\'
if (-not $run.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Run must belong to this workspace harness'
}
$samples = @(Import-Csv -LiteralPath (Join-Path $run 'resources.csv') | Select-Object -Last $instances)
if ($samples.Count -ne $instances -or
    @($samples | Where-Object { $_.Alive -ne 'true' -or
        ([DateTimeOffset]::Now - [DateTimeOffset]::Parse($_.Timestamp)).TotalSeconds -gt 10 }).Count) {
    throw 'Fresh live samples for all four owned processes are required'
}
foreach ($instance in 0..3) {
    $row = @($samples | Where-Object { [int]$_.Instance -eq $instance })
    $description = Get-CimInstance Win32_Process -Filter "ProcessId=$($OwnedPids[$instance])"
    if ($row.Count -ne 1 -or [int]$row[0].Pid -ne $OwnedPids[$instance] -or $null -eq $description -or
        $description.ExecutablePath -ne (Join-Path $GameRoot 'deadrising2.exe') -or
        $description.CommandLine -notmatch "(?i)(?:^|\s)-coopinstance=$instance(?:\s|$)" -or
        $description.CommandLine -notmatch '(?i)(?:^|\s)-coopsilent(?:\s|$)' -or
        $description.CommandLine -notmatch '(?i)(?:^|\s)-coopprivatemouse(?:\s|$)') {
        throw "Owned process validation failed for instance $instance"
    }
}
$lockPath = Join-Path $run 'interactive-player-switch.lock'
if (Test-Path -LiteralPath $lockPath) { throw 'A player switch bridge is already active' }
foreach ($instance in 0..3) {
    if ((Test-Path -LiteralPath (Join-Path $GameRoot "coop_input.$instance.txt")) -or
        (Test-Path -LiteralPath (Join-Path $GameRoot "coop_mouse.$instance.txt"))) {
        throw "Owned input command already exists for instance $instance"
    }
}
$lock = [IO.File]::Open($lockPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
$lockBytes = [Text.Encoding]::ASCII.GetBytes("pid=$PID pids=$($OwnedPids -join ',')")
$lock.Write($lockBytes, 0, $lockBytes.Length)
$lock.Dispose()

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class HarnessPlayerSwitch {
  public delegate bool EnumProc(IntPtr window, IntPtr parameter);
  [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
  [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")] public static extern IntPtr GetWindowLongPtr(IntPtr window, int index);
  [DllImport("user32.dll", EntryPoint="SetWindowLongPtrW")] public static extern IntPtr SetWindowLongPtr(IntPtr window, int index, IntPtr value);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr window);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
  [DllImport("user32.dll")] public static extern IntPtr SetActiveWindow(IntPtr window);
  [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr window);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool SetWindowText(IntPtr window, string text);
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint source, uint target, bool attach);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int key);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point point);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rectangle);
}
'@

$script:windows = @([IntPtr]::Zero, [IntPtr]::Zero, [IntPtr]::Zero, [IntPtr]::Zero)
[HarnessPlayerSwitch]::EnumWindows({
    param($candidate, $parameter)
    $processId = 0
    [void][HarnessPlayerSwitch]::GetWindowThreadProcessId($candidate, [ref]$processId)
    for ($instance = 0; $instance -lt 4; $instance++) {
        if ($processId -eq $OwnedPids[$instance]) { $script:windows[$instance] = $candidate; break }
    }
    return $true
}, [IntPtr]::Zero) | Out-Null
if (@($script:windows | Where-Object { $_ -eq [IntPtr]::Zero }).Count) {
    Remove-Item -LiteralPath $lockPath -Force
    throw 'One or more owned harness windows were not found'
}

$keyMap = [ordered]@{
    0x57=17; 0x41=30; 0x53=31; 0x44=32; 0x45=18; 0x51=16; 0x52=19; 0x46=33
    0x20=57; 0x10=42; 0x11=29; 0x1B=1; 0x09=15; 0x0D=28
    0x26=200; 0x28=208; 0x25=203; 0x27=205
}
$active = $InitialInstance
$lastKeys = $null
$lastButtons = 0
$switchDown = @(0,0,0,0)
$sequence = [uint32](([DateTimeOffset]::Now.ToUnixTimeMilliseconds() % 3000000000) + 1)
$logPath = Join-Path $run 'interactive-player-switch.jsonl'

function Keyboard-Path([int]$instance) { Join-Path $GameRoot "coop_input.$instance.txt" }
function Mouse-Path([int]$instance) { Join-Path $GameRoot "coop_mouse.$instance.txt" }
function Release-Instance([int]$instance) {
    foreach ($path in @((Keyboard-Path $instance), (Mouse-Path $instance))) {
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
}
function Set-VisibleInstance([int]$instance) {
    $GWL_EXSTYLE = -20
    $WS_EX_TOOLWINDOW = 0x80L
    $WS_EX_APPWINDOW = 0x40000L
    $WS_EX_NOACTIVATE = 0x08000000L
    for ($slot = 0; $slot -lt 4; $slot++) {
        $style = [HarnessPlayerSwitch]::GetWindowLongPtr($script:windows[$slot], $GWL_EXSTYLE).ToInt64()
        if ($slot -eq $instance) {
            $style = ($style -band (-bnot ($WS_EX_TOOLWINDOW -bor $WS_EX_NOACTIVATE))) -bor $WS_EX_APPWINDOW
            [void][HarnessPlayerSwitch]::SetWindowLongPtr($script:windows[$slot], $GWL_EXSTYLE, [IntPtr]$style)
            [void][HarnessPlayerSwitch]::SetWindowText($script:windows[$slot], "DR2 Four-Player Test - Player $($slot + 1)")
            [void][HarnessPlayerSwitch]::PostMessage($script:windows[$slot], 0x8435, [IntPtr]::Zero, [IntPtr]::Zero)
            [void][HarnessPlayerSwitch]::ShowWindow($script:windows[$slot], 9)
            [void][HarnessPlayerSwitch]::SetWindowPos($script:windows[$slot], [IntPtr]::Zero, 80, 80, 960, 540, 0x0060)

            # SetForegroundWindow can be rejected when the bridge and game use different input queues.
            # Temporarily join them so camera, focus and raw mouse ownership move together.
            $foreground = [HarnessPlayerSwitch]::GetForegroundWindow()
            $foregroundProcess = 0
            $targetProcess = 0
            $foregroundThread = if ($foreground -ne [IntPtr]::Zero) {
                [HarnessPlayerSwitch]::GetWindowThreadProcessId($foreground, [ref]$foregroundProcess)
            } else { 0 }
            $targetThread = [HarnessPlayerSwitch]::GetWindowThreadProcessId($script:windows[$slot], [ref]$targetProcess)
            $currentThread = [HarnessPlayerSwitch]::GetCurrentThreadId()
            if ($foregroundThread -and $foregroundThread -ne $targetThread) {
                [void][HarnessPlayerSwitch]::AttachThreadInput($foregroundThread, $targetThread, $true)
            }
            if ($currentThread -ne $targetThread) {
                [void][HarnessPlayerSwitch]::AttachThreadInput($currentThread, $targetThread, $true)
            }
            try {
                [void][HarnessPlayerSwitch]::BringWindowToTop($script:windows[$slot])
                [void][HarnessPlayerSwitch]::SetForegroundWindow($script:windows[$slot])
                [void][HarnessPlayerSwitch]::SetActiveWindow($script:windows[$slot])
                [void][HarnessPlayerSwitch]::SetFocus($script:windows[$slot])
            } finally {
                if ($currentThread -ne $targetThread) {
                    [void][HarnessPlayerSwitch]::AttachThreadInput($currentThread, $targetThread, $false)
                }
                if ($foregroundThread -and $foregroundThread -ne $targetThread) {
                    [void][HarnessPlayerSwitch]::AttachThreadInput($foregroundThread, $targetThread, $false)
                }
            }
        } else {
            $style = ($style -bor $WS_EX_TOOLWINDOW -bor $WS_EX_NOACTIVATE) -band (-bnot $WS_EX_APPWINDOW)
            [void][HarnessPlayerSwitch]::SetWindowLongPtr($script:windows[$slot], $GWL_EXSTYLE, [IntPtr]$style)
            [void][HarnessPlayerSwitch]::SetWindowPos($script:windows[$slot], [IntPtr]::Zero, 100000, 100000, 640, 360, 0x0070)
        }
    }
}
function Write-Event($value) {
    [IO.File]::AppendAllText($logPath, ($value | ConvertTo-Json -Compress -Depth 5) + "`n")
}

try {
    Set-VisibleInstance $active
    Write-Event @{event='started';time=[DateTimeOffset]::Now.ToString('o');active=$active;pids=$OwnedPids;
        controls='F1-F4 switch; F12 release'}
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $DurationSeconds) {
        if ((Test-Path -LiteralPath (Join-Path $run 'outcome.json')) -or
            @($OwnedPids | Where-Object { -not (Get-Process -Id $_ -ErrorAction SilentlyContinue) }).Count) { break }
        if (([HarnessPlayerSwitch]::GetAsyncKeyState(0x7B) -band 0x8000) -ne 0) { break }
        for ($slot = 0; $slot -lt 4; $slot++) {
            $down = ([HarnessPlayerSwitch]::GetAsyncKeyState(0x70 + $slot) -band 0x8000) -ne 0
            if ($down -and -not $switchDown[$slot]) {
                Release-Instance $active
                $previous = $active
                $active = $slot
                $lastKeys = $null
                $lastButtons = 0
                Set-VisibleInstance $active
                Write-Event @{event='switched';time=[DateTimeOffset]::Now.ToString('o');from=$previous;active=$active}
            }
            $switchDown[$slot] = if ($down) { 1 } else { 0 }
        }
        $focused = [HarnessPlayerSwitch]::GetForegroundWindow() -eq $script:windows[$active]
        $held = @()
        if ($focused) {
            foreach ($entry in $keyMap.GetEnumerator()) {
                if (([HarnessPlayerSwitch]::GetAsyncKeyState([int]$entry.Key) -band 0x8000) -ne 0) { $held += [int]$entry.Value }
            }
        }
        $keys = ($held | Sort-Object -Unique) -join ' '
        if ($keys -ne $lastKeys) {
            $keyboardPath = Keyboard-Path $active
            if ($keys) { [IO.File]::WriteAllText($keyboardPath, $keys, [Text.Encoding]::ASCII) }
            elseif (Test-Path -LiteralPath $keyboardPath) { Remove-Item -LiteralPath $keyboardPath -Force }
            $lastKeys = $keys
        }
        if ($focused) {
            $rectangle = [HarnessPlayerSwitch+Rect]::new()
            $point = [HarnessPlayerSwitch+Point]::new()
            if ([HarnessPlayerSwitch]::GetWindowRect($script:windows[$active], [ref]$rectangle) -and
                [HarnessPlayerSwitch]::GetCursorPos([ref]$point)) {
                $centerX = [int](($rectangle.L + $rectangle.R) / 2)
                $centerY = [int](($rectangle.T + $rectangle.B) / 2)
                $x = [Math]::Max(-120, [Math]::Min(120, $point.X - $centerX))
                $y = [Math]::Max(-120, [Math]::Min(120, $point.Y - $centerY))
                $buttons = 0
                if (([HarnessPlayerSwitch]::GetAsyncKeyState(0x01) -band 0x8000) -ne 0) { $buttons = $buttons -bor 1 }
                if (([HarnessPlayerSwitch]::GetAsyncKeyState(0x02) -band 0x8000) -ne 0) { $buttons = $buttons -bor 2 }
                if ($x -ne 0 -or $y -ne 0 -or $buttons -ne $lastButtons) {
                    $sequence++
                    [IO.File]::WriteAllText((Mouse-Path $active), "$sequence $x $y 0 $buttons", [Text.Encoding]::ASCII)
                    $lastButtons = $buttons
                }
                [void][HarnessPlayerSwitch]::SetCursorPos($centerX, $centerY)
            }
        }
        Start-Sleep -Milliseconds 50
    }
} finally {
    foreach ($instance in 0..3) { Release-Instance $instance }
    if (Test-Path -LiteralPath $lockPath) { Remove-Item -LiteralPath $lockPath -Force }
    Write-Event @{event='stopped';time=[DateTimeOffset]::Now.ToString('o');active=$active}
}
